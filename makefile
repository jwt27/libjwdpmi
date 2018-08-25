CXXFLAGS += -masm=intel
CXXFLAGS += -std=gnu++17 -fconcepts
CXXFLAGS += -Wall -Wextra
CXXFLAGS += -fasynchronous-unwind-tables
CXXFLAGS += -fnon-call-exceptions
CXXFLAGS += -mcld
CXXFLAGS += -mpreferred-stack-boundary=4

INCLUDE := -Iinclude
LIBS := 

OUTPUT := libjwdpmi.a

SRCDIR := src
OUTDIR := bin
OBJDIR := obj
SRC := $(wildcard $(SRCDIR)/*.cpp)
OBJ := $(SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
DEP := $(SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.d)
ASM := $(SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.asm)
PREPROCESSED := $(SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.ii)

.PHONY: all clean preprocessed asm

all: $(OUTDIR)/$(OUTPUT)

preprocessed: $(PREPROCESSED)

asm: $(ASM)

clean:
	rm -f $(OBJ) $(DEP) $(ASM) $(PREPROCESSED) $(OUTDIR)/$(OUTPUT)

$(OUTDIR): 
	mkdir -p $(OUTDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OUTDIR)/$(OUTPUT): $(OBJ) | $(OUTDIR)
	$(AR) scru $@ $(OBJ) $(LIBS)

$(OBJDIR)/cpu_exception.% : override CXXFLAGS += -mgeneral-regs-only
$(OBJDIR)/fpu.% : override CXXFLAGS += -mgeneral-regs-only
$(OBJDIR)/irq.% : override CXXFLAGS += -mgeneral-regs-only
$(OBJDIR)/debug.% : override CXXFLAGS += -O3

$(OBJDIR)/%.asm: $(SRCDIR)/%.cpp jwdpmi_config.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -S -o $@ $(INCLUDE) -c $< $(PIPECMD)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp jwdpmi_config.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -MD -MP -MF $(@:.o=.d) -o $@ $(INCLUDE) -c $< $(PIPECMD)

$(OBJDIR)/%.ii: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -E -o $@ $(INCLUDE) -c $<

jwdpmi_config.h:
	cp -n jwdpmi_config_default.h jwdpmi_config.h

ifneq ($(MAKECMDGOALS),clean)
  -include $(DEP)
endif
