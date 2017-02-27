CXXFLAGS += -masm=intel
CXXFLAGS += -MD -MP
CXXFLAGS += -std=gnu++14
CXXFLAGS += -Wall -Wextra
CXXFLAGS += -gdwarf-4
CXXFLAGS += -funwind-tables -fasynchronous-unwind-tables
CXXFLAGS += -fnon-call-exceptions 
CXXFLAGS += -mcld
CXXFLAGS += -mpreferred-stack-boundary=4
# CXXFLAGS += -save-temps

INCLUDE := -Iinclude
LIBS := 

OUTPUT := libjwdpmi.a

SRCDIR := src
OUTDIR := bin
OBJDIR := obj
SRC := $(wildcard $(SRCDIR)/*.cpp)
OBJ := $(SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
DEP := $(SRC:$(SRCDIR)/%.cpp=$(OBJDIR)/%.d)
VPATH := .:$(SRCDIR)
OBJ += $(OBJDIR)/gdb_stub.o

.PHONY: all clean vs

all: $(OBJDIR) $(OUTDIR) $(OUTDIR)/$(OUTPUT)

clean:
	-rm -rf obj/* bin/*

$(OUTDIR): 
	-mkdir $(OUTDIR)

$(OBJDIR):
	-mkdir $(OBJDIR)

$(OUTDIR)/$(OUTPUT): $(OBJ)
	ar cru $@ $(OBJ) $(LIBS)
	ranlib $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -o $@ -MF $(@:.o=.d) $(INCLUDE) -c $<

$(OBJDIR)/gdb_stub.o: $(SRCDIR)/gdb_stub.c
	$(CC) $(CXXFLAGS) -masm=att -o $@ -c $<

ifneq ($(MAKECMDGOALS),clean)
  -include $(DEP)
endif
