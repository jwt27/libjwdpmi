/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <limits>
#include <experimental/memory_resource>
#include <jw/dpmi/dpmi.h>

namespace jw
{
    namespace dpmi
    {
        //DPMI 0.9 AX=0604
        [[gnu::pure]] inline std::size_t get_page_size()
        {
            static std::size_t page_size { 0 };
            if (__builtin_expect(page_size > 0, true)) return page_size;

            dpmi_error_code error;
            split_uint32_t size;
            bool c;

            asm("int 0x31;"
                : "=@ccc" (c)
                , "=a" (error)
                , "=b" (size.hi)
                , "=c" (size.lo)
                : "a" (0x0604));
            if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);

            page_size = size;
            return page_size;
        }

        [[gnu::pure]] inline std::size_t round_down_to_page_size(std::size_t num_bytes)
        {
            return num_bytes & -get_page_size();
        }

        [[gnu::pure]] inline std::size_t round_up_to_page_size(std::size_t num_bytes)
        {
            auto page = get_page_size();
            return round_down_to_page_size(num_bytes) + ((num_bytes & (page - 1)) == 0 ? 0 : page);
        }

        enum segment_type
        {
            const_data_segment = 0b000,
            data_segment = 0b001,
            stack_segment = 0b011,
            code_segment = 0b101,
            conforming_code_segment = 0b111
        };

        union ldt_access_rights
        {
            struct[[gnu::packed]]
            {
                unsigned has_been_accessed : 1;
                segment_type type : 3;
                unsigned system_segment : 1;            // should be 1
                unsigned privilege_level : 2;           // must be 3 for user space
                unsigned is_present : 1;                // must be 1
                unsigned : 4;
                unsigned available_for_system_use : 1;  // should be 0
                unsigned : 1;                           // must be 0
                unsigned is_32_bit : 1;
                unsigned is_page_granular : 1;          // byte granular otherwise. note: this is automatically set by dpmi function set_selector_limit.
            };
            ldt_access_rights() noexcept = default;

            ldt_access_rights(selector sel)
            {
                std::uint32_t r;
                bool z;
                asm("lar %k1, %2;"
                    : "=@ccz" (z)
                    , "=r" (r)
                    : "rm" (static_cast<std::uint32_t>(sel)));
                if (!z) throw dpmi_error(invalid_segment, __PRETTY_FUNCTION__);
                access_rights = r >> 8;
            }

