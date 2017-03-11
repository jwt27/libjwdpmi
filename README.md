# libjwdpmi
Because I can't come up with any better names.  
This library aims to be a complete development framework for DPMI (32-bit DOS) applications, written in C++14.  
It's still in the experimental stage. Anything may change at any time.

## Features
Current features include:
* C++ interfaces to access many DPMI services.
* Interrupt handling, including dynamic IRQ assignment, IRQ sharing, and nested interrupts.
* CPU exception handling, also nested and re-entrant.
* Cooperative multi-threading and coroutines.
* RS-232 serial communication using `std::iostream`.
* Event-driven keyboard interface

## Installing
* Build and install DJGPP (the DOS port of gcc)  
A build script can be found here: https://github.com/andrewwutw/build-djgpp

* Set your `PATH` and `GCC_EXEC_PREFIX` accordingly:  
```
    $ export PATH=/usr/local/djgpp/i586-pc-msdosdjgpp/bin:$PATH  
    $ export GCC_EXEC_PREFIX=/usr/local/djgpp/lib/gcc/  
```
* Add this repository as a submodule in your own project  
```
    $ git submodule add https://github.com/jwt27/libjwdpmi.git ./lib/libjwdpmi  
    $ git submodule update --init
```
* In your makefile, export your `CXX` and `CXXFLAGS`, and add a rule to build `libjwdpmi`:  
```
    export CXX CXXFLAGS  
    libjwdpmi:  
        $(MAKE) -C lib/libjwdpmi/  
```
* Add the `include/` directory to your global include path (`-I`) and link your program with `libjwdpmi.a`, found in the `bin/` directory.
```
    bin/program.exe: $(OBJ) libjwdpmi
        $(CXX) $(CXXFLAGS) -o $@ $(OBJ) -Llib/libjwdpmi/bin -ljwdpmi

    obj/%.o: src/%.cpp
        $(CXX) $(CXXFLAGS) -o $@ -Ilib/libjwdpmi/include -c $<
``` 

## Using
See the [wiki page](https://github.com/jwt27/libjwdpmi/wiki), where I'm slowly adding documentation.  
Read the header files in `include/`, most functions should be self-explanatory (I hope :)). The `detail` namespaces contain implementation details, you shouldn't need to use anything in it (file a feature request if you do).

## License
It's GPLv3, for now.
