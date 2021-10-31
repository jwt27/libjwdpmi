/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#include <jw/audio/opl.h>
#include <jw/io/io_error.h>
#include <jw/thread.h>

namespace jw::audio
{
    basic_opl::basic_opl(io::port_num port)
        : status_register { port }
        , index { port + 0, port + 2 }
        , data { port + 1, port + 3 }
        , type { detect() }
    {
        reset();
    }

    void basic_opl::reset()
    {
        for (unsigned i = 0; i < 36; ++i)
        {
            oscillator o { oscillators[i].value };
            o.sustain = 0;
            o.release = 0xf;
            write(o, i);
        }
        for (unsigned i = 0; i < 18; ++i)
        {
            channel c { channels[i].value };
            c.key_on = false;
            c.freq_block = 0;
            c.freq_num = 0;
            write(c, i);
        }
        common_registers c { };
        c.mask_timer0 = true;
        c.mask_timer1 = true;
        c.enable_waveform_select = type == opl_type::opl2;
        c.enable_opl3 = type != opl_type::opl2;
        c.enable_opl3_l = type == opl_type::opl3_l;
        write(c);
        c.reset_irq = true;
        write(c);
    }

    opl_type basic_opl::detect()
    {
        using namespace std::chrono_literals;
        auto w = [this] (unsigned r, unsigned v) { do_write<opl_type::opl2>(r, static_cast<std::byte>(v)); };
        auto w_opl3 = [this] (unsigned r, unsigned v) { do_write<opl_type::opl3>(r, static_cast<std::byte>(v)); };

        // from https://www.fit.vutbr.cz/~arnost/opl/opl3.html#appendixB
        w(0x04, 0x60);          // mask both timers
        w(0x04, 0x80);          // reset irq
        if (status().timer0) throw io::device_not_found { "OPL not detected" };
        w(0x02, 0xff);          // set timer 0 count 0xff
        w(0x04, 0x21);          // start timer 0
        this_thread::yield_for(100us);
        auto s = status();
        if (not s.timer0) throw io::device_not_found { "OPL not detected" };
        w(0x04, 0x60);          // mask both timers
        w(0x04, 0x80);          // reset irq
        if (s.opl2) return opl_type::opl2;

        w_opl3(0x02, 0xa5);     // write a distinctive value to timer 0
        this_thread::yield_for(2235ns);
        if (data[0].read() == std::byte { 0xa5 }) return opl_type::opl3_l;
        else return opl_type::opl3;
    }

    void basic_opl::write(const common_registers& value)
    {
        write<0x01, 0x02, 0x03, 0x04, 0x08, 0xbd, 0x101, 0x104, 0x105>(value, common, 0);
        common.value.reset_irq = false;
    }

    void basic_opl::write(const oscillator& value, std::uint8_t slot)
    {
        assume(slot < 36);
        const bool hi = slot >= 18;
        const unsigned n = slot - (hi ? 18 : 0);
        const unsigned offset = n + 2 * (n / 6) + (hi ? 0x100 : 0);
        write<0x20, 0x40, 0x60, 0x80, 0xe0>(value, oscillators[slot], offset);
    }

    void basic_opl::write(const channel& value, std::uint8_t ch)
    {
        assume(ch < 18);
        const unsigned offset = ch + (ch >= 9 ? 0x100 - 9 : 0);
        if (type != opl_type::opl2)
        {
            auto ch_4op = lookup_2to4(ch);
            if (ch_4op != 0xff and ch == lookup_4to2_sec(ch_4op) and is_4op(ch_4op))
            {
                write<0xc0>(value, channels[ch], offset);
                return;
            }
        }

        write<0xc0, 0xa0, 0xb0>(value, channels[ch], offset);
    }

    template<unsigned... R, typename T>
    void basic_opl::write(const T& v, cached_reg<T>& cache, unsigned offset)
    {
        static_assert(sizeof...(R) <= sizeof(T));
        static constexpr unsigned regnum[] { R... };
        const reg<T> value { v };
        for (unsigned i = 0; i < sizeof...(R); ++i)
        {
            std::unique_lock lock { mutex };
            if (value.raw[i] == cache.raw[i] and cache.written[i]) continue;
            write(regnum[i] + offset, value.raw[i]);
            cache.raw[i] = value.raw[i];
            cache.written[i] = true;
        }
    }

