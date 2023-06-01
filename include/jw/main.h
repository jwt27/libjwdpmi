/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <exception>
#include <new>

int main(int, const char**);

namespace jw
{
    struct init;

    // Print the current exception to stderr, including any nested exceptions.
    void print_exception() noexcept;

    // Terminate via forced unwinding.  The idea is to clean up as much as
    // possible, and hopefully restore the system to a usable state.  The OS
    // won't do it for you.
    // If unwinding fails, std::terminate() is called.
    [[noreturn]] void terminate();

    [[noreturn]] inline void halt() { do { asm ("cli"); } while (true); }

    [[nodiscard]] void* realloc(void* pointer, std::size_t new_size, std::size_t alignment);

    [[nodiscard]] void* allocate(std::size_t, std::size_t = __STDCPP_DEFAULT_NEW_ALIGNMENT__);
    [[nodiscard]] void* allocate_locked(std::size_t, std::size_t = __STDCPP_DEFAULT_NEW_ALIGNMENT__);
    void free(void*, std::size_t, std::size_t = __STDCPP_DEFAULT_NEW_ALIGNMENT__);
    void free_locked(void*, std::size_t, std::size_t = __STDCPP_DEFAULT_NEW_ALIGNMENT__);

    // This tag type may be used to allocate from a pre-allocated locked
    // memory pool, using the 'operator new' overloads below.  This also works
    // with arrays.  The returned pointer can be deallocated using a regular
    // 'delete' or 'delete[]' expression.
    // Example: auto* p = new (jw::locked) int[128];
    struct locked_alloc_tag { } constexpr inline locked;
}

[[nodiscard]] inline void* operator new  (std::size_t n, const jw::locked_alloc_tag&) { return jw::allocate_locked(n); }
[[nodiscard]] inline void* operator new[](std::size_t n, const jw::locked_alloc_tag&) { return jw::allocate_locked(n); }
[[nodiscard]] inline void* operator new  (std::size_t n, std::align_val_t a, const jw::locked_alloc_tag&) { return jw::allocate_locked(n, static_cast<std::size_t>(a)); }
[[nodiscard]] inline void* operator new[](std::size_t n, std::align_val_t a, const jw::locked_alloc_tag&) { return jw::allocate_locked(n, static_cast<std::size_t>(a)); }
