EE_BIN = OPNPS2LD-JOGO.ELF
EE_OBJS = src/opl.o

EE_INCS += -I$(PS2DEV)/gsKit/include
EE_LDFLAGS += -L$(PS2DEV)/gsKit/lib
EE_LIBS += -lgskit -ldmakit -lpad

all: $(EE_BIN)

release: $(EE_BIN)

clean:
	rm -f $(EE_BIN) opl.elf $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
