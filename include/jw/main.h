/* * * * * * * * * * * * * * libjwdpmi * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2020 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2019 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2017 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2016 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <exception>

int main(int, const char**);

namespace jw
{
    struct init;

    void print_exception(const std::exception& e, int level = 0) noexcept;

    struct terminate_exception final
    {
        ~terminate_exception() { if (not defused) std::terminate(); }
        const char* what() const noexcept { return "Terminating."; }
        void defuse() const noexcept { defused = true; }
    private:
        mutable bool defused { false };
    };

    [[noreturn]] inline void terminate() { throw terminate_exception { }; }

    [[noreturn]] inline void halt() { do { asm ("cli; hlt"); } while (true); }

    [[nodiscard]] void* realloc(void* pointer, std::size_t new_size, std::size_t alignment);

    // Allocate from a pre-allocated locked memory pool.  This memory may be
    // deallocated with free().
    [[nodiscard]] void* locked_malloc(std::size_t size, std::size_t alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__);

#   ifdef HAVE__MMX__
    inline constexpr bool mmx = true;
#   else
    inline constexpr bool mmx = false;
#   endif
#   ifdef HAVE__SSE__
    inline constexpr bool sse = true;
#   else
    inline constexpr bool sse = false;
#   endif
}
