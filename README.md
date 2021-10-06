# libjwdpmi
This library aims to be a complete development framework for DPMI (32-bit DOS) applications, written in C++20.  
It's still in the experimental stage. Anything may change at any time.

## Features
Current features include:
* C++ interfaces to access many DPMI services.
* Interrupt handling, including dynamic IRQ assignment, IRQ sharing, and nested interrupts.
* CPU exception handling, also nested and re-entrant.
* Automatic translation of CPU exceptions to C++ language exceptions.
* Cooperative multi-threading and coroutines.
* Event-driven keyboard interface.
* Integrated GDB remote debugging backend.
* Access to PIT, RTC and RDTSC clocks using `std::chrono` interface.
* Yamaha OPL2/OPL3 driver with automatic channel allocation.
* MIDI protocol implementation and MPU-401 driver.
* VESA VBE3 graphics interface.
* Accurate analog game port driver.
* Serial port driver with `std::iostream` interface.

## Installing
* Build and install gcc with `--target=i386-pc-msdosdjgpp`, and install the djgpp standard library.  
An easy to use build script is available here: https://github.com/jwt27/build-gcc

* Set your `PATH` accordingly:  
```sh
$ export PATH=/usr/local/cross/bin:$PATH
```
* Add this repository and [jwutil](https://github.com/jwt27/libjwutil) as submodules in your own project  
```sh
$ git submodule add https://github.com/jwt27/libjwutil.git ./lib/libjwutil
$ git submodule add https://github.com/jwt27/libjwdpmi.git ./lib/libjwdpmi
$ git submodule update --init
```
* In your makefile, export your `AR`, `CXX` and `CXXFLAGS`, and add a rule to build `libjwdpmi`:  
```make
AR := i386-pc-msdosdjgpp-ar
CXX := i386-pc-msdosdjgpp-g++
CXXFLAGS := -std=gnu++20 -masm=intel
CXXFLAGS += -I$(CURDIR)/lib/libjwutil/include
CXXFLAGS += -I$(CURDIR)/lib/libjwdpmi/include

export AR CXX CXXFLAGS
libjwdpmi:
    $(MAKE) -C lib/libjwdpmi/
```
* Link your program with `libjwdpmi.a`, found in the `bin/` directory:  
```make
LDFLAGS += -Llib/libjwdpmi/bin -ljwdpmi
LDFLAGS += -Wl,--script=lib/libjwdpmi/tools/i386go32.x
LDFLAGS += -Wl,@lib/libjwdpmi/tools/ldflags

obj/%.o: src/%.cpp
    $(CXX) $(CXXFLAGS) -o $@ -c $<

bin/program.exe: $(OBJ) libjwdpmi
    $(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDFLAGS)
```

## Using
See the [wiki page](https://github.com/jwt27/libjwdpmi/wiki), where I'm slowly adding documentation.  
Read the header files in `include/`, most functions should be self-explanatory (I hope :)). The `detail` namespaces contain implementation details, you shouldn't need to use anything in it (file a feature request if you do).

## License
It's GPLv3, for now. I may decide to move to a different (less restrictive) license in the future. Therefore I am unable to accept code contributions at this time.
