/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2020 - 2023 J.W. Jagersma, see COPYING.txt for details    */

#include <jw/audio/opl.h>
#include <jw/io/io_error.h>
#include <jw/thread.h>

namespace jw::audio
{
    opl_driver::opl_driver(io::port_num port)
        : base { port }
        , opltype { detect() }
        , do_write { opltype == opl_type::opl3_l ? write_impl<opl_type::opl3_l> :
                     opltype == opl_type::opl3   ? write_impl<opl_type::opl3> :
                                                   write_impl<opl_type::opl2> }
    { }

    inline opl_type opl_driver::detect()
    {
        using namespace std::chrono_literals;
        auto w = [this](unsigned r, unsigned v) { write_impl<opl_type::opl2>(this, r, static_cast<std::byte>(v)); };
        auto w3 = [this](unsigned r, unsigned v) { write_impl<opl_type::opl3>(this, r, static_cast<std::byte>(v)); };

        // from https://www.fit.vutbr.cz/~arnost/opl/opl3.html#appendixB
        w(0x04, 0x60);          // mask both timers
        w(0x04, 0x80);          // reset irq
        if (status().timer0) throw io::device_not_found { "OPL not detected" };
        w(0x02, 0xff);          // set timer 0 count 0xff
        w(0x04, 0x21);          // start timer 0
        this_thread::sleep_for(100us);
        auto s = status();
        if (not s.timer0) throw io::device_not_found { "OPL not detected" };
        w(0x04, 0x60);          // stop timer
        w(0x04, 0x80);          // reset irq
        if (s.opl2) return opl_type::opl2;

        w3(0x02, 0xa5);         // write a distinctive value to timer 0
        this_thread::sleep_for(2235ns);
        if (io::read_port(base + 1) != std::byte { 0xa5 }) return opl_type::opl3;
        w3(0x105, 0x05);        // enable BUSY flag
        return opl_type::opl3_l;
    }

    template<opl_type t>
    void opl_driver::write_impl(opl_driver* drv, unsigned idx, std::byte data)
    {
        using namespace std::chrono_literals;
        constexpr bool opl2   = t == opl_type::opl2;
        constexpr bool opl3   = t == opl_type::opl3;
        constexpr bool opl3_l = t == opl_type::opl3_l;

        idx &= 0x1ff;
        const bool hi = opl2 ? false : idx > 0xff;
        const auto port = drv->base + hi * 2;

        if constexpr (opl3_l) do { } while (drv->status().busy);
        else if constexpr (opl3) do { } while (clock::now() < drv->last_access + 2235ns);
        else this_thread::sleep_until(drv->last_access + 23us);

        if (drv->index != idx)
        {
            io::write_port<std::uint8_t>(port, idx);
            drv->index = idx;

            if constexpr (opl2)
            {
                const auto now = clock::now();
                do { } while (clock::now() < now + 3300ns);
            }
        }

        io::write_port(port + 1, data);
        if constexpr (not opl3_l) drv->last_access = clock::now();
    }

    basic_opl::basic_opl(io::port_num port)
        : drv { port }
    {
        channels = { };
        operators = { };

        init();

        opl_channel c { };
        reg<opl_channel> c_tmp;
        for (auto j : { 0, 0x100 })
            for (unsigned i = 0; i < 9; ++i)
                write<true, 0xc0, 0xa0, 0xb0>(c, c_tmp, i | j);

        opl_operator o { };
        reg<opl_operator> o_tmp;
        for (auto j : { 0, 0x100 })
            for (unsigned i = 0; i < 18; ++i)
                write<true, 0x20, 0x40, 0x60, 0x80, 0xe0>(o, o_tmp, i | j);
    }

    inline void basic_opl::init()
    {
        opl_setup s { };
        s.enable_waveform_select = type() == opl_type::opl2;
        s.enable_opl3 = type() != opl_type::opl2;
        s.enable_opl3_l = type() == opl_type::opl3_l;
        s.note_sel = true;
        write<true, 0x01, 0x08, 0x101, 0x105>(s, reg_setup, 0);

        opl_timer t { };
        t.mask_timer0 = true;
        t.mask_timer1 = true;
        write<true, 0x02, 0x03, 0x04>(t, reg_timer, 0);
        t.reset_irq = true;
        write(t);

        opl_4op m { };
        write<true, 0x104>(m, reg_4op, 0);

        opl_percussion p { };
        write<true, 0xbd>(p, reg_percussion, 0);
    }

