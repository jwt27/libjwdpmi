/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2020 - 2025 J.W. Jagersma, see COPYING.txt for details    */

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
        opl_channel c_tmp;
        for (auto j : { 0, 0x100 })
            for (unsigned i = 0; i < 9; ++i)
                write<true, 0xc0, 0xa0, 0xb0>(c, c_tmp, i | j);

        opl_operator o { };
        opl_operator o_tmp;
        for (auto j : { 0, 0x100 })
            for (unsigned i = 0; i < 18; ++i)
                write<true, 0x20, 0x40, 0x60, 0x80, 0xe0>(o, o_tmp, i | j);
    }

    inline void basic_opl::init()
    {
        opl_setup s { };
        s.enable_opl2 = type() == opl_type::opl2;
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
            opl_operator o { operators[i] };
            o.sustain = 0;
            o.release = 0xf;
            write(o, i);
        }
        for (unsigned i = 0; i < 18; ++i)
        {
            opl_channel c { channels[i] };
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
        reg_timer.reset_irq = false;
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
        [[assume(slot < 36)]];
        const bool hi = slot >= 18;
        const unsigned n = slot - (hi ? 18 : 0);
        const unsigned offset = n + 2 * (n / 6) + (hi ? 0x100 : 0);
        write<false, 0x20, 0x40, 0x60, 0x80, 0xe0>(value, operators[slot], offset);
    }

    void basic_opl::write(const opl_channel& value, std::uint8_t ch)
    {
        [[assume(ch < 18)]];
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
    inline void basic_opl::write(const T& v, T& cache, unsigned offset)
    {
        static_assert(sizeof...(N) <= sizeof(T));
        do_write<force, N...>(as_bytes(v).data(), as_writable_bytes(cache).data(), offset);
    }

    template<bool force, unsigned N, unsigned... Next>
    inline void basic_opl::do_write(const std::byte* value, std::byte* cache, unsigned offset)
    {
        if (force or *value != *cache)
        {
            *cache = *value;
            drv.write(N + offset, *value);
        }
        if constexpr (sizeof...(Next) > 0)
            return do_write<force, Next...>(value + 1, cache + 1, offset);
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
        for (auto* i : channels_2op) if (i != nullptr) remove(i);
        for (auto* i : channels_4op) if (i != nullptr) remove(i);
    }

    void opl::update()
    {
        for (auto* i : channels_4op) if (i != nullptr) update(i);
        for (auto* i : channels_2op) if (i != nullptr) update(i);
    }

    template<unsigned N>
    void opl::update(opl_voice<N>* ch)
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

    template<unsigned N>
    inline void opl::start(opl_voice<N>* ch)
    {
        ch->key_on(true);
        write(ch);
        ch->on_time = clock::now();
        ch->off_time = off_time(ch, true, ch->on_time);
    }

    template<unsigned N>
    void opl::stop(opl_voice<N>* ch)
    {
        const auto was_on = ch->key_on();
        ch->key_on(false);
        write(ch);
        if (was_on) ch->off_time = std::min(ch->off_time, off_time(ch, false, clock::now()));
    }

    template<unsigned N>
    inline bool opl::insert_at(std::uint8_t n, opl_voice<N>* ch)
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

    template<unsigned N>
    bool opl::insert(opl_voice<N>* ch)
    {
        if (ch->owner == this)
        {
            if (ch->key_on())
            {
                const auto pri = N == 4 ? opl_4to2_pri(ch->channel_num) : ch->channel_num;
                ch->key_on(false);
                base::write(*ch, pri);
            }
            start(ch);
            return true;
        }

        const auto now = clock::now();

        constexpr std::uint8_t npos { 0xff };

        struct
        {
            std::uint8_t i { npos };
            int priority { std::numeric_limits<int>::max() };
            clock::time_point on_time { clock::time_point::max() };
            clock::time_point off_time { clock::time_point::max() };
        } best { };

        auto check = [&](std::uint8_t i, auto prio, auto on_time, auto off_time)
        {
            if (off_time > best.off_time) return;
            if (off_time == best.off_time and
                on_time > best.on_time) return;

            if (not cfg.ignore_priority and now < off_time)
            {
                if (prio > ch->priority) return;
                if (prio > best.priority) return;
                best.priority = prio;
            }

            best.off_time = off_time;
            best.on_time = on_time;
            best.i = i;
        };

        auto search_2op = [&] (auto... order)
        {
            for (std::uint8_t i : { order... })
            {
                if (channels_2op[i] == nullptr)
                    return insert_at(i, ch);

                const auto* const c = channels_2op[i];
                check(i, c->priority, c->on_time, c->off_time);
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

                        const auto* const c = channels_4op[i];
                        check(i, c->priority, c->on_time, c->off_time);
                    }
                    else
                    {
                        const auto pri = opl_4to2_pri(i);
                        const auto sec = opl_4to2_sec(i);
                        if (channels_2op[pri] == nullptr and channels_2op[sec] == nullptr)
                            return insert_at(i, ch);

                        const auto* const a = channels_2op[pri];
                        const auto* const b = channels_2op[sec];

                        if (a == nullptr)
                            check(i, b->priority, b->on_time, b->off_time);
                        else if (b == nullptr)
                            check(i, a->priority, a->on_time, a->off_time);
                        else
                        {
                            const auto a_on = now < a->off_time;
                            const auto b_on = now < b->off_time;
                            const auto max = [a_on, b_on](auto va, auto vb) { return a_on == b_on ? std::max(va, vb) : a_on ? va : vb; };
                            const auto prio = max(a->priority, b->priority);
                            const auto on_time = max(a->on_time, b->on_time);
                            const auto off_time = std::max(a->off_time, b->off_time);

                            check(i, prio, on_time, off_time);
                        }
                    }
                }
                else if constexpr (N == 2)
                {
                    const auto pri = opl_4to2_pri(i);
                    const auto sec = opl_4to2_sec(i);
                    if (is_4op(i))
                    {
                        if (channels_4op[i] == nullptr)
                            return insert_at(pri, ch);

                        const auto* const c = channels_4op[i];
                        check(pri, c->priority, c->on_time, c->off_time);
                    }
                    else if (search_2op(pri, sec))
                        return true;
                }
            }
            return false;
        };

        if (type() == opl_type::opl2)
        {
            if constexpr (N == 2)
                if (search_2op(0, 1, 2, 3, 4, 5, 6, 7, 8))
                    return true;
        }
        else
        {
            if constexpr (N == 2)
            {
                if (search_2op(6, 7, 8, 15, 16, 17))
                    return true;
                switch (cfg.prioritize_4op)
                {
                case opl_config::auto_force:
                    if (read_4op().bitset().none())
                        break;
                    [[fallthrough]];
                case opl_config::force:
                    if (best.i != npos)
                        return insert_at(best.i, ch);
                    return false;

                case opl_config::automatic:
                    if (read_4op().bitset().none())
                        break;
                    [[fallthrough]];
                case opl_config::yes:
                    if (now >= best.off_time)
                        return insert_at(best.i, ch);
                case opl_config::no:
                    break;
                }
            }
            if (search_4op(0, 1, 2, 3, 4, 5))
                return true;
        }

        if (best.i != npos) [[likely]]
            return insert_at(best.i, ch);
        return false;
    }

    template <unsigned N>
    void opl::remove(opl_voice<N>* ch) noexcept
    {
        if (ch == nullptr) return;
        const auto pri = N == 4 ? opl_4to2_pri(ch->channel_num) : ch->channel_num;
        auto c = read_channel(pri);
        c.key_on = false;
        base::write(c, pri);
        for (unsigned i = 0; i < N; ++i)
        {
            auto o = read_operator(pri, i);
            o.sustain = 0xf;
            o.release = 0xf;
            base::write(o, pri, i);
        }
        if constexpr (N == 2) channels_2op[ch->channel_num] = nullptr;
        if constexpr (N == 4) channels_4op[ch->channel_num] = nullptr;
        ch->owner = nullptr;
    }

    template<unsigned N>
    inline void opl::write(opl_voice<N>* ch)
    {
        constexpr auto translate = [](auto n) { return N == 4 ? opl_4to2_pri(n) : n; };

        for (unsigned i = 0; i < N; ++i)
            base::write(ch->op[i], translate(ch->channel_num), i);

        if constexpr (N == 4)
        {
            opl_channel ch2 { *ch };
            ch2.connection = (ch->connection >> 1) & 1;
            base::write(ch2, opl_4to2_sec(ch->channel_num));
        }
        static_cast<opl_channel*>(ch)->connection = ch->connection & 1;
        base::write(*ch, translate(ch->channel_num));
    }

    template<unsigned N>
    inline void opl::move(opl_voice<N>* ch) noexcept
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
        static constexpr duration table[]   // Approx. us / -3dB
        {
              infinite,   infinite,   infinite,   infinite,  1318207us,  1054566us,   878791us,   749154us,
              659104us,   527283us,   439396us,   374577us,   329552us,   263641us,   219698us,   187288us,
              164776us,   131821us,   109849us,    93644us,    82388us,    65910us,    54924us,    46822us,
               41194us,    32955us,    27462us,    23411us,    20597us,    16478us,    13731us,    11706us,
               10298us,     8239us,     6866us,     5853us,     5149us,     4119us,     3433us,     2926us,
                2575us,     2060us,     1716us,     1463us,     1287us,     1030us,      858us,      732us,
                 644us,      515us,      429us,      366us,      322us,      257us,      215us,      183us,
                 161us,      129us,      107us,       91us,       80us,       80us,       80us,       80us,
        };
        return table[rate];
    }

    // Estimate when the given channel will become silent.
    template<unsigned N>
    inline opl::clock::time_point opl::off_time(const opl_voice<N>* ch, bool key_on, clock::time_point now) const noexcept
    {
        constexpr clock::time_point infinity = clock::time_point::max();

        const std::uint8_t freq_msb = (ch->freq_num >> (9 - read_setup().note_sel)) & 1;
        const std::uint8_t freq_rate = (ch->freq_block << 1) | freq_msb;
        const std::bitset<N> carriers = [ch]
        {
            if constexpr (N == 2)
                return 0b10 | ch->connection;
            if constexpr (N == 4)
                return 0b1000 | ((0b11'01'10'00 >> (ch->connection * 2)) & 0b11);
        }();

        clock::time_point off_time = clock::time_point::min();

        for (unsigned i = 0; i < N; ++i)
        {
            if (not carriers[i]) continue;
            const auto& o = ch->op[i];
            if (o.attack == 0) continue;
            if (o.release == 0) return infinity;
            if (o.decay == 0 and o.sustain != 0) return infinity;
            if (o.enable_sustain and key_on) return infinity;

            const std::uint8_t key_scale_num = freq_rate >> ((not o.key_scale_rate) << 1);
            auto key_scale = [key_scale_num](unsigned r) { return std::min((r << 2) + key_scale_num, 63u); };

            // Assumes -72dB is inaudible (24 * -3).
            const unsigned sustain_level = (o.decay == 0 ? 0u : (o.sustain == 15 ? 24u : o.sustain));
            const auto attack = attack_time(key_scale(o.attack));
            const auto decay = release_time(key_scale(o.decay)) * sustain_level;
            const auto release_from_sustain = release_time(key_scale(o.release)) * (24 - sustain_level);
            const auto release_from_0db = release_time(key_scale(o.release)) * 24;
            clock::time_point t = now;
            if (key_on)
                t += attack + decay + release_from_sustain;
            else if (now < ch->on_time + attack + decay)
                t += release_from_0db;
            else
                t += release_from_sustain;
            off_time = std::max(off_time, t);
        }
        return off_time;
    }

    template void opl::update(opl_voice<2>*);
    template void opl::stop(opl_voice<2>*);
    template bool opl::insert(opl_voice<2>*);
    template void opl::remove(opl_voice<2>*) noexcept;
    template void opl::move(opl_voice<2>*) noexcept;

    template void opl::update(opl_voice<4>*);
    template void opl::stop(opl_voice<4>*);
    template bool opl::insert(opl_voice<4>*);
    template void opl::remove(opl_voice<4>*) noexcept;
    template void opl::move(opl_voice<4>*) noexcept;
}