            void set(selector sel) const
            {
                dpmi_error_code error;
                bool c;
                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0009)
                    , "b" (sel)
                    , "c" (access_rights)
                    : "memory");
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            }

            ldt_access_rights(auto ldt) : ldt_access_rights(ldt->get_selector()) { }
            void set(auto ldt) { set(ldt->get_selector()); }

        private:
            std::uint16_t access_rights { 0x0010 };
        };

        struct ldt_entry
        {
            union
            {
                struct
                {
                    std::uint16_t limit;
                    std::uint16_t base_lo;
                    std::uint8_t base_hi0;
                    std::uint8_t base_hi1;
                } segment;
            } descriptor;

            ldt_entry(selector s) : sel(s) { }

            ldt_entry(std::uintptr_t linear_base, std::size_t limit)
            {
                selector s;
                bool c;
                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (s)
                    : "a" (0x0000)
                    , "c" (1)
                    : "memory");
                if (c) throw dpmi_error(s, __PRETTY_FUNCTION__);
                no_alloc = false;
                sel = s;
                set_base(linear_base);
                set_limit(limit);
            }

            static auto create_alias(selector s)
            {
                selector new_sel;
                bool c;
                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (new_sel)
                    : "a" (0x000A)
                    , "b" (s)
                    : "memory");
                if (c) throw dpmi_error(new_sel, __PRETTY_FUNCTION__);
                ldt_entry ldt { new_sel };
                ldt.no_alloc = false;
                return ldt;
            }

            ~ldt_entry()
            {
                if (no_alloc) return;
                dpmi_error_code error;
                bool c;
                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0001)
                    , "b" (sel)
                    : "memory");
                // if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            }

            auto get_selector() const noexcept { return sel; }
            void set_base(auto b) { set_base(sel, b); }
            [[gnu::pure]] auto get_base() const { return get_base(sel); }
            void set_limit(auto l) { set_limit(sel, l); }
            [[gnu::pure]] auto get_limit() const { return get_limit(sel); }
            ldt_access_rights get_access_rights();
            void set_access_rights(const auto& r) { r.set(sel); }
            
            [[gnu::pure]] static std::uintptr_t get_base(selector seg = get_ds())
            {
                dpmi_error_code error;
                split_uint32_t base;
                bool c;

                asm("int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    , "=c" (base.hi)
                    , "=d" (base.lo)
                    : "a" (0x0006)
                    , "b" (seg));
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);

                return base;
            }

            static void set_base(selector seg, std::uintptr_t linear_base)
            {
                dpmi_error_code error;
                split_uint32_t base { linear_base };
                bool c;

                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0007)
                    , "b" (seg)
                    , "c" (base.hi)
                    , "d" (base.lo)
                    : "memory");
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            }

            static std::size_t get_limit(selector sel = get_ds())
            {
                std::size_t limit;
                bool z;
                asm("lsl %1, %2;"
                    : "=@ccz" (z)
                    , "=r" (limit)
                    : "rm" (static_cast<std::uint32_t>(sel))
                    : "cc");
                if (!z) throw dpmi_error(invalid_segment, __PRETTY_FUNCTION__);
                return limit;
            }

            static void set_limit(selector sel, std::size_t limit)
            {
                dpmi_error_code error;
                split_uint32_t _limit = (limit >= 1_MB) ? round_up_to_page_size(limit) - 1 : limit;
                bool c;

                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0008)
                    , "b" (sel)
                    , "c" (_limit.hi)
                    , "d" (_limit.lo)
                    : "memory");
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            }

        private:
            selector sel;
            bool no_alloc { true };
        };
            
        inline constexpr std::uintptr_t conventional_to_linear(std::uint16_t segment, std::uint16_t offset) noexcept
        {
            return (static_cast<std::uint32_t>(segment) << 4) + offset;
        }

        inline constexpr std::uintptr_t conventional_to_linear(far_ptr16 addr) noexcept
        {
            return conventional_to_linear(addr.segment, addr.offset);
        }

        inline constexpr far_ptr16 linear_to_conventional(std::uintptr_t address) noexcept
        {
            return far_ptr16(address >> 4, address & 0x0f); //TODO: round?
        }

        inline constexpr std::size_t round_down_to_paragraph_size(std::size_t num_bytes) noexcept
        {
            return num_bytes & -0x10;
        }
            
        inline constexpr std::size_t round_up_to_paragraph_size(std::size_t num_bytes) noexcept
        {
            return round_down_to_paragraph_size(num_bytes) + ((num_bytes & 0x0f) == 0 ? 0 : 0x10);
        }

        inline constexpr std::size_t bytes_to_paragraphs(std::size_t num_bytes) noexcept
        {
            return round_up_to_paragraph_size(num_bytes) >> 4;
        }

        inline constexpr std::size_t paragraphs_to_bytes(std::size_t num_paragraphs) noexcept
        {
            return num_paragraphs << 4;
        }

        [[gnu::pure]] inline std::intptr_t linear_to_near(std::uintptr_t address, selector sel = get_ds())
        {
            return address - ldt_entry::get_base(sel);
        }

        template <typename T>
        [[gnu::pure]] inline T* linear_to_near(std::uintptr_t address, selector sel = get_ds())
        {
            return reinterpret_cast<T*>(linear_to_near(address, sel));
        }

        [[gnu::pure]] inline std::uintptr_t near_to_linear(std::uintptr_t address, selector sel = get_ds())
        {
            return address + ldt_entry::get_base(sel);
        }

        template <typename T>
        [[gnu::pure]] inline std::uintptr_t near_to_linear(T* address, selector sel = get_ds())
        {
            return near_to_linear(reinterpret_cast<std::uintptr_t>(address), sel);
        }

        struct linear_memory
        {
            std::uintptr_t get_address() const noexcept { return addr; }
            virtual std::size_t get_size() const noexcept { return size; }

            template <typename T>
            [[gnu::pure]] T* get_ptr(selector sel = get_ds())
            {
                return linear_to_near<T>(addr, sel);
            }

            template <typename T>
            [[gnu::pure]] const T* get_ptr(selector sel = get_ds()) const
            {
                return get_ptr<const T>(sel);
            }

            linear_memory() : linear_memory(0, 0) { }

            template<typename T, std::enable_if_t<!std::is_void<T>::value, bool> = { }>
            linear_memory(selector seg, const T* ptr, std::size_t num_elements = 1)
                : linear_memory(dpmi::near_to_linear(ptr, seg), num_elements * sizeof(T)) { }

            linear_memory(selector seg, const void* ptr, std::size_t num_bytes)
                : linear_memory(dpmi::near_to_linear(ptr, seg), num_bytes) { }

            linear_memory(std::uintptr_t address, std::size_t num_bytes) noexcept
                : addr(address), size(num_bytes) { }

            linear_memory(const linear_memory&) noexcept = default;
            linear_memory& operator=(const linear_memory&) noexcept = default;
            linear_memory(linear_memory&& m) noexcept = default;
            linear_memory& operator=(linear_memory&& m) noexcept = default;

            //DPMI 0.9 AX=0600
            void lock_memory()
            {
                dpmi_error_code error;
                split_uint32_t _addr = addr, _size = size;
                bool c;

                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0600)
                    , "b" (_addr.hi)
                    , "c" (_addr.lo)
                    , "S" (_size.hi)
                    , "D" (_size.lo));
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            }

            //DPMI 0.9 AX=0601
            void unlock_memory()
            {
                dpmi_error_code error;
                split_uint32_t _addr = addr, _size = size;
                bool c;

                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0601)
                    , "b" (_addr.hi)
                    , "c" (_addr.lo)
                    , "S" (_size.hi)
                    , "D" (_size.lo));
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
            }

            virtual std::weak_ptr<ldt_entry> get_ldt_entry()
            {
                if (!ldt) ldt = std::make_shared<ldt_entry>(addr, size);
                return ldt;
            }

            virtual selector get_selector()
            {
                return get_ldt_entry().lock()->get_selector();
            }

            virtual bool requires_new_selector() const noexcept { return ldt_entry::get_base(get_ds()) < addr; }

        protected:
            std::shared_ptr<ldt_entry> ldt;
            std::uintptr_t addr;
            std::size_t size;
        };
        
        struct device_memory_base;
        struct mapped_dos_memory_base;
        struct dos_memory_base;

        struct no_alloc_tag { };

        struct memory_base : public linear_memory
        {
            memory_base(const linear_memory& mem, bool committed = true) : linear_memory(mem)
            {
                allocate(committed, addr);
            }

            memory_base(std::size_t num_bytes, bool committed = true) : memory_base(linear_memory { 0, num_bytes }, committed) { }

            virtual ~memory_base() 
            {
                try
                {
                    deallocate();
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Warning: caught exception while deallocating memory!\n";
                    std::cerr << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "Warning: caught exception while deallocating memory!\n";
                }
            }

            memory_base(const memory_base&) = delete;
            memory_base& operator=(const memory_base&) = delete;

            memory_base(memory_base&& m) noexcept : linear_memory(m), handle(m.handle) { m.handle = null_handle; }
            memory_base& operator=(memory_base&& m) noexcept
            {
                std::swap(handle, m.handle);
                std::swap(size, m.size);
                std::swap(addr, m.addr);
                return *this;
            }

            memory_base& operator=(const linear_memory&) = delete;
            memory_base& operator=(linear_memory&&) = delete;
            memory_base(const linear_memory&) = delete;
            memory_base(linear_memory&&) = delete;

            memory_base& operator=(device_memory_base&&) = delete;
            memory_base& operator=(mapped_dos_memory_base&&) = delete;
            memory_base& operator=(dos_memory_base&&) = delete;
            memory_base(device_memory_base&&) = delete;
            memory_base(mapped_dos_memory_base&&) = delete;
            memory_base(dos_memory_base&&) = delete;

            virtual void resize(std::size_t num_bytes, bool committed = true)
            {
                if (new_alloc_supported) new_resize(num_bytes, committed);
                else old_resize(num_bytes);
            }

            std::uint32_t get_handle() const noexcept { return handle; }
            virtual operator bool() const noexcept { return handle != null_handle; }
            virtual std::ptrdiff_t get_offset_in_block() const noexcept { return 0; }
            auto* get_memory_resource() { return &mem_res; }

        protected:
            struct memory_resource : public std::experimental::pmr::memory_resource
            {
                memory_resource(memory_base* m) : mem(m) { }

            protected:
                virtual void* do_allocate(std::size_t, std::size_t) override
                {
                    if (in_use) throw std::bad_alloc { };
                    in_use = true;
                    return mem->get_ptr<void>();
                }

                virtual void do_deallocate(void* p, std::size_t, std::size_t) noexcept override
                {
                    if (mem->get_ptr<void>() == p) in_use = false;
                }

                virtual bool do_is_equal(const std::experimental::pmr::memory_resource& other) const noexcept override 
                {
                    auto* p = dynamic_cast<const memory_resource*>(&other);
                    if (p == nullptr) return false;
                    return &p->mem == &mem;
                }

                bool in_use { false };
                memory_base* mem;
            } mem_res { this };

            memory_base(no_alloc_tag, const linear_memory& mem) noexcept : linear_memory(mem) { }
            memory_base(no_alloc_tag, std::size_t num_bytes) noexcept : memory_base(no_alloc_tag { }, linear_memory { null_handle, num_bytes }) { }

            void allocate(bool committed = true, bool new_only = false, std::uintptr_t desired_address = 0)
            {
                try
                {
                    if (new_alloc_supported) try
                    {
                        new_alloc(committed, desired_address);
                        return;
                    }
                    catch (const dpmi_error& e)
                    {
                        switch (e.code().value())
                        {
                        default: throw;
                        case unsupported_function:
                        case 0x0504:
                            new_alloc_supported = false;
                        }
                    }
                    if (new_only) return;
                    old_alloc();
                }
                catch (...)
                {
                    std::throw_with_nested(std::bad_alloc { });
                }
            }

            virtual void deallocate()
            {
                if (handle == null_handle) return;
                split_uint32_t _handle { handle };
                dpmi_error_code error;
                bool c;
                asm volatile(
                    "int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    : "a" (0x0502)
                    , "S" (_handle.hi)
                    , "D" (_handle.lo)
                    : "memory");
                if (c) throw dpmi_error(error, __PRETTY_FUNCTION__);
                handle = null_handle;
            }

            bool is_valid_address(std::uintptr_t address)
            {
                if (address <= ldt_entry::get_base(get_ds())) return false;
                //if (get_selector_limit() < linear_to_near(address + size)) set_selector_limit(get_ds(), address + size);
                while (ldt_entry::get_limit(get_ds()) < static_cast<std::uintptr_t>(linear_to_near(address + size)))
                    ldt_entry::set_limit(get_ds(), ldt_entry::get_limit(get_ds()) * 2);
                return true;
            }

            static bool new_alloc_supported;
            static constexpr std::uint32_t null_handle { std::numeric_limits<std::uint32_t>::max() };
            std::uint32_t handle { null_handle };

        private:
            void old_alloc();
            void new_alloc(bool committed, std::uintptr_t desired_address);
            void old_resize(std::size_t num_bytes);
            void new_resize(std::size_t num_bytes, bool committed);
        };

        struct device_memory_base : public memory_base
        {
            using base = memory_base;

            device_memory_base(std::size_t num_bytes, std::uintptr_t physical_address, bool use_old_alloc = false)
                : base(no_alloc_tag { }, round_up_to_page_size(num_bytes) + get_page_size())
            {
                allocate(physical_address, use_old_alloc);
            }

            device_memory_base(const base&) = delete;
            device_memory_base(const device_memory_base&) = delete;
            device_memory_base(base&&) = delete;
            device_memory_base& operator=(base&&) = delete;

            device_memory_base(device_memory_base&& m) : base(static_cast<base&&>(m)) { }
            device_memory_base& operator=(device_memory_base&& m) { base::operator=(static_cast<base&&>(m)); return *this; }

            virtual void resize(std::size_t, bool = true) override { }
            virtual bool requires_new_selector() const noexcept override { return !device_map_supported; }
            virtual operator bool() const noexcept override 
            { 
                if (device_map_supported) return base::operator bool(); 
                else return addr != null_handle; 
            };

        protected:
            static bool device_map_supported;

            void allocate(std::uintptr_t physical_address, bool use_old_alloc)
            {
                try
                {
                    if (!use_old_alloc)
                    {
                        if (device_map_supported)
                        {
                            capabilities c { };
                            if (!c.supported || !c.flags.device_mapping) device_map_supported = false;
                        }
                        if (device_map_supported)
                        {
                            base::allocate(false, false);
                            new_alloc(physical_address);
                            return;
                        }
                    }
                    old_alloc(physical_address);
                }
                catch (...)
                {
                    std::throw_with_nested(std::bad_alloc { });
                }
            }

            virtual void deallocate() override
            {
                if (device_map_supported)
                {
                    base::deallocate();
                    return;
                }
                else
                {
                    split_uint32_t old_addr { addr };
                    asm volatile(
                        "int 0x31;"
                        :: "a" (0x0801)
                        , "b" (old_addr.hi)
                        , "c" (old_addr.lo)
                        : "memory");
                    // This is an optional dpmi 1.0 function. don't care if this fails.
                }
            }

        private:
            void old_alloc(std::uintptr_t physical_address);
            void new_alloc(std::uintptr_t physical_address);
        };

        struct mapped_dos_memory_base : public memory_base
        {
            using base = memory_base;
            mapped_dos_memory_base(std::size_t num_bytes, std::uintptr_t dos_linear_address) 
                : base(no_alloc_tag { }, round_up_to_page_size(num_bytes) + get_page_size())
                , dos_addr(linear_to_conventional(dos_linear_address))
            {
                allocate(dos_linear_address);
            }

            mapped_dos_memory_base(std::size_t num_bytes, far_ptr16 address) : mapped_dos_memory_base(num_bytes, conventional_to_linear(address)) { }

            mapped_dos_memory_base(const base&) = delete;
            mapped_dos_memory_base(const mapped_dos_memory_base&) = delete;
            mapped_dos_memory_base(base&&) = delete;
            mapped_dos_memory_base& operator=(base&&) = delete;

            mapped_dos_memory_base(mapped_dos_memory_base&& m) 
                : base(static_cast<base&&>(m)), offset(m.offset), dos_handle(m.dos_handle), dos_addr(m.dos_addr) { m.dos_handle = null_dos_handle;  }
            mapped_dos_memory_base& operator=(mapped_dos_memory_base&& m) 
            {
                base::operator=(static_cast<base&&>(m)); 
                std::swap(dos_handle, m.dos_handle);
                std::swap(dos_addr, m.dos_addr);
                std::swap(offset, m.offset);
                return *this; 
            }
            
            virtual void resize(std::size_t, bool = true) override { }
            virtual bool requires_new_selector() const noexcept override { return !dos_map_supported; }
            auto get_dos_ptr() const noexcept { return dos_addr; }
            virtual std::ptrdiff_t get_offset_in_block() const noexcept override { return offset; }
            virtual operator bool() const noexcept override
            { 
                if (dos_map_supported) return base::operator bool(); 
                else return addr != null_handle;
            };

            virtual std::weak_ptr<ldt_entry> get_ldt_entry() override
            {
                if (dos_handle == null_dos_handle) alloc_selector();
                if (!ldt) ldt = std::make_shared<ldt_entry>(dos_handle);
                return ldt;
            }

            virtual selector get_selector() override
            {
                if (dos_handle == null_dos_handle) alloc_selector();
                return dos_handle; 
            }

        protected:
            mapped_dos_memory_base(no_alloc_tag, std::size_t num_bytes) : base(no_alloc_tag { }, round_up_to_page_size(num_bytes) + get_page_size()) { }

            static bool dos_map_supported;
            std::ptrdiff_t offset { 0 };
            static constexpr selector null_dos_handle { std::numeric_limits<selector>::max() };
            selector dos_handle { null_dos_handle }; // this is actually a PM selector.
            far_ptr16 dos_addr;

            void allocate(std::uintptr_t dos_linear_address)
            {
                try
                {
                    if (dos_map_supported)
                    {
                        capabilities c { };
                        if (!c.supported || !c.flags.conventional_memory_mapping) dos_map_supported = false;
                    }
                    if (dos_map_supported)
                    {
                        base::allocate(false, false);
                        new_alloc(dos_linear_address);
                        return;
                    }
                    addr = dos_linear_address;
                }
                catch (...)
                {
                    std::throw_with_nested(std::bad_alloc { });
                }
            }

        private:
            void new_alloc(std::uintptr_t dos_linear_address);
            void alloc_selector();
        };

        struct dos_memory_base : public mapped_dos_memory_base
        {
            using base = mapped_dos_memory_base;

            dos_memory_base(std::size_t num_bytes) : base(no_alloc_tag { }, round_up_to_paragraph_size(num_bytes))
            {
                allocate(num_bytes);
            }

            dos_memory_base(const base&) = delete;
            dos_memory_base(const dos_memory_base&) = delete;
            dos_memory_base(base&&) = delete;
            dos_memory_base& operator=(base&&) = delete;

            dos_memory_base(dos_memory_base&& m) : base(static_cast<base&&>(m)) { }
            dos_memory_base& operator=(dos_memory_base&& m) { base::operator=(static_cast<base&&>(m)); return *this; }

            virtual void resize(std::size_t num_bytes, bool = true) override
            {
                try
                {
                    base::deallocate();
                    dos_resize(round_up_to_paragraph_size(num_bytes));
                    base::allocate(conventional_to_linear(dos_addr));
                }
                catch (...)
                {
                    std::throw_with_nested(std::bad_alloc { });
                }
            }

            virtual operator bool() const noexcept override { return dos_handle != null_dos_handle; };

        protected:
            void allocate(std::size_t num_bytes)
            {
                try
                {
                    deallocate();
                    dos_alloc(round_up_to_paragraph_size(num_bytes));
                    base::allocate(conventional_to_linear(dos_addr));
                }
                catch (...)
                {
                    std::throw_with_nested(std::bad_alloc { });
                }
            }

            virtual void deallocate() override
            {
                base::deallocate();
                if (dos_handle == null_dos_handle) return;
                dos_dealloc();
            }

        private:
            void dos_alloc(std::size_t num_bytes);
            void dos_dealloc();
            void dos_resize(std::size_t num_bytes);
        };

        template <typename T, typename base = memory_base>
        struct memory_t : public base
        {
            template<typename... Args>
            memory_t(std::size_t num_elements, Args&&... args) : base(num_elements * sizeof(T), std::forward<Args>(args)...) { }
            
            [[gnu::pure]] auto* get_ptr(selector sel = get_ds()) { return base::template get_ptr<T>(sel); }
            [[gnu::pure]] auto* operator->() noexcept { return get_ptr(); }
            auto& operator*() noexcept { return *get_ptr(); }
            auto& operator[](std::ptrdiff_t i) noexcept { return *(get_ptr() + i); }

            [[gnu::pure]] const auto* get_ptr(selector sel = get_ds()) const { return base::template get_ptr<T>(sel); }
            [[gnu::pure]] const auto* operator->() const noexcept { return get_ptr(); }
            const auto& operator*() const noexcept { return *get_ptr(); }
            const auto& operator[](std::ptrdiff_t i) const noexcept { return *(get_ptr() + i); }

            template<typename... Args>
            void resize(std::size_t num_elements, Args&&... args) { base::resize(num_elements * sizeof(T), std::forward<Args>(args)...); }
            virtual std::size_t get_size() const noexcept override { return base::get_size() / sizeof(T); }
        };

        template <typename T = byte> using memory = memory_t<T, memory_base>;
        template <typename T = byte> using device_memory = memory_t<T, device_memory_base>;
        template <typename T = byte> using mapped_dos_memory = memory_t<T, mapped_dos_memory_base>;
        template <typename T = byte> using dos_memory = memory_t<T, dos_memory_base>;
    }
}
