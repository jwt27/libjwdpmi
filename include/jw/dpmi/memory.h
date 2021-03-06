/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <limits>
#include <memory_resource>
#include <jw/dpmi/dpmi.h>
#include <../jwdpmi_config.h>

namespace jw
{
    namespace dpmi
    {
        struct [[gnu::packed]] selector_bits
        {
            union
            {
                struct
                {
                    mutable unsigned privilege_level : 2;
                    bool local : 1;
                    unsigned index : 13;
                };
                selector value;
            };
            selector_bits() noexcept = default;
            constexpr selector_bits(selector sel) noexcept : value(sel) { };
            constexpr operator selector() const noexcept { return value; }
        };

        struct page_size_t final
        {
            constexpr operator std::size_t() const noexcept { return size; }

            explicit constexpr page_size_t(std::true_type) noexcept : size { 4_KB } { }
            explicit page_size_t(std::false_type) : size { init() } { }

        private:
            page_size_t() = delete;
            page_size_t(const page_size_t&) = delete;
            page_size_t(page_size_t&&) = delete;
            page_size_t& operator=(const page_size_t&) = delete;
            page_size_t& operator=(page_size_t&&) = delete;

            static std::size_t init()
            {
                dpmi_error_code error;
                split_uint32_t size;
                bool c;

                asm("int 0x31;"
                    : "=@ccc" (c)
                    , "=a" (error)
                    , "=b" (size.hi)
                    , "=c" (size.lo)
                    : "a" (0x0604));
                if (c) throw dpmi_error(error, "page_size");

                return size;
            }

            std::size_t size;
        } inline const [[gnu::init_priority(101)]] page_size { std::integral_constant<bool, config::assume_4k_pages> { } };

        inline std::size_t round_down_to_page_size(std::size_t num_bytes)
        {
            return num_bytes & -page_size;
        }

        inline std::size_t round_up_to_page_size(std::size_t num_bytes)
        {
            return round_down_to_page_size(num_bytes) + ((num_bytes & (page_size - 1)) == 0 ? 0 : page_size);
        }

        enum segment_type
        {
            const_data_segment = 0b000,
            data_segment = 0b001,
            stack_segment = 0b011,
            code_segment = 0b101,
            conforming_code_segment = 0b111
        };

        enum system_segment_type
        {
            call_gate16 = 0b0100,
            call_gate32 = 0b1100
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
            ldt_access_rights(selector sel);

            void set(selector sel) const;

            ldt_access_rights(auto ldt) : ldt_access_rights(ldt->get_selector()) { }
            void set(auto ldt) { set(ldt->get_selector()); }

        private:
            std::uint16_t access_rights { 0x0010 };
        };

        struct [[gnu::packed]] descriptor_data
        {
            union
            {
                struct [[gnu::packed]]
                {
                    std::uint16_t limit_lo;
                    std::uint16_t base_lo;
                    std::uint8_t base_hi_lo;
                    union
                    {
                        struct [[gnu::packed]]
                        {
                            bool has_been_accessed : 1;
                            bool is_writable : 1;
                            bool expands_downward : 1;
                            bool is_code_segment : 1;
                            bool not_system_segment : 1;
                            unsigned privilege_level : 2;
                            bool is_present : 1;
                        } data_segment;
                        struct [[gnu::packed]]
                        {
                            bool has_been_accessed : 1;
                            bool is_readable : 1;
                            bool is_conforming : 1;
                            bool is_code_segment : 1;
                            bool not_system_segment : 1;
                            unsigned privilege_level : 2;
                            bool is_present : 1;
                        } code_segment;
                        struct [[gnu::packed]]
                        {
                            bool has_been_accessed : 1;
                            unsigned : 2;
                            bool is_code_segment : 1;
                            bool not_system_segment : 1;
                            unsigned privilege_level : 2;
                            bool is_present : 1;
                        } any_segment;
                    };
                    unsigned limit_hi : 4;
                    bool available_for_system_use : 1;      // should be 0
                    unsigned : 1;                           // must be 0
                    bool is_32_bit : 1;
                    bool is_page_granular : 1;          // byte granular otherwise. note: this is automatically set by dpmi function set_selector_limit.
                    std::uint8_t base_hi_hi;
                } segment;