    void basic_opl::reset()
    {
        for (unsigned i = 0; i < 36; ++i)
        {
            opl_operator o { operators[i].value };
            o.sustain = 0;
            o.release = 0xf;
            write(o, i);
        }
        for (unsigned i = 0; i < 18; ++i)
        {
            opl_channel c { channels[i].value };
            c.key_on = false;
            c.freq_block = 0;
            c.freq_num = 0;
            write(c, i);
        }
        init();
    }

    void basic_opl::write(const opl_setup& value)
    {
        write<false, 0x01, 0x08, 0x101, 0x105>(value, reg_setup, 0);
    }

    void basic_opl::write(const opl_timer& value)
    {
        write<false, 0x02, 0x03, 0x04>(value, reg_timer, 0);
        reg_timer.value.reset_irq = false;
    }

    void basic_opl::write(const opl_4op& value)
    {
        write<false, 0x104>(value, reg_4op, 0);
    }

    void basic_opl::write(const opl_percussion& value)
    {
        write<false, 0xbd>(value, reg_percussion, 0);
    }

    void basic_opl::write(const opl_operator& value, std::uint8_t slot)
    {
        assume(slot < 36);
        const bool hi = slot >= 18;
        const unsigned n = slot - (hi ? 18 : 0);
        const unsigned offset = n + 2 * (n / 6) + (hi ? 0x100 : 0);
        write<false, 0x20, 0x40, 0x60, 0x80, 0xe0>(value, operators[slot], offset);
    }

    void basic_opl::write(const opl_channel& value, std::uint8_t ch)
    {
        assume(ch < 18);
        const unsigned offset = ch + (ch >= 9 ? 0x100 - 9 : 0);
        if (type() != opl_type::opl2)
        {
            auto ch_4op = opl_2to4(ch);
            if (ch_4op != 0xff and ch == opl_4to2_sec(ch_4op) and is_4op(ch_4op))
            {
                write<false, 0xc0>(value, channels[ch], offset);
                return;
            }
        }

        write<false, 0xc0, 0xa0, 0xb0>(value, channels[ch], offset);
    }

    template<bool force, unsigned... N, typename T>
    inline void basic_opl::write(const T& v, reg<T>& cache, unsigned offset)
    {
        static_assert(sizeof...(N) <= sizeof(T));
        const reg<T> value { v };
        do_write<force, 0, N...>(value.raw.data(), cache.raw.data(), offset);
    }

    template<bool force, unsigned I, unsigned N, unsigned... Next>
    inline void basic_opl::do_write(const std::byte* value, std::byte* cache, unsigned offset)
    {
        if (force or value[I] != cache[I])
        {
            cache[I] = value[I];
            drv.write(N + offset, value[I]);
        }
        if constexpr (sizeof...(Next) > 0)
            return do_write<force, I + 1, Next...>(value, cache, offset);
    }

    void basic_opl::set_4op(std::uint8_t n, bool v)
    {
        auto r = read_4op();
        auto bits = r.bitset();
        bits[n] = v;
        r.bitset(bits);
        write(r);
    }

    opl::~opl()
    {
        for (auto&& i : channels_2op) if (i != nullptr) remove(i);
        for (auto&& i : channels_4op) if (i != nullptr) remove(i);
    }

    void opl::update()
    {
        for (auto&& i : channels_4op) if (i != nullptr) update(i);
        for (auto&& i : channels_2op) if (i != nullptr) update(i);
    }

    template<unsigned N> void opl::update(channel<N>* ch)
    {
        auto pri = N == 4 ? opl_4to2_pri(ch->channel_num) : ch->channel_num;
        auto key_on = ch->key_on();
        ch->key_on(base::read_channel(pri).key_on);
        write(ch);
        ch->key_on(key_on);
    }

    void opl::update_config()
    {
        auto r = base::read_setup();
        auto p = base::read_percussion();
        r.note_sel = cfg.note_select;
        p.tremolo_depth = cfg.tremolo_depth;
        p.vibrato_depth = cfg.vibrato_depth;
        base::write(r);
        base::write(p);
    }

    template<unsigned N> void opl::start(channel<N>* ch)
    {
        ch->key_on(true);
        write(ch);
        ch->on_time = clock::now();
        ch->off_time = off_time(ch, clock::time_point::max());
    }

    template<unsigned N> void opl::stop(channel<N>* ch)
    {
        const auto was_on = ch->key_on();
        ch->key_on(false);
        write(ch);
        if (was_on) ch->off_time = off_time(ch, clock::now());
    }

