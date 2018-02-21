CXXFLAGS += -masm=intel
CXXFLAGS += -std=gnu++17
CXXFLAGS += -Wall -Wextra
CXXFLAGS += -fasynchronous-unwind-tables
CXXFLAGS += -fnon-call-exceptions
CXXFLAGS += -mcld
CXXFLAGS += -mpreferred-stack-boundary=4

INCLUDE := -I$(CURDIR)/include
LIBS := 

OUTPUT := libjwdpmi.a
DEPFILE := libjwdpmi.d

SRCDIR := src
OUTDIR := bin
OBJDIR := obj
SRC := $(wildcard $(SRCDIR)/*.cpp)
OBJ := $(SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
DEP := $(SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.d)

.PHONY: all clean

all: $(OUTDIR)/$(OUTPUT) $(OUTDIR)/$(DEPFILE)

clean:
	rm -f $(OBJ) $(DEP) $(OUTDIR)/$(OUTPUT) $(OUTDIR)/$(DEPFILE)

$(OUTDIR): 
	mkdir -p $(OUTDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OUTDIR)/$(OUTPUT): $(OBJ) | $(OUTDIR)
	$(AR) scru $@ $(OBJ) $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp jwdpmi_config.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -MD -MP -MF $(@:.o=.d) -o $@ $(INCLUDE) -c $< $(PIPECMD)

$(DEP): $(OBJ)

$(OUTDIR)/$(DEPFILE): $(DEP) | $(OUTDIR)
	echo -include $(foreach D, $(DEP), $(join $(CURDIR)/, $(D))) > $@
	echo $(CURDIR)/$(OUTDIR)/$(OUTPUT): $(foreach O, $(OBJ), $(join $(CURDIR)/, $(O))) >> $@

jwdpmi_config.h:
	cp -n jwdpmi_config_default.h jwdpmi_config.h

ifneq ($(MAKECMDGOALS),clean)
  -include $(DEP)
endif