    void basic_opl::write(std::uint16_t reg, std::byte value)
    {
        switch (type)
        {
        case opl_type::opl2: return do_write<opl_type::opl2>(reg, value);
        case opl_type::opl3: return do_write<opl_type::opl3>(reg, value);
        case opl_type::opl3_l: return do_write<opl_type::opl3_l>(reg, value);
        }
    }

    template<opl_type t>
    void basic_opl::do_write(std::uint16_t reg, std::byte value)
    {
        using namespace std::chrono_literals;
        constexpr bool opl2 = t == opl_type::opl2;
        constexpr bool opl3 = t == opl_type::opl3;
        constexpr bool opl3_l = t == opl_type::opl3_l;

        const bool hi = (reg & 0x100) != 0;
        if constexpr (opl2) if (hi) return;
        reg &= 0xff;

        if constexpr (opl3_l) this_thread::yield_while([this] { return status().busy; });
        else this_thread::yield_until(last_access + (opl3 ? 2235ns : 23us));

        index[hi].write(reg);

        if constexpr (opl2) this_thread::yield_for<clock>(3300ns);

        data[hi].write(value);
        if constexpr (not opl3_l) last_access = clock::now();
    }

    void basic_opl::set_4op(std::uint8_t n, bool v)
    {
        reg enable_4op { common.value.enable_4op };
        auto bits = enable_4op.value.bitset();
        bits[n] = v;
        enable_4op.value.bitset(bits);
        if (common.value.enable_4op.bitset() != bits) write(0x104, enable_4op.raw[0]);
        common.value.enable_4op = enable_4op.value;
    };

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
        auto pri = N == 4 ? lookup_4to2_pri(ch->channel_num) : ch->channel_num;
        auto key_on = ch->key_on();
        ch->key_on(base::read_channel(pri).key_on);
        write(ch);
        ch->key_on(key_on);
    }

    void opl::update_config()
    {
        auto r = base::read();
        r.note_sel = cfg.note_select;
        r.tremolo_depth = cfg.tremolo_depth;
        r.vibrato_depth = cfg.vibrato_depth;
        base::write(r);
    }

    template<unsigned N> void opl::start(channel<N>* ch)
    {
        ch->key_on(true);
        write(ch);
        ch->on_time = clock::now();
        ch->off_time = off_time(ch, clock::time_point::max());
        if (ch->off_time != clock::time_point::max()) ch->key_on(false);
    }

    template<unsigned N> void opl::stop(channel<N>* ch)
    {
        const auto key_on = ch->key_on();
        ch->key_on(false);
        write(ch);
        if (key_on) ch->off_time = off_time(ch, clock::now());
    }

    template<unsigned N> bool opl::insert_at(std::uint8_t n, channel<N>* ch)
    {
        if (ch->owner != nullptr) ch->owner->remove(ch);

        if constexpr (N == 2)
        {
            remove(channels_2op[n]);
            auto ch_4op = lookup_2to4(n);
            if (type != opl_type::opl2 and ch_4op != 0xff)
            {
                remove(channels_4op[ch_4op]);
                set_4op(ch_4op, false);
            }
            channels_2op[n] = ch;
        }
        if constexpr (N == 4)
        {
            remove(channels_4op[n]);
            remove(channels_2op[lookup_4to2_pri(n)]);
            remove(channels_2op[lookup_4to2_sec(n)]);
            set_4op(n, true);
            channels_4op[n] = ch;
        }
        ch->channel_num = n;
        ch->owner = this;
        start(ch);
        return true;
    };

    template<unsigned N> bool opl::insert(channel<N>* ch)
    {
        if (ch->owner == this)
        {
            auto pri = N == 4 ? lookup_4to2_pri(ch->channel_num) : ch->channel_num;
            if (read_channel(pri).key_on)
            {
                ch->key_on(false);
                write(ch);
            }
            start(ch);
            return true;
        }

        struct
        {
            std::uint8_t i { 0xff };
            bool key_on { true };
            int priority { std::numeric_limits<int>::max() };
            clock::time_point on_time { clock::time_point::max() };
            clock::time_point off_time { clock::time_point::max() };
        } best { };

        auto check = [&](std::uint8_t i, bool key_on, auto prio, auto on_time, auto off_time)
        {
            if (key_on)
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
                        auto pri = lookup_4to2_pri(i);
                        auto sec = lookup_4to2_sec(i);
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
                    auto pri = lookup_4to2_pri(i);
                    auto sec = lookup_4to2_sec(i);
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

        if (type == opl_type::opl2)
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
                    if (read().enable_4op.bitset().none()) break;
                    [[fallthrough]];
                case opl_config::force:
                    if (best.i != 0xff) return insert_at(best.i, ch);
                    return false;
                case opl_config::automatic:
                    if (read().enable_4op.bitset().none()) break;
                    [[fallthrough]];
                case opl_config::yes:
                    if (best.i != 0xff and not best.key_on and best.off_time < clock::now()) return insert_at(best.i, ch);
                case opl_config::no:
                    break;
                }
            }
            if (search_4op(0, 1, 2, 3, 4, 5)) return true;
        }

        if (best.i != 0xff) return insert_at(best.i, ch);
        return false;
    };

    template <unsigned N> void opl::remove(channel<N>* ch) noexcept
    {
        if (ch == nullptr) return;
        auto pri = N == 4 ? lookup_4to2_pri(ch->channel_num) : ch->channel_num;
        auto c = read_channel(pri);
        c.key_on = false;
        base::write(c, pri);
        for (unsigned i = 0; i < N; ++i)
        {
            auto o = read_oscillator(pri, i);
            o.sustain = 0x0;
            o.release = 0xf;
            base::write(o, pri, i);
        }
        if constexpr (N == 2) channels_2op[ch->channel_num] = nullptr;
        if constexpr (N == 4) channels_4op[ch->channel_num] = nullptr;
        ch->owner = nullptr;
    };

    template<unsigned N>
    void opl::write(channel<N>* ch)
    {
        constexpr auto translate = [](auto n) { return N == 4 ? lookup_4to2_pri(n) : n; };

        for (unsigned i = 0; i < N; ++i)
            base::write(ch->osc[i], translate(ch->channel_num), i);

        if constexpr (N == 4)
        {
            base::channel ch2 { *ch };
            ch2.connection = ch->connection[1];
            base::write(ch2, lookup_4to2_sec(ch->channel_num));
        }
        static_cast<base::channel*>(ch)->connection = ch->connection[0];
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
        const bool key_on = read_channel(N == 4 ? lookup_4to2_pri(ch->channel_num) : ch->channel_num).key_on;
        const std::uint8_t freq_msb = (ch->freq_num >> (9 - read().note_sel)) & 1;
        const std::uint8_t freq_rate = (ch->freq_block << 1) | freq_msb;

        clock::time_point off_time = clock::time_point::min();

        for (unsigned i = 0; i < N; ++i)
        {
            if (not carriers[i]) continue;
            const auto& o = ch->osc[i];
            if (o.attack == 0) continue;
            if (o.release == 0) return infinity;
            if (o.enable_sustain and key_on) return infinity;

            const std::uint8_t key_scale = freq_rate >> (o.key_scale_rate << 1);
            clock::time_point t;
            duration d = release_time((o.release << 2) | key_scale);
            if (o.enable_sustain) t = key_off + d;
            else
            {
                d += attack_time((o.attack << 2) | key_scale);
                if (o.decay != 0) d += release_time((o.decay << 2) | key_scale);
                t = ch->on_time + d;
            }
            off_time = std::max(off_time, t);
        }
        return off_time;
    }

    template<unsigned N>
    opl::channel<N>::channel(const channel& c) noexcept
        : base { c } { }

    template<unsigned N>
    opl::channel<N>& opl::channel<N>::operator=(const channel& c) noexcept
    {
        *static_cast<base*>(this) = c;
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
    }

    template<unsigned N>
    opl::channel<N>& opl::channel<N>::operator=(channel&& c) noexcept
    {
        this->~channel();
        return *new (this) channel { std::move(c) };
    }

    template<unsigned N>
    opl::channel<N> opl::channel<N>::from_bytes(std::span<std::byte, sizeof(base)> bytes) noexcept
    {
        return *reinterpret_cast<const base*>(bytes.data());
    }

    template<unsigned N>
    std::array<std::byte, sizeof(typename opl::channel<N>::base)> opl::channel<N>::to_bytes() const noexcept
    {
        std::array<std::byte, sizeof(base)> array;
        std::copy_n(reinterpret_cast<const std::byte*>(this), array.size(), array.begin());
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
