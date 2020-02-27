CXXFLAGS += -Wall -Wextra
CXXFLAGS += -Wno-address-of-packed-member

CXXFLAGS_NOFPU := -mgeneral-regs-only $(shell tools/missing-macros.sh $(CXX) $(CXXFLAGS))

INCLUDE := -Iinclude
LIBS := 

SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:src/%.cpp=obj/%.o)
DEP := $(OBJ:%.o=%.d)
ASM := $(OBJ:%.o=%.asm)
DWO := $(OBJ:%.o=%.dwo)
PREPROCESSED := $(OBJ:%.o=%.ii)

.PHONY: all clean preprocessed asm

all: bin/libjwdpmi.a

preprocessed: $(PREPROCESSED)

asm: $(ASM)

clean:
	rm -f $(OBJ) $(DEP) $(ASM) $(DWO) $(PREPROCESSED) bin/libjwdpmi.a

bin:
	mkdir -p bin

obj:
	mkdir -p obj

bin/libjwdpmi.a: $(OBJ) | bin
	$(AR) scru $@ $(OBJ) $(LIBS)

obj/cpu_exception.% : override CXXFLAGS += $(CXXFLAGS_NOFPU)
obj/fpu.% : override CXXFLAGS += $(CXXFLAGS_NOFPU)
obj/irq.% : override CXXFLAGS += $(CXXFLAGS_NOFPU)
obj/ring0.% : override CXXFLAGS += $(CXXFLAGS_NOFPU)
obj/memory.% : override CXXFLAGS += $(CXXFLAGS_NOFPU)
obj/debug.% : override CXXFLAGS += -O3

obj/%.asm: src/%.cpp jwdpmi_config.h | obj
	$(CXX) $(CXXFLAGS) -S -o $@ $(INCLUDE) -c $< $(PIPECMD)

obj/%.o: src/%.cpp jwdpmi_config.h | obj
	$(CXX) $(CXXFLAGS) -MD -MP -MF $(@:.o=.d) -o $@ $(INCLUDE) -c $< $(PIPECMD)

obj/%.ii: src/%.cpp | obj
	$(CXX) $(CXXFLAGS) -E -o $@ $(INCLUDE) -c $<

jwdpmi_config.h:
	cp -n jwdpmi_config_default.h jwdpmi_config.h

ifneq ($(MAKECMDGOALS),clean)
  -include $(DEP)
endif
