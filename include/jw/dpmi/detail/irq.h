#pragma once

namespace jw
{
    namespace dpmi
    {
        namespace detail
        {
            class [[gnu::packed]] irq_wrapper
            {
            public:
                using entry_fptr = void(*)(int_vector) noexcept;
                using stack_fptr = byte*(*)() noexcept;

            private:
                selector ss;                    // [esi-0x26]
                std::uint32_t* use_cnt;         // [esi-0x24]
                stack_fptr get_stack;           // [esi-0x20]
                int_vector vec;                 // [esi-0x1C]
                selector ds;                    // [esi-0x18]
                selector es;                    // [esi-0x16]
                selector fs;                    // [esi-0x14]
                selector gs;                    // [esi-0x12]
                entry_fptr entry_point;         // [esi-0x10]
                std::array<byte, 0x60> code;    // [esi-0x0C]

            public:
                irq_wrapper(int_vector _vec, entry_fptr entry_f, stack_fptr stack_f, std::uint32_t* use_cnt_ptr) noexcept;
                auto get_ptr(selector cs = get_cs()) const noexcept { return far_ptr32 { cs, reinterpret_cast<std::uintptr_t>(code.data()) }; }
            };

            struct irq_handler_base
            {   
                template<typename F>
                irq_handler_base(F func, irq_config_flags f = { }) : handler_ptr(std::forward<F>(func)), flags(f) { }
                //irq_handler_base(F func, irq_config_flags f = { }) : handler_ptr(std::allocator_arg, locking_allocator<> { }, std::forward<F>(func)), flags(f) { }
                irq_handler_base() = delete;

                //const func::function<void(ack_ptr)> handler_ptr;
                const std::function<void(ack_ptr)> handler_ptr;
                const irq_config_flags flags;
            };

            class irq
            {
                std::deque<irq_handler_base*, locking_allocator<>> handler_chain { };
                int_vector vec;
                std::shared_ptr<irq_wrapper> wrapper;
                far_ptr32 old_handler { };
                irq_config_flags flags { };

                void add_flags() noexcept { flags = { }; for (auto* p : handler_chain) flags |= p->flags; }

                static void set_pm_interrupt_vector(int_vector v, far_ptr32 ptr);
                static far_ptr32 get_pm_interrupt_vector(int_vector v);

                INTERRUPT void operator()();

                irq(const irq&) = delete;
                irq(int_vector v) : vec(v), old_handler(get_pm_interrupt_vector(v))
                {
                    wrapper = std::allocate_shared<irq_wrapper>(locking_allocator<> { }, v, interrupt_entry_point, get_stack_ptr, &stack_use_count);
                    set_pm_interrupt_vector(vec, wrapper->get_ptr());
                }

            public:
                irq(irq&& m) : handler_chain(m.handler_chain), vec(m.vec), wrapper(std::move(m.wrapper)), old_handler(m.old_handler), flags(m.flags) { m.old_handler = { }; }

                ~irq() { if (old_handler.offset != 0) set_pm_interrupt_vector(vec, old_handler); }

                void add(irq_handler_base* p) 
                { 
                    interrupt_mask no_ints_here { };
                    handler_chain.push_back(p); add_flags();
                    if (is_irq(vec)) irq_mask::unmask(vec_to_irq(vec));
                }
                void remove(irq_handler_base* p)
                {
                    interrupt_mask no_ints_here { };
                    std::remove_if(handler_chain.begin(), handler_chain.end(), [p](auto a) { return a == p; });
                    add_flags();
                }

                static irq& get(int_vector v)
                {
                    if (entries.count(v) == 0) entries.emplace(v, irq { v });
                    return entries.at(v);
                }

                static irq& get_irq(irq_level i) { return get(irq_to_vec(i)); }

            private:
                static int_vector irq_to_vec(irq_level i) noexcept
                { 
                    assert(i < 16); 
                    dpmi::version ver { };
                    return i < 8 ? i + ver.pic_master_base : i + ver.pic_slave_base; 
                }
                static irq_level vec_to_irq(int_vector v) noexcept
                { 
                    dpmi::version ver { };
                    if (v >= ver.pic_master_base && v < ver.pic_master_base + 8u) return v - ver.pic_master_base;
                    if (v >= ver.pic_slave_base && v < ver.pic_slave_base + 8u) return v - ver.pic_slave_base;
                    return 0xff;
                }

                static bool is_irq(int_vector v) { return vec_to_irq(v) != 0xff; }
                static bool is_acknowledged() { return current_int.back() == 0; }

                INTERRUPT static void acknowledge() noexcept
                {
                    if (is_acknowledged()) return;
                    send_eoi();
                    current_int.back() = 0;
                }

                INTERRUPT static auto in_service() noexcept
                {
                    split_uint16_t r;
                    pic0_cmd.write(0x0B);
                    pic1_cmd.write(0x0B);
                    r.lo = pic0_cmd.read();
                    r.hi = pic1_cmd.read();
                    return std::bitset<16> { r };
                }

                INTERRUPT static void send_eoi() noexcept
                {
                    auto v = current_int.back();
                    if (entries.at(v).flags & always_chain) return;
                    auto i = vec_to_irq(v);
                    if (i >= 16) return;
                    if (!in_service()[i]) return;

                    if (i >= 8)
                    {
                        pic1_cmd.write((i % 8) | 0x60);
                        pic0_cmd.write(0x62);
                    }
                    else pic0_cmd.write(i | 0x60);
                }

                INTERRUPT static byte* get_stack_ptr() noexcept;
                INTERRUPT static void interrupt_entry_point(int_vector vec) noexcept;

                static locked_pool_allocator<> alloc;
                static std::vector<int_vector, locked_pool_allocator<>> current_int; // Current interrupt vector. Set to 0 when acknowlegded.
                static std::unordered_map<int_vector, irq, std::hash<int_vector>, std::equal_to<int_vector>, locking_allocator<>> entries;
                static std::vector<byte, locking_allocator<>> stack;
                static std::uint32_t stack_use_count;
                static constexpr io::io_port<byte> pic0_cmd { 0x20 };
                static constexpr io::io_port<byte> pic1_cmd { 0xA0 };

                struct initializer
                {
                    initializer()
                    {
                        stack.resize(config::interrupt_initial_stack_size);
                        pic0_cmd.write(0x68);
                        pic1_cmd.write(0x68);
                    }
                } static init;
            };
        }
    }
}