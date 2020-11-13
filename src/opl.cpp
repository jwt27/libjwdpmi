/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */

#include <jw/audio/opl.h>
#include <jw/io/io_error.h>

namespace jw::audio
{
    basic_opl::basic_opl(io::port_num port)
        : status_register { port }
        , index { port + 0, port + 2 }
        , data { port + 1, port + 3 }
        , type { detect() }
    {
        common_registers c { };
        c.mask_timer0 = true;
        c.mask_timer1 = true;
        c.enable_waveform_select = type == opl_type::opl2;
        c.enable_opl3 = type != opl_type::opl2;
        c.enable_opl3_l = type == opl_type::opl3_l;
        write(c);
    }

    void basic_opl::reset()
    {
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
        c.enable_waveform_select = common.value.enable_waveform_select;
        c.enable_opl3 = common.value.enable_opl3;
        c.enable_opl3_l = common.value.enable_opl3_l;
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
        thread::yield_for(80us);
        auto s = status();
        if (not s.timer0) throw io::device_not_found { "OPL not detected" };
        w(0x04, 0x60);          // mask both timers
        w(0x04, 0x80);          // reset irq
        if (s.opl2) return opl_type::opl2;

        w_opl3(0x02, 0xa5);     // write a distinctive value to timer 0
        thread::yield_for(2235ns);
        if (data[0].read() == std::byte { 0xa5 }) return opl_type::opl3_l;
        else return opl_type::opl3;
    }

    void basic_opl::write(const common_registers& value)
    {
        write<0x01, 0x02, 0x03, 0x04, 0x08, 0xbd, 0x101, 0x104, 0x105>(value, common, 0);
        common.value.reset_irq = false;
        common.value.hihat = false;
        common.value.top_cymbal = false;
        common.value.tomtom = false;
        common.value.snare_drum = false;
        common.value.bass_drum = false;
    }

    void basic_opl::write(const oscillator& value, std::uint8_t slot)
    {
        constexpr auto offset = [](std::uint8_t n) { return n + 2 * (n / 6); };
        if (slot >= 18) write<0x120, 0x140, 0x160, 0x180, 0x1e0>(value, oscillators[slot], offset(slot - 18));
        else write<0x20, 0x40, 0x60, 0x80, 0xe0>(value, oscillators[slot], offset(slot));
    }

    void basic_opl::write(const channel& value, std::uint8_t ch)
    {
        if (type != opl_type::opl2)
        {
            auto enable_4op = read().enable_4op.bitset();
            auto ch_4op = lookup_2to4(ch);
            if (ch_4op != 0xff and ch == lookup_4to2_sec(ch_4op) and enable_4op[ch_4op])
            {
                if (ch >= 9) write<0x1c0>(value, channels[ch], ch - 9);
                else write<0xc0>(value, channels[ch], ch);
                return;
            }
        }

        if (ch >= 9) write<0x1c0, 0x1a0, 0x1b0>(value, channels[ch], ch - 9);
        else write<0xc0, 0xa0, 0xb0>(value, channels[ch], ch);
    }