    template<unsigned N> bool opl::insert_at(std::uint8_t n, channel<N>* ch)
    {
        if (ch->owner != nullptr) ch->owner->remove(ch);

        if constexpr (N == 2)
        {
            remove(channels_2op[n]);
            auto ch_4op = opl_2to4(n);
            if (type() != opl_type::opl2 and ch_4op != 0xff)
            {
                remove(channels_4op[ch_4op]);
                set_4op(ch_4op, false);
            }
            channels_2op[n] = ch;
        }
        if constexpr (N == 4)
        {
            remove(channels_4op[n]);
            remove(channels_2op[opl_4to2_pri(n)]);
            remove(channels_2op[opl_4to2_sec(n)]);
            set_4op(n, true);
            channels_4op[n] = ch;
        }
        ch->channel_num = n;
        ch->owner = this;
        start(ch);
        return true;
    }

    template<unsigned N> bool opl::insert(channel<N>* ch)
    {
        if (ch->owner == this)
        {
            if (ch->key_on())
            {
                ch->key_on(false);
                write(ch);
            }
            start(ch);
            return true;
        }

        const auto now = clock::now();

        constexpr std::uint8_t npos { 0xff };

        struct
        {
            std::uint8_t i { npos };
            bool key_on { true };
            int priority { std::numeric_limits<int>::max() };
            clock::time_point on_time { clock::time_point::max() };
            clock::time_point off_time { clock::time_point::max() };
        } best { };

        auto check = [&](std::uint8_t i, bool key_on, auto prio, auto on_time, auto off_time)
        {
            if (key_on and now < off_time)
            {
                if (not best.key_on) return;
                if (not cfg.ignore_priority)
                {
                    if (prio > ch->priority) return;
                    if (prio > best.priority) return;
                }
                if (on_time > best.on_time) return;
                best.priority = prio;
                best.on_time = on_time;
            }
            else
            {
                if (off_time > best.off_time) return;
                best.key_on = false;
                best.off_time = off_time;
            }
            best.i = i;
        };

        auto search_2op = [&] (auto... order)
        {
            for (std::uint8_t i : { order... })
            {
                if (channels_2op[i] == nullptr)
                    return insert_at(i, ch);

                auto* c = channels_2op[i];
                check(i, c->key_on(), c->priority, c->on_time, c->off_time);
            }
            return false;
        };

        auto search_4op = [&] (auto... order)
        {
            for (std::uint8_t i : { order... })
            {
                if constexpr (N == 4)
                {
                    if (is_4op(i))
                    {
                        if (channels_4op[i] == nullptr)
                            return insert_at(i, ch);

                        auto* c = channels_4op[i];
                        check(i, c->key_on(), c->priority, c->on_time, c->off_time);
                    }
                    else
                    {
                        auto pri = opl_4to2_pri(i);
                        auto sec = opl_4to2_sec(i);
                        if (channels_2op[pri] == nullptr and channels_2op[sec] == nullptr)
                            return insert_at(i, ch);

                        auto [key_on, prio, on_time, off_time] = [this, pri, sec]()
                        {
                            auto* a = channels_2op[pri];
                            auto* b = channels_2op[sec];
                            if (a == nullptr) return std::make_tuple(b->key_on(), b->priority, b->on_time, b->off_time);
                            if (b == nullptr) return std::make_tuple(a->key_on(), a->priority, a->on_time, a->off_time);
                            auto a_on = a->key_on();
                            auto b_on = b->key_on();
                            auto max = [a_on, b_on](auto va, auto vb) { return a_on == b_on ? std::max(va, vb) : a_on ? va : vb; };
                            auto on_time = max(a->on_time, b->on_time);
                            auto prio = max(a->priority, b->priority);
                            return std::make_tuple(a_on or b_on, prio, on_time, std::max(a->off_time, b->off_time));
                        }();

                        check(i, key_on, prio, on_time, off_time);
                    }
                }
                else if constexpr (N == 2)
                {
                    auto pri = opl_4to2_pri(i);
                    auto sec = opl_4to2_sec(i);
                    if (is_4op(i))
                    {
                        if (channels_4op[i] == nullptr)
                            return insert_at(pri, ch);

                        auto* c = channels_4op[i];
                        check(pri, c->key_on(), c->priority, c->on_time, c->off_time);
                    }
                    else if (search_2op(pri, sec)) return true;
                }
            }
            return false;
        };

        if (type() == opl_type::opl2)
        {
            if constexpr (N == 2) if (search_2op(0, 1, 2, 3, 4, 5, 6, 7, 8)) return true;
        }
        else
        {
            if constexpr (N == 2)
            {
                if (search_2op(6, 7, 8, 15, 16, 17)) return true;
                switch (cfg.prioritize_4op)
                {
                case opl_config::auto_force:
                    if (read_4op().bitset().none()) break;
                    [[fallthrough]];
                case opl_config::force:
                    if (best.i != npos) return insert_at(best.i, ch);
                    return false;
                case opl_config::automatic:
                    if (read_4op().bitset().none()) break;
                    [[fallthrough]];
                case opl_config::yes:
                    if (best.i != npos and not best.key_on and best.off_time < now) return insert_at(best.i, ch);
                case opl_config::no:
                    break;
                }
            }
            if (search_4op(0, 1, 2, 3, 4, 5)) return true;
        }

        if (best.i != npos) return insert_at(best.i, ch);
        return false;
    }

