EE_BIN = OPNPS2LD.ELF
EE_OBJS = src/opl.o
EE_LIBS = -ldebug -lpad

all: $(EE_BIN)

release: $(EE_BIN) ; cp $(EE_BIN) opl.elf

debug: $(EE_BIN)

clean: ; rm -f $(EE_BIN) opl.elf src/opl.o

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