                struct [[gnu::packed]]
                {
                    std::uint16_t offset_lo;
                    selector cs;
                    unsigned stack_params : 5;
                    unsigned : 3;
                    system_segment_type type : 4;
                    unsigned not_system_segment : 1;
                    unsigned privilege_level : 2;
                    unsigned is_present : 1;
                    std::uint16_t offset_hi;
                } call_gate;
            };
        };
        static_assert(sizeof(descriptor_data) == 8);

        struct [[gnu::packed]] alignas(8) descriptor : descriptor_data
        {
            // Does not allocate a new descriptor
            descriptor(selector s) : sel(s) { read(); }
            ~descriptor();

            descriptor(const descriptor&) = delete;
            descriptor(const descriptor_data&) = delete;
            descriptor(descriptor_data&&) = delete;
            descriptor& operator=(const descriptor&) = delete;

            descriptor(descriptor&& d) noexcept;
            descriptor& operator=(descriptor&& d);
            descriptor& operator=(const descriptor_data& d);

            static descriptor create_segment(std::uintptr_t linear_base, std::size_t limit);
            static descriptor create_code_segment(std::uintptr_t linear_base, std::size_t limit);
            static descriptor clone_segment(selector s);
            static descriptor create_call_gate(selector code_seg, std::uintptr_t entry_point);

            auto get_selector() const noexcept { return sel; }
            void set_base(std::uintptr_t b) { set_base(sel, b); read(); }
            auto get_base() const { return get_base(sel); }
            void set_limit(std::size_t l) { set_limit(sel, l); read(); }
            std::size_t get_limit() const;
            ldt_access_rights get_access_rights();
            void set_access_rights(const ldt_access_rights& r) { r.set(sel); read(); }
            void set_selector_privilege(unsigned priv) { sel.privilege_level = priv; }

            void read() const;
            void write();

            static std::uintptr_t get_base(selector seg);
            static void set_base(selector seg, std::uintptr_t linear_base);
            static std::size_t get_limit(selector sel);
            static void set_limit(selector sel, std::size_t limit);

        private:
            descriptor() { }
            void allocate();
            void deallocate();

            selector_bits sel;
            bool no_alloc { true };
            enum direct_ldt_access_t { unknown, yes, no, ring0 };

        public:
            static direct_ldt_access_t direct_ldt_access() noexcept;
        };

        inline constexpr std::uintptr_t conventional_to_physical(std::uint16_t segment, std::uint16_t offset) noexcept
        {
            return (static_cast<std::uint32_t>(segment) << 4) + offset;
        }

        inline constexpr std::uintptr_t conventional_to_physical(far_ptr16 addr) noexcept
        {
            return conventional_to_physical(addr.segment, addr.offset);
        }

        inline constexpr far_ptr16 physical_to_conventional(std::uintptr_t address) noexcept
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

        inline std::intptr_t linear_to_near(std::uintptr_t address, selector sel = get_ds())
        {
            return address - descriptor::get_base(sel);
        }

        template <typename T>
        inline T* linear_to_near(std::uintptr_t address, selector sel = get_ds())
        {
            return reinterpret_cast<T*>(linear_to_near(address, sel));
        }

        inline std::uintptr_t near_to_linear(std::uintptr_t address, selector sel = get_ds())
        {
            return address + descriptor::get_base(sel);
        }

        template <typename T>
        inline std::uintptr_t near_to_linear(T* address, selector sel = get_ds())
        {
            return near_to_linear(reinterpret_cast<std::uintptr_t>(address), sel);
        }

        struct linear_memory
        {
            std::uintptr_t get_address() const noexcept { return addr; }
            virtual std::size_t get_size() const noexcept { return size; }

            template <typename T>
            T* get_ptr(selector sel = get_ds())
            {
                return linear_to_near<T>(addr, sel);
            }

            template <typename T>
            const T* get_ptr(selector sel = get_ds()) const
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

            linear_memory(std::shared_ptr<descriptor> l) noexcept
                : ldt(l), addr(ldt->get_base()), size((ldt->get_limit() + 1) * (ldt->segment.is_page_granular ? page_size : 0)) { }

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

            virtual std::weak_ptr<descriptor> get_descriptor()
            {
                if (not ldt) ldt = std::make_shared<descriptor>(descriptor::create_segment(addr, size));
                return ldt;
            }

            virtual selector get_selector()
            {
                return get_descriptor().lock()->get_selector();
            }

