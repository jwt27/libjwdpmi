# libjwdpmi
This library aims to be a complete development framework for DPMI (32-bit DOS) applications, written in C++20.  
It's still in the experimental stage. Anything may change at any time.

## Features
Current features include:
* C++ interfaces to access many DPMI services.
* Interrupt handling, including dynamic IRQ assignment, IRQ sharing, and nested interrupts.
* CPU exception handling, also nested and re-entrant.
* Cooperative multi-threading and coroutines.
* RS-232 serial communication using `std::iostream`.
* Event-driven keyboard interface.
* Integrated GDB remote debugging backend.
* Access to PIT, RTC and RDTSC clocks using `std::chrono`.
* VESA VBE3 graphics interface.
* Accurate analog game port interface.
* MIDI protocol implementation.

## Installing
* Build and install gcc with `--target=i386-pc-msdosdjgpp`, and install the djgpp standard library.  
An easy to use build script is available here: https://github.com/jwt27/build-gcc

* Set your `PATH` accordingly:  
```sh
$ export PATH=/usr/local/cross/bin:$PATH
```
* Add this repository as a submodule in your own project  
```sh
$ git submodule add https://github.com/jwt27/libjwdpmi.git ./lib/libjwdpmi
$ git submodule update --init
```
* In your makefile, export your `AR`, `CXX` and `CXXFLAGS`, and add a rule to build `libjwdpmi`:  
```make
AR:=i386-pc-msdosdjgpp-ar
CXX:=i386-pc-msdosdjgpp-g++
CXXFLAGS:=-std=gnu++2a -masm=intel

export AR CXX CXXFLAGS
libjwdpmi:
    $(MAKE) -C lib/libjwdpmi/
```
* Add the `include/` directory to your global include path (`-I`) and link your program with `libjwdpmi.a`, found in the `bin/` directory.
```make
obj/%.o: src/%.cpp
    $(CXX) $(CXXFLAGS) -o $@ -Ilib/libjwdpmi/include -c $<

bin/program.exe: $(OBJ) libjwdpmi
    $(CXX) $(CXXFLAGS) -o $@ $(OBJ) -Llib/libjwdpmi/bin -ljwdpmi
```

## Using
See the [wiki page](https://github.com/jwt27/libjwdpmi/wiki), where I'm slowly adding documentation.  
Read the header files in `include/`, most functions should be self-explanatory (I hope :)). The `detail` namespaces contain implementation details, you shouldn't need to use anything in it (file a feature request if you do).

## License
It's GPLv3, for now. I may decide to move to a different (less restrictive) license in the future. Therefore I am unable to accept code contributions at this time.