    template <unsigned N> void opl::remove(channel<N>* ch) noexcept
    {
        if (ch == nullptr) return;
        auto pri = N == 4 ? opl_4to2_pri(ch->channel_num) : ch->channel_num;
        auto c = read_channel(pri);
        c.key_on = false;
        base::write(c, pri);
        for (unsigned i = 0; i < N; ++i)
        {
            auto o = read_operator(pri, i);
            o.sustain = 0x0;
            o.release = 0xf;
            base::write(o, pri, i);
        }
        if constexpr (N == 2) channels_2op[ch->channel_num] = nullptr;
        if constexpr (N == 4) channels_4op[ch->channel_num] = nullptr;
        ch->owner = nullptr;
    }

    template<unsigned N>
    void opl::write(channel<N>* ch)
    {
        constexpr auto translate = [](auto n) { return N == 4 ? opl_4to2_pri(n) : n; };

        for (unsigned i = 0; i < N; ++i)
            base::write(ch->op[i], translate(ch->channel_num), i);

        if constexpr (N == 4)
        {
            opl_channel ch2 { *ch };
            ch2.connection = ch->connection[1];
            base::write(ch2, opl_4to2_sec(ch->channel_num));
        }
        static_cast<opl_channel*>(ch)->connection = ch->connection[0];
        base::write(*ch, translate(ch->channel_num));
    }

    template<unsigned N> void opl::move(channel<N>* ch) noexcept
    {
        if constexpr (N == 2) channels_2op[ch->channel_num] = ch;
        if constexpr (N == 4) channels_4op[ch->channel_num] = ch;
    }

    static auto attack_time(std::uint8_t rate) noexcept
    {
        using namespace std::chrono_literals;
        using duration = std::chrono::duration<std::uint32_t, std::micro>;
        constexpr auto infinite = duration::max();
        static constexpr duration table[]    // From YMF715 register description document
        {
              infinite,   infinite,   infinite,   infinite,  2826240us,  2252800us,  1884160us,  1597440us,
             1413120us,  1126400us,   942080us,   798720us,   706560us,   563200us,   471040us,   399360us,
              353280us,   281600us,   235520us,   199680us,   176760us,   140800us,   117760us,    99840us,
               88320us,    70400us,    58880us,    49920us,    44160us,    35200us,    29440us,    24960us,
               22080us,    17600us,    14720us,    12480us,    11040us,     8800us,     7360us,     6240us,
                5520us,     4400us,     3680us,     3120us,     2760us,     2200us,     1840us,     1560us,
                1400us,     1120us,      920us,      800us,      700us,      560us,      460us,      420us,
                 380us,      300us,      240us,      200us,        0us,        0us,        0us,        0us
        };
        return table[rate];
    }

    static auto release_time(std::uint8_t rate) noexcept
    {
        using namespace std::chrono_literals;
        using duration = std::chrono::duration<std::uint32_t, std::micro>;
        constexpr auto infinite = duration::max();
        static constexpr duration table[]
        {
              infinite,   infinite,   infinite,   infinite, 39280640us, 31416320us, 26173440us, 22446080us,
            19640320us, 15708160us, 13086720us, 11223040us,  9820160us,  7854080us,  6543360us,  5611520us,
             4910080us,  3927040us,  3271680us,  2805760us,  2455040us,  1936520us,  1635840us,  1402880us,
             1227520us,   981760us,   817920us,   701440us,   613760us,   490880us,   488960us,   350720us,
              306880us,   245440us,   204480us,   175360us,   153440us,   122720us,   102240us,    87680us,
               76720us,    61360us,    51120us,    43840us,    38360us,    30680us,    25560us,    21920us,
               19200us,    15360us,    12800us,    10960us,     9600us,     7680us,     6400us,     5480us,
                4800us,     3840us,     3200us,     2740us,     2400us,     2400us,     2400us,     2400us
        };
        return table[rate];
    }