    template<unsigned... R, typename T>
    void basic_opl::write(const T& v, cached_reg<T>& cache, unsigned offset)
    {
        static_assert(sizeof...(R) <= sizeof(T));
        static constexpr unsigned regnum[] { R... };
        const reg<T> value { v };
        for (unsigned i = 0; i < sizeof...(R); ++i)
        {
            if (value.raw[i] == cache.raw[i] and cache.written[i]) continue;
            write(regnum[i] + offset, value.raw[i]);
            cache.raw[i] = value.raw[i];
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

        const bool hi = reg > 0xff;
        reg &= 0xff;
        if constexpr (opl2) if (hi) return;

        if constexpr (opl3_l) thread::yield_while([this] { return status().busy; });
        else thread::yield_while([this] { return clock::now() < last_access + (opl3 ? 2235ns : 23us); });

        if (current_index[hi] != reg) [[likely]]
        {
            index[hi].write(reg);
            if constexpr (opl2) last_access = clock::now();
            current_index[hi] = reg;
            if constexpr (opl2) thread::yield_while([this] { return clock::now() < last_access + 3300ns; });
        }

        data[hi].write(value);
        if constexpr (opl2 or opl3) last_access = clock::now();
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
        ch->key_on = base::read_channel(pri).key_on;
        write(ch);
    }

    void opl::update_config()
    {
        auto r = base::read();
        r.note_sel = cfg.note_select;
        r.tremolo_depth = cfg.tremolo_depth;
        r.vibrato_depth = cfg.vibrato_depth;
        base::write(r);
    }

    template<unsigned N> void opl::stop(channel<N>* ch)
    {
        ch->key_on = false;
        write(ch);
        ch->off_time = clock::now();
    }

    template<unsigned N> bool opl::retrigger(channel<N>* ch)
    {
        auto pri = N == 4 ? lookup_4to2_pri(ch->channel_num) : ch->channel_num;
        if (ch->owner == this and base::read_channel(pri).key_on)
        {
            ch->off_time = clock::time_point::max();
            ch->key_on = false;
            write(ch);
            ch->key_on = true;
            write(ch);
            ch->on_time = clock::now();
            return true;
        }
        else return insert(ch);
    }

    template<unsigned N> bool opl::insert_at(std::uint8_t n, channel<N>* ch)
    {
        if (ch->owner != nullptr)
        {
            ch->stop();
            ch->owner->remove(ch);
        }
        if constexpr (N == 2)
        {
            if (channels_2op[n] != nullptr) channels_2op[n]->stop();
            channels_2op[n] = ch;
            auto ch_4op = lookup_2to4(n);
            if (type != opl_type::opl2 and ch_4op != 0xff)
            {
                remove(channels_4op[ch_4op]);
                set_4op(ch_4op, false);
            }
        }
        if constexpr (N == 4)
        {
            if (channels_4op[n] != nullptr) channels_4op[n]->stop();
            channels_4op[n] = ch;
            remove(channels_2op[lookup_4to2_pri(n)]);
            remove(channels_2op[lookup_4to2_sec(n)]);
            set_4op(n, true);
        }
        ch->channel_num = n;
        ch->owner = this;
        ch->key_on = true;
        write(ch);
        ch->on_time = clock::now();
        ch->off_time = clock::time_point::max();
        return true;
    };

    template<unsigned N> bool opl::insert(channel<N>* ch)
    {
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
                check(i, base::read_channel(i).key_on, c->priority, c->on_time, c->off_time);
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
                        check(i, base::read_channel(i).key_on, c->priority, c->on_time, c->off_time);
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
                            if (a == nullptr) return std::make_tuple(base::read_channel(sec).key_on, b->priority, b->on_time, b->off_time);
                            if (b == nullptr) return std::make_tuple(base::read_channel(pri).key_on, a->priority, a->on_time, a->off_time);
                            auto a_on = base::read_channel(pri).key_on;
                            auto b_on = base::read_channel(sec).key_on;
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
                        check(i, base::read_channel(pri).key_on, c->priority, c->on_time, c->off_time);
                    }
                    else search_2op(pri, sec);
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
            if constexpr (N == 2) if (search_2op(6, 7, 8, 15, 16, 17)) return true;
            if (search_4op(0, 1, 2, 3, 4, 5)) return true;
        }

        if (best.i != 0xff) return insert_at(best.i, ch);
        return false;
    };

    template <unsigned N> void opl::remove(channel<N>* ch) noexcept
    {
        if (ch == nullptr) return;
        if (ch->owner != this) return;
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
    };

    template<unsigned N>
    opl::channel<N>::channel(const channel& c) noexcept
        : base { c }
        , connection { c.connection }
        , osc { c.osc }
        , priority { c.priority } { }

    template<unsigned N>
    opl::channel<N>& opl::channel<N>::operator=(const channel& c) noexcept
    {
        *static_cast<base*>(this) = c;
        connection = c.connection;
        osc = c.osc;
        priority = c.priority;
        return *this;
    }

    template<unsigned N>
    opl::channel<N>::channel(channel&& c) noexcept
        : base { std::move(c) }
        , connection { std::move(c.connection) }
        , osc { std::move(c.osc) }
        , priority { std::move(c.priority) }
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

    template void opl::update(channel<2>* ch);
    template void opl::stop(channel<2>* ch);
    template bool opl::retrigger(channel<2>* ch);
    template bool opl::insert_at(std::uint8_t n, channel<2>* ch);
    template bool opl::insert(channel<2>*);
    template void opl::remove(channel<2>*) noexcept;
    template void opl::write(channel<2>*);
    template void opl::move(channel<2>*) noexcept;

    template void opl::update(channel<4>* ch);
    template void opl::stop(channel<4>* ch);
    template bool opl::retrigger(channel<4>* ch);
    template bool opl::insert_at(std::uint8_t n, channel<4>* ch);
    template bool opl::insert(channel<4>*);
    template void opl::remove(channel<4>*) noexcept;
    template void opl::write(channel<4>*);
    template void opl::move(channel<4>*) noexcept;

    template struct opl::channel<2>;
    template struct opl::channel<4>;
}