            virtual bool requires_new_selector() const noexcept { return descriptor::get_base(get_ds()) < addr; }

        protected:
            std::shared_ptr<descriptor> ldt;
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
            struct memory_resource : public std::pmr::memory_resource
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

                virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
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
                if (address <= descriptor::get_base(get_ds())) return false;
                //if (get_selector_limit() < linear_to_near(address + size)) set_selector_limit(get_ds(), address + size);
                while (descriptor::get_limit(get_ds()) < static_cast<std::uintptr_t>(linear_to_near(address + size)))
                    descriptor::set_limit(get_ds(), descriptor::get_limit(get_ds()) * 2);
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

            // 'use_old_alloc' in this context means to use the DPMI 0.9 function 0800.
            // This is useful because HDPMI does not set the cache-disable/write-through flags when
            // using this function, but it does do so with the DPMI 1.0 function 0508. It's probably
            // an oversight , but we can use this to preserve write-combining on framebuffer memory.
            device_memory_base(std::size_t num_bytes, std::uintptr_t physical_address, bool use_old_alloc = false)
                : base(no_alloc_tag { }, round_up_to_page_size(num_bytes) + page_size)
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
            mapped_dos_memory_base(std::size_t num_bytes, std::uintptr_t dos_physical_address)
                : base(no_alloc_tag { }, round_up_to_page_size(num_bytes) + page_size)
                , dos_addr(physical_to_conventional(dos_physical_address))
            {
                allocate(dos_physical_address);
            }

            mapped_dos_memory_base(std::size_t num_bytes, far_ptr16 address) : mapped_dos_memory_base(num_bytes, conventional_to_physical(address)) { }

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

            virtual std::weak_ptr<descriptor> get_descriptor() override
            {
                if (dos_handle == null_dos_handle) alloc_selector();
                if (!ldt) ldt = std::make_shared<descriptor>(dos_handle);
                return ldt;
            }

            virtual selector get_selector() override
            {
                if (dos_handle == null_dos_handle) alloc_selector();
                return dos_handle;
            }

        protected:
            mapped_dos_memory_base(no_alloc_tag, std::size_t num_bytes) : base(no_alloc_tag { }, round_up_to_page_size(num_bytes) + page_size) { }

            static bool dos_map_supported;
            std::ptrdiff_t offset { 0 };
            static constexpr selector null_dos_handle { std::numeric_limits<selector>::max() };
            selector dos_handle { null_dos_handle }; // this is actually a PM selector.
            far_ptr16 dos_addr;

            void allocate(std::uintptr_t dos_physical_address)
            {   // According to DPMI specs, this should be a linear address. This makes no sense however, and all dpmi hosts treat it as physical address.
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
                        new_alloc(dos_physical_address);
                        return;
                    }
                    //addr = dos_physical_address;    // Pretend it's a linear address anyway?
                    throw dpmi_error { unsupported_function, __PRETTY_FUNCTION__ }; // TODO: alternative solution if alloc fails
                }
                catch (...)
                {
                    std::throw_with_nested(std::bad_alloc { });
                }
            }

        private:
            void new_alloc(std::uintptr_t dos_physical_address);
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
                    base::allocate(conventional_to_physical(dos_addr));
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
                    base::allocate(conventional_to_physical(dos_addr));
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
            // Constructor arguments for each memory class:
            // memory(std::size_t num_elements, bool committed = true)
            // device_memory(std::size_t num_elements, std::uintptr_t physical_address, bool use_old_alloc = false)
            // mapped_dos_memory(std::size_t num_elements, std::uintptr_t dos_physical_address)
            // dos_memory(std::size_t num_elements)
            template<typename... Args>
            memory_t(std::size_t num_elements, Args&&... args) : base(num_elements * sizeof(T), std::forward<Args>(args)...) { }

            auto* get_ptr(selector sel = get_ds()) { return base::template get_ptr<T>(sel); }
            auto* operator->() noexcept { return get_ptr(); }
            auto& operator*() noexcept { return *get_ptr(); }
            auto& operator[](std::ptrdiff_t i) noexcept { return *(get_ptr() + i); }

            const auto* get_ptr(selector sel = get_ds()) const { return base::template get_ptr<T>(sel); }
            const auto* operator->() const noexcept { return get_ptr(); }
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
