EE_BIN = OPNPS2LD-JOGO.ELF
EE_OBJS = src/opl.o src/audsrv_irx.o
EE_LIBS = -ldebug -lpad -lmc -laudsrv

all: $(EE_BIN)

release: $(EE_BIN) ; cp $(EE_BIN) opl.elf

debug: $(EE_BIN)

src/audsrv_irx.c: ; bin2c $(PS2SDK)/iop/irx/audsrv.irx src/audsrv_irx.c audsrv_irx

clean: ; rm -f $(EE_BIN) opl.elf src/opl.o src/audsrv_irx.c

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
