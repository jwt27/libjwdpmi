/* * * * * * * * * * * * * * * * * * jwdpmi * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2017 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <jw/dpmi/dpmi.h>
#include <jw/dpmi/detail/selectors.h>
#include <sys/nearptr.h>
#include <limits>
#include <optional>
#include <utility>
#include "jwdpmi_config.h"

namespace jw::dpmi
{
    union selector_bits
    {
        struct [[gnu::packed]]
        {
            unsigned privilege_level : 2;
            bool local : 1;
            unsigned index : 13;
        };
        selector value;

        constexpr selector_bits() noexcept = default;
        constexpr selector_bits(selector sel) noexcept : value { sel } { };
        constexpr operator selector() const noexcept { return value; }
    };

    static_assert (sizeof(selector_bits) == sizeof(selector));

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

            asm("int 0x31"
                : "=@ccc" (c)
                , "=a" (error)
                , "=b" (size.hi)
                , "=c" (size.lo)
                : "a" (0x0604));
            if (c) throw dpmi_error(error, "page_size");

            return size;
        }

        std::size_t size;
    };
    [[gnu::init_priority(101)]] inline const page_size_t page_size { std::integral_constant<bool, config::assume_4k_pages> { } };

    enum system_segment_type
    {
        call_gate16 = 0b0100,
        call_gate32 = 0b1100
    };

    union alignas(4) descriptor_data
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

            constexpr std::uintptr_t base() const noexcept
            {
                return split_uint32_t { base_lo, split_uint16_t { base_hi_lo, base_hi_hi } };
            }

            constexpr void base(std::uintptr_t b) noexcept
            {
                split_uint32_t base { b };
                base_lo = base.lo;
                base_hi_lo = base.hi.lo;
                base_hi_hi = base.hi.hi;
            }

            // Note: not adjusted for granularity!
            constexpr std::size_t limit() const noexcept
            {
                return split_uint32_t { limit_lo, limit_hi };
            }

            // Note: not adjusted for granularity!
            constexpr void limit(std::size_t l) noexcept
            {
                split_uint32_t lim { l };
                limit_lo = lim.lo;
                limit_hi = lim.hi;
            }
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
    static_assert(sizeof(descriptor_data) == 8);

    // Represents a descriptor in the LDT or GDT.
    // The static functions (create_segment, etc) allocate a new descriptor
    // which is freed upon destruction.  When created via the constructor that
    // takes a selector, this class does not take ownership of the descriptor.
    struct descriptor
    {
        // Does not allocate a new descriptor
        constexpr descriptor(selector s) noexcept : sel(s) { }
        ~descriptor();

        descriptor(const descriptor&) = delete;
        descriptor& operator=(const descriptor&) = delete;

        descriptor(descriptor&&) noexcept;
        descriptor& operator=(descriptor&&);

        descriptor& operator=(const descriptor_data& d) { write(d); return *this; }

        static descriptor create();
        static descriptor create_segment(std::uintptr_t linear_base, std::size_t limit);
        static descriptor create_code_segment(std::uintptr_t linear_base, std::size_t limit);
        static descriptor clone_segment(selector s);
        static descriptor create_call_gate(selector code_seg, std::uintptr_t entry_point);

        selector get_selector() const noexcept { return sel; }
        void set_base(std::uintptr_t b) { set_base(sel, b); }
        std::uintptr_t get_base() const { return get_base(sel); }
        void set_limit(std::size_t l) { set_limit(sel, l); }
        std::size_t get_limit() const { return get_limit(sel); }
        void set_selector_privilege(unsigned priv) { sel.privilege_level = priv; }

        [[nodiscard]] descriptor_data read() const;
        void write(const descriptor_data&);

        static std::uintptr_t get_base(selector seg);
        static void set_base(selector seg, std::uintptr_t linear_base);
        static std::size_t get_limit(selector sel);
        static void set_limit(selector sel, std::size_t limit);

    private:
        constexpr descriptor() noexcept = default;
        void allocate();
        void deallocate();

        selector_bits sel;
        bool no_alloc { true };
    };

    inline std::size_t round_down_to_page_size(std::size_t num_bytes)
    {
        return num_bytes & -page_size;
    }

    inline std::size_t round_up_to_page_size(std::size_t num_bytes)
    {
        return round_down_to_page_size(num_bytes + page_size - 1);
    }

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
        return round_down_to_paragraph_size(num_bytes + 0x0f);
    }

    inline constexpr std::size_t bytes_to_paragraphs(std::size_t num_bytes) noexcept
    {
        return round_up_to_paragraph_size(num_bytes) >> 4;
    }

    inline constexpr std::size_t paragraphs_to_bytes(std::size_t num_paragraphs) noexcept
    {
        return num_paragraphs << 4;
    }

    inline std::intptr_t linear_to_near(std::uintptr_t address)
    {
        return address - __djgpp_base_address;
    }

    inline std::intptr_t linear_to_near(std::uintptr_t address, selector sel)
    {
        return address - descriptor::get_base(sel);
    }

    template <typename T>
    inline T* linear_to_near(std::uintptr_t address)
    {
        return reinterpret_cast<T*>(linear_to_near(address));
    }

    template <typename T>
    inline T* linear_to_near(std::uintptr_t address, selector sel)
    {
        return reinterpret_cast<T*>(linear_to_near(address, sel));
    }

    inline std::uintptr_t near_to_linear(std::uintptr_t address)
    {
        return address + __djgpp_base_address;
    }

    inline std::uintptr_t near_to_linear(std::uintptr_t address, selector sel)
    {
        return address + descriptor::get_base(sel);
    }

    template <typename T>
    inline std::uintptr_t near_to_linear(T* address)
    {
        return near_to_linear(reinterpret_cast<std::uintptr_t>(address));
    }

    template <typename T>
    inline std::uintptr_t near_to_linear(T* address, selector sel)
    {
        return near_to_linear(reinterpret_cast<std::uintptr_t>(address), sel);
    }

    struct bad_dos_alloc final : std::bad_alloc
    {
        bad_dos_alloc(std::size_t max) noexcept : bad_alloc { }, max_size { max } { }

        // Largest available block size in bytes.
        std::size_t max_size;

        virtual const char* what() const noexcept override { return "Insufficient conventional memory"; }

        virtual ~bad_dos_alloc() noexcept = default;
        constexpr bad_dos_alloc(bad_dos_alloc&&) noexcept = default;
        constexpr bad_dos_alloc(const bad_dos_alloc&) noexcept = default;
        constexpr bad_dos_alloc& operator=(bad_dos_alloc&&) noexcept = default;
        constexpr bad_dos_alloc& operator=(const bad_dos_alloc&) noexcept = default;
    };

    struct dos_alloc_result
    {
        // Conventional memory pointer.  This is always aligned to a 16-byte
        // boundary, so pointer.offset always equals 0.
        far_ptr16 pointer;

        // Selector used to access allocated memory.  Doubles as a handle for
        // free/resize operations.
        selector handle;
    };

    // Allocate conventional memory.  Size is given in bytes, but is rounded
    // up to paragraphs (16 bytes).
    [[nodiscard]] dos_alloc_result dos_allocate(std::size_t);

    // Resize conventional memory block in-place.
    void dos_resize(selector, std::size_t);
    inline void dos_resize(const dos_alloc_result& r, std::size_t n) { return dos_resize(r.handle, n); }

    // Free conventional memory.
    void dos_free(selector);
    inline void dos_free(const dos_alloc_result& r) { return dos_free(r.handle); };

    // Allocate a selector for a 64KB segment in conventional memory.  Always
    // returns the same selector, given the same segment.  This selector must
    // never be modified or freed, so use sparingly.
    [[nodiscard]] selector dos_selector(std::uint16_t);

    // Describes an existing linear memory region.  Does not own any memory.
    struct linear_memory
    {
        std::uintptr_t address() const noexcept { return addr; }
        std::size_t size() const noexcept { return bytes; }

        template <typename T>
        T* near_pointer() const { return linear_to_near<T>(addr); }

        template<typename T>
        static linear_memory from_pointer(const T* ptr, std::size_t n) { return linear_memory { dpmi::near_to_linear(ptr), n * sizeof(T) }; }
        static linear_memory from_pointer(const void* ptr, std::size_t n) { return linear_memory { dpmi::near_to_linear(ptr), n }; }

        constexpr linear_memory() = default;

        linear_memory(std::uintptr_t address, std::size_t num_bytes) noexcept
            : addr(address), bytes(num_bytes) { }

        linear_memory(const descriptor& d) noexcept
        {
            auto data = d.read();
            addr = data.segment.base();
            bytes = data.segment.limit() * (data.segment.is_page_granular ? page_size : 1);
        }

        linear_memory(const linear_memory&) noexcept = default;
        linear_memory& operator=(const linear_memory&) noexcept = default;
        linear_memory(linear_memory&& m) noexcept = default;
        linear_memory& operator=(linear_memory&& m) noexcept = default;

        // DPMI 0.9 AX=0600
        void lock()
        {
            std::uint16_t ax { 0x0600 };
            split_uint32_t _addr = addr, _size = bytes;
            bool c;

            asm volatile
            (
                "int 0x31"
                : "=@ccc" (c), "+a" (ax)
                : "b" (_addr.hi), "c" (_addr.lo)
                , "S" (_size.hi), "D" (_size.lo)
            );
            if (c) throw dpmi_error { ax, __PRETTY_FUNCTION__ };
        }

        // DPMI 0.9 AX=0601
        void unlock()
        {
            std::uint16_t ax { 0x0601 };
            split_uint32_t _addr = addr, _size = bytes;
            bool c;

            asm volatile
            (
                "int 0x31"
                : "=@ccc" (c), "+a" (ax)
                : "b" (_addr.hi), "c" (_addr.lo)
                , "S" (_size.hi), "D" (_size.lo)
            );
            if (c) throw dpmi_error { ax, __PRETTY_FUNCTION__ };
        }

        [[nodiscard]] descriptor create_segment()
        {
            return descriptor::create_segment(addr, bytes);
        }

        bool near_pointer_accessible() const noexcept
        {
            return addr >= static_cast<std::uintptr_t>(__djgpp_base_address)
                and addr + bytes <= static_cast<std::size_t>(__djgpp_base_address + __djgpp_selector_limit + 1);
        }

    protected:
        std::uintptr_t addr;
        std::size_t bytes;
    };

    struct device_memory_base;
    struct mapped_dos_memory_base;
    struct dos_memory_base;

    struct no_alloc_tag { };

    // Silence warning about resize() being hidden in memory_t below.  This is
    // very much intentional.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-virtual"

    struct memory_base : public linear_memory
    {
        memory_base(const linear_memory& mem, bool committed = true)
            : linear_memory { mem }
        {
            allocate(true, committed, addr);
        }

        memory_base(std::size_t num_bytes, bool committed = true)
            : linear_memory { 0, num_bytes }
        {
            allocate(false, committed, 0);
        }

        virtual ~memory_base()
        {
            try
            {
                deallocate();
            }
            catch (...)
            {
#               ifndef NDEBUG
                fmt::print(stderr, "Warning: caught exception while deallocating memory!\n");
                try { throw; }
                catch (const std::exception& e) { fmt::print(stderr, "{}\n", e.what()); }
#               endif
            }
        }

        memory_base(const memory_base&) = delete;
        memory_base& operator=(const memory_base&) = delete;

        memory_base(memory_base&& m) noexcept
            : linear_memory { m }
            , handle { std::exchange(m.handle, 0) }
        { }
        memory_base& operator=(memory_base&& m) noexcept
        {
            std::swap(handle, m.handle);
            std::swap(bytes, m.bytes);
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

        std::uint32_t get_handle() const noexcept { return handle; }
        virtual std::ptrdiff_t offset_in_block() const noexcept { return 0; }

    protected:
        memory_base(no_alloc_tag, const linear_memory& mem) noexcept
            : linear_memory { mem }
        { }
        memory_base(no_alloc_tag, std::size_t num_bytes) noexcept
            : memory_base { no_alloc_tag { }, linear_memory { 0, num_bytes } }
        { }

        void allocate(bool dpmi10_alloc_only, bool committed, std::uintptr_t desired_address);
        virtual bool allocated() const noexcept { return handle != 0; }
        virtual void deallocate();
        virtual void resize(std::size_t num_bytes, bool committed = true);

        static inline bool dpmi10_alloc_supported = true;

    private:
        void dpmi09_alloc();
        [[nodiscard]] std::optional<dpmi_error> dpmi10_alloc(bool committed, std::uintptr_t desired_address);
        void dpmi09_resize(std::size_t num_bytes);
        void dpmi10_resize(std::size_t num_bytes, bool committed);

        std::uint32_t handle { 0 };
    };

    struct device_memory_base : public memory_base
    {
        // 'use_dpmi09_alloc' in this context means to use the DPMI 0.9
        // function 0800.  This is useful because HDPMI does not set the
        // cache-disable/write-through flags when using this function, but it
        // does do so with the DPMI 1.0 function 0508.  It's probably an
        // oversight, but we can use this to preserve write-combining on
        // framebuffer memory.
        device_memory_base(std::size_t num_bytes, std::uintptr_t physical_address, bool use_dpmi09_alloc = false)
            : memory_base(no_alloc_tag { }, round_up_to_page_size(num_bytes) + page_size)
        {
            allocate(physical_address, use_dpmi09_alloc);
        }

        device_memory_base(const memory_base&) = delete;
        device_memory_base(const device_memory_base&) = delete;
        device_memory_base(memory_base&&) = delete;
        device_memory_base& operator=(memory_base&&) = delete;

        device_memory_base(device_memory_base&& m)
            : memory_base(static_cast<memory_base&&>(m))
        {
            m.addr = 0;
        }
        device_memory_base& operator=(device_memory_base&& m)
        {
            memory_base::operator=(static_cast<memory_base&&>(m));
            return *this;
        }

    protected:
        void allocate(std::uintptr_t physical_address, bool use_dpmi09_alloc);
        virtual bool allocated() const noexcept override { return addr != 0; };
        virtual void deallocate() override;

        virtual void resize(std::size_t, bool = true) override
        {
            throw dpmi_error { unsupported_function, __PRETTY_FUNCTION__ };
        }

    private:
        [[nodiscard]]
        std::optional<dpmi_error> dpmi10_alloc(std::uintptr_t physical_address);
        void dpmi09_alloc(std::uintptr_t physical_address);

        static inline bool dpmi10_alloc_supported = true;
    };

    struct mapped_dos_memory_base : public memory_base
    {
        mapped_dos_memory_base(std::size_t num_bytes, std::uintptr_t dos_physical_address)
            : memory_base { no_alloc_tag { }, round_up_to_page_size(num_bytes) + page_size }
            , dos_addr { physical_to_conventional(dos_physical_address) }
        {
            allocate(dos_physical_address);
        }

        mapped_dos_memory_base(std::size_t num_bytes, far_ptr16 address)
            : mapped_dos_memory_base { num_bytes, conventional_to_physical(address) }
        { }

        mapped_dos_memory_base(const memory_base&) = delete;
        mapped_dos_memory_base(const mapped_dos_memory_base&) = delete;
        mapped_dos_memory_base(memory_base&&) = delete;
        mapped_dos_memory_base& operator=(memory_base&&) = delete;

        mapped_dos_memory_base(mapped_dos_memory_base&& m)
            : memory_base { static_cast<memory_base&&>(m) }
            , dos_addr { m.dos_addr }
            , offset { m.offset }
        { }
        mapped_dos_memory_base& operator=(mapped_dos_memory_base&& m)
        {
            memory_base::operator=(static_cast<memory_base&&>(m));
            std::swap(dos_addr, m.dos_addr);
            std::swap(offset, m.offset);
            return *this;
        }

        auto dos_pointer() const noexcept { return dos_addr; }
        virtual std::ptrdiff_t offset_in_block() const noexcept override { return offset; }

    protected:
        mapped_dos_memory_base(no_alloc_tag, std::size_t num_bytes)
            : memory_base { no_alloc_tag { }, round_up_to_page_size(num_bytes) + page_size }
        { }

        void allocate(std::uintptr_t dos_physical_address);
        virtual void resize(std::size_t, bool = true) override
        {
            throw dpmi_error { unsupported_function, __PRETTY_FUNCTION__ };
        }

        far_ptr16 dos_addr;

    private:
        std::ptrdiff_t offset { 0 };
    };

    struct dos_memory_base : public mapped_dos_memory_base
    {
        dos_memory_base(std::size_t num_bytes)
            : mapped_dos_memory_base { no_alloc_tag { }, round_up_to_paragraph_size(num_bytes) }
        {
            allocate();
        }

        dos_memory_base(const mapped_dos_memory_base&) = delete;
        dos_memory_base(const dos_memory_base&) = delete;
        dos_memory_base(mapped_dos_memory_base&&) = delete;
        dos_memory_base& operator=(mapped_dos_memory_base&&) = delete;

        dos_memory_base(dos_memory_base&& m)
            : mapped_dos_memory_base { static_cast<mapped_dos_memory_base&&>(m) }
            , dos_handle { std::exchange(m.dos_handle, 0) }
        { }
        dos_memory_base& operator=(dos_memory_base&& m)
        {
            mapped_dos_memory_base::operator=(static_cast<mapped_dos_memory_base&&>(m));
            std::swap(dos_handle, m.dos_handle);
            return *this;
        }

        selector get_selector() const noexcept { return dos_handle; }

    protected:
        void allocate();
        virtual bool allocated() const noexcept override { return dos_handle != 0; };
        virtual void deallocate() override;
        virtual void resize(std::size_t num_bytes, bool = true) override;

    private:
        selector dos_handle { 0 };
    };

    template <typename T, typename base>
    struct memory_t final : public base
    {
        // Constructor arguments for each memory class:
        // memory(std::size_t num_elements, bool committed = true)
        // device_memory(std::size_t num_elements, std::uintptr_t physical_address, bool use_dpmi09_alloc = false)
        // mapped_dos_memory(std::size_t num_elements, std::uintptr_t dos_physical_address)
        // mapped_dos_memory(std::size_t num_elements, far_ptr16 dos_address)
        // dos_memory(std::size_t num_elements)
        template<typename... Args>
        memory_t(std::size_t num_elements, Args&&... args)
            : base { num_elements * sizeof(T), std::forward<Args>(args)... }
        { }

        auto* near_pointer() const { return base::template near_pointer<T>(); }
        auto* operator->() const noexcept { return near_pointer(); }
        auto& operator*() const noexcept { return *near_pointer(); }
        auto& operator[](std::ptrdiff_t i) const noexcept { return *(near_pointer() + i); }

        template<typename... Args>
        void resize(std::size_t num_elements, Args&&... args)
        {
            base::resize(num_elements * sizeof(T), std::forward<Args>(args)...);
        }

        std::size_t size() const noexcept
        {
            return base::size() / sizeof(T);
        }
    };

    template <typename T = std::byte> using memory = memory_t<T, memory_base>;
    template <typename T = std::byte> using device_memory = memory_t<T, device_memory_base>;
    template <typename T = std::byte> using mapped_dos_memory = memory_t<T, mapped_dos_memory_base>;
    template <typename T = std::byte> using dos_memory = memory_t<T, dos_memory_base>;

#pragma GCC diagnostic pop
}