    // Estimate when the given channel will become silent.
    template<unsigned N> opl::clock::time_point opl::off_time(const channel<N>* ch, clock::time_point key_off) const noexcept
    {
        using duration = std::chrono::duration<std::uint32_t, std::micro>;

        const std::bitset<N> carriers = [ch]
        {
            const std::uint8_t connection = ch->connection.to_ulong();
            if constexpr (N == 2) return 0b10 | connection;
            else if constexpr (N == 4) return 0b1000 | ((0b11'01'10'00 >> (connection * 2)) & 0b11);
        }();

        const clock::time_point infinity = clock::time_point::max();
        const bool key_on = read_channel(N == 4 ? opl_4to2_pri(ch->channel_num) : ch->channel_num).key_on;
        const std::uint8_t freq_msb = (ch->freq_num >> (9 - read_setup().note_sel)) & 1;
        const std::uint8_t freq_rate = (ch->freq_block << 1) | freq_msb;

        clock::time_point off_time = clock::time_point::min();

        for (unsigned i = 0; i < N; ++i)
        {
            if (not carriers[i]) continue;
            const auto& o = ch->op[i];
            if (o.attack == 0) continue;
            if (o.release == 0) return infinity;
            if (o.enable_sustain and key_on) return infinity;

            const std::uint8_t key_scale_num = freq_rate >> ((not o.key_scale_rate) << 1);
            auto key_scale = [key_scale_num](unsigned r) { return std::min((r << 2) + key_scale_num, 63u); };
            clock::time_point t;
            duration d = release_time(key_scale(o.release));
            if (o.enable_sustain) t = key_off + d;
            else
            {
                const auto a = attack_time(key_scale(o.attack));
                if (o.decay != 0) d += release_time(key_scale(o.decay));
                t = std::min(ch->on_time + a, key_off) + d;
            }
            off_time = std::max(off_time, t);
        }
        return off_time;
    }

    template<unsigned N>
    opl::channel<N>::channel(const channel& c) noexcept
        : base { c }
    {
        base::key_on = false;
    }

    template<unsigned N>
    opl::channel<N>& opl::channel<N>::operator=(const channel& c) noexcept
    {
        const bool k = base::key_on;
        *static_cast<base*>(this) = c;
        base::key_on = k;
        return *this;
    }

    template<unsigned N>
    opl::channel<N>::channel(channel&& c) noexcept
        : base { std::move(c) }
        , owner { std::move(c.owner) }
        , channel_num { std::move(c.channel_num) }
        , on_time { std::move(c.on_time) }
        , off_time { std::move(c.off_time) }
    {
        if (owner != nullptr) owner->move(this);
        c.owner = nullptr;
        c.base::key_on = false;
    }

    template<unsigned N>
    opl::channel<N>& opl::channel<N>::operator=(channel&& c) noexcept
    {
        this->~channel();
        return *new (this) channel { std::move(c) };
    }

    template<unsigned N>
    opl::channel<N> opl::channel<N>::from_bytes(std::span<const std::byte, sizeof(base)> bytes) noexcept
    {
        return *reinterpret_cast<const base*>(bytes.data());
    }

    template<unsigned N>
    std::array<std::byte, sizeof(typename opl::channel<N>::base)> opl::channel<N>::to_bytes() const noexcept
    {
        std::array<std::byte, sizeof(base)> array;
        std::memcpy(array.data(), this, sizeof(base));
        reinterpret_cast<base*>(array.data())->key_on = false;
        return array;
    }

    template void opl::update(channel<2>*);
    template void opl::stop(channel<2>*);
    template bool opl::insert(channel<2>*);
    template void opl::remove(channel<2>*) noexcept;

    template void opl::update(channel<4>*);
    template void opl::stop(channel<4>*);
    template bool opl::insert(channel<4>*);
    template void opl::remove(channel<4>*) noexcept;

    template struct opl::channel<2>;
    template struct opl::channel<4>;
}
