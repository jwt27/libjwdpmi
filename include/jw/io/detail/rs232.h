#pragma once
#include <iostream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <jw/dpmi/alloc.h>

namespace jw
{
    namespace io
    {
        namespace detail
        {
            struct [[gnu::packed]] rs232_interrupt_enable_reg
            {
                bool data_available : 1;
            bool transmitter_empty : 1;
            bool error : 1;
            bool status_change : 1;
            unsigned : 4;
            };

            class rs232_streambuf : public std::streambuf
            {
            public:
                rs232_streambuf(port_num p, unsigned irq) { /* TODO */ }

            protected:
                //virtual void imbue(const std::locale& loc);

                virtual basic_streambuf<char_type, traits_type>* setbuf(char_type* s, std::streamsize n);
                //virtual pos_type seekoff(off_type off, std::ios_base::seekdir way, std::ios_base::openmode which = std::ios_base::in|std::ios_base::out);
                //virtual pos_type seekpos(pos_type sp, std::ios_base::openmode which = std::ios_base::in|std::ios_base::out);
                virtual int sync() override; // TODO: read all, write all, restore ptrs

                virtual std::streamsize showmanyc() override { return input_buffer.size(); }; // TODO: should return bytes available in uart
                virtual std::streamsize xsgetn(char_type* s, std::streamsize n) override 
                { 
                    std::ptrdiff_t diff_from_begin = gptr() - &*input_buffer.begin();
                    while (input_buffer.size() - diff_from_begin < n) underflow();
                    std::copy_n(gptr(), n, s);
                    input_buffer.erase(input_buffer.begin(), input_buffer.begin() + diff_from_begin + n);
                    update_ptrs();
                    return n;
                };

                virtual int_type underflow() override; // TODO: read one char, dont advance ptr

                virtual int_type pbackfail(int_type c = traits_type::eof());

                virtual std::streamsize xsputn(const char_type* s, std::streamsize n) override
                {
                    std::copy_n(s, n, std::back_inserter(output_buffer));
                    update_ptrs();
                    return n;
                }

                virtual int_type overflow (int_type c = traits_type::eof()) override;

            private:
                void update_ptrs()
                {
                    auto id = input_buffer.data();
                    auto is = input_buffer.size();
                    setg(id, id, id + is);
                    auto od = output_buffer.data();
                    auto os = output_buffer.size();
                    setp(od + os - 1, od + os);
                }

                dpmi::locked_pool_allocator<> alloc { 16_KB };
                std::vector<char_type, dpmi::locked_pool_allocator<>> input_buffer { alloc };
                std::vector<char_type, dpmi::locked_pool_allocator<>> output_buffer { alloc };
                static std::unordered_map<port_num, bool> com_port_use_map;
            };
        }
    }
}
