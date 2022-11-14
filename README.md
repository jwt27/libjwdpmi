# jwdpmi
This library aims to be a complete development framework for DPMI (32-bit DOS)
applications, written in C++20.  
It's still in the experimental stage.  Anything may change at any time.

## Features
Current features include:
* Idiomatic C++ interfaces to access most DPMI services.
* Interrupt handling, including dynamic IRQ assignment, IRQ sharing, and nested interrupts.
* CPU exception handling, also nested and re-entrant.
* Automatic translation of CPU exceptions to C++ language exceptions.
* Cooperative multi-threading, implementing `std::thread`.
* Event-driven keyboard interface.
* Integrated GDB remote debugging backend.
* Access to PIT, RTC and RDTSC clocks using `std::chrono` interface.
* Yamaha OPL2/OPL3 driver with automatic channel allocation.
* Sound Blaster driver (all models).
* ~~MIDI protocol implementation.~~ (moved to [jwmidi](https://github.com/jwt27/libjwmidi))
* VESA VBE3 graphics interface.
* Accurate analog game port driver.
* Serial port and MPU-401 driver with `std::iostream` interface.

## Installing
First, you need a toolchain compiled with `--target=i386-pc-msdosdjgpp`, and
djgpp's `libc`.  An easy to use build script is available here:  
https://github.com/jwt27/build-gcc

This library is meant to be integrated in your project, not installed system-
wide.  The only dependency is [jwutil](https://github.com/jwt27/libjwutil), and
the most convenient way to install both is as submodules in your own project:  
```sh
$ git submodule add https://github.com/jwt27/libjwutil.git ./lib/libjwutil
$ git submodule add https://github.com/jwt27/libjwdpmi.git ./lib/libjwdpmi
$ git submodule update --init --recursive
```

If you use [jwbuild](https://github.com/jwt27/jwbuild), simply add the
following to your `configure` script, and you're all set:  
```sh
add_submodule lib/libjwutil
add_submodule lib/libjwdpmi --with-jwutil=$(pwd)/lib/libjwutil
```

For other build systems, you need some way to call the `configure` script, and
then the `cxxflags` and `ldflags` files in the build directory contain all
flags you need to compile and link.

## Using
See the [wiki page](https://github.com/jwt27/libjwdpmi/wiki), where I'm slowly
adding documentation.  
Read the header files in `include/`, most functions should be self-explanatory.
The `detail` namespaces contain implementation details, you shouldn't need to
use anything in it (file a feature request if you do).

## License
It's GPLv3, for now. I may decide to move to a different (less restrictive)
license in the future. Therefore I am unable to accept code contributions at
this time.
