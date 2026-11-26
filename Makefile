EE_BIN = OPNPS2LD.ELF
EE_OBJS = src/opl.o
EE_INCS = -I$(PS2SDK)/ports/include -I$(GSKIT)/include
EE_LDFLAGS = -L$(PS2SDK)/ports/lib -L$(GSKIT)/lib
EE_LIBS = -lgskit -ldmakit -lpad -lc -lkernel

all: $(EE_BIN)
	mkdir -p bin
	cp -f $(EE_BIN) bin/OPNPS2LD.ELF

release: all

clean:
	rm -f $(EE_BIN) $(EE_OBJS) bin/OPNPS2LD.ELF

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
