#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <debug.h>
#include <libpad.h>
#include <libmc.h>

/* Cores no formato PS2 GS (0x00BBGGRR) */
#define COR_BRANCO   0x00FFFFFF
#define COR_CINZA    0x00999999
#define COR_OURO     0x0033DDFF
#define COR_AMARELO  0x0022EEFF
#define COR_LARANJA  0x002277FF
#define COR_VERMELHO 0x003333FF
#define COR_VERDE    0x0044EE44
#define COR_CIANO    0x00FFCC33
#define COR_ROXO     0x00EE44BB

/* Suporte ao driver de som audsrv */
typedef struct {
    int freq;
    int bits;
    int channels;
} sfx_fmt_t;

extern int audsrv_init(void) __attribute__((weak));
extern int audsrv_set_format(sfx_fmt_t *fmt) __attribute__((weak));
extern void audsrv_set_volume(int vol) __attribute__((weak));
extern int audsrv_play_audio(const char *buf, int len) __attribute__((weak));
extern unsigned char *audsrv_irx __attribute__((weak));
extern unsigned int size_audsrv_irx __attribute__((weak));

static int som_ativo = 0;

static struct {
    u64 blk0, blk1, blk2, blk3, blk4, blk5, blk6, blk7;
    u64 blk8, blk9, blk10, blk11, blk12, blk13, blk14, blk15;
    u64 blk16, blk17, blk18, blk19, blk20, blk21, blk22, blk23;
    u64 blk24, blk25, blk26, blk27, blk28, blk29, blk30, blk31;
} sfxMem __attribute__((aligned(64)));

static struct {
    u64 a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p;
    u64 q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F;
} padBuf __attribute__((aligned(64)));

static struct {
    u64 a,b,c,d,e,f,g,h,i,j,k,l;
} falaMem;

static struct {
    u64 a,b,c,d;
} barraMem;

/* Estrutura de Save no Memory Card */
typedef struct {
    int magic;
    int classe;
    int nivel;
    int almas;
    int custo_nivel;
    int st_vig;
    int st_end;
    int st_str;
    int st_int;
    int arma_atual;
    int armas_mask;
    int arma_nv;
    int escudo_atual;
    int escudos_mask;
    int minerios;
    int almas_chefe_mask;
    int chefes_mortos;
    int subchefes_mask;
    int fogueiras_mask;
    int brasas_entregues;
    int mortes;
    int frascos_max;
    int elixires;
    int resinas;
    int antidotos;
    int aneis_mask;
    int anel_equipado;
    int runas_mask;
    int runa_equipada;
    int quest_mask;
    int baus_mask;
} SaveData;

int abs_val(int v) {
    return (v < 0) ? -v : v;
}

void iniciar_som(void) {
    int ret = 0;
    if (audsrv_init && &audsrv_irx) {
        SifLoadModule("rom0:LIBSD", 0, NULL);
        SifExecModuleBuffer(&audsrv_irx, size_audsrv_irx, 0, NULL, &ret);
        if (audsrv_init() == 0) {
            sfx_fmt_t fmt;
            fmt.freq = 22050;
            fmt.bits = 16;
            fmt.channels = 1;
            if (audsrv_set_format) audsrv_set_format(&fmt);
            if (audsrv_set_volume) audsrv_set_volume(100);
            som_ativo = 1;
        }
    }
}

void tocar_som(int tipo) {
    if (!som_ativo || !audsrv_play_audio) return;
    short *sfx_buf = (short *)&sfxMem;
    int amostras = 128;
    unsigned int seed = 12345 + tipo * 777;
    for (int i = 0; i < amostras; i++) {
        int val = 0;
        if (tipo == 0) {
            seed = seed * 1103515245 + 12345;
            int env = 14000 - (i * 100);
            if (env < 0) env = 0;
            val = ((seed >> 16) & 1) ? env : -env;
        } else if (tipo == 1) {
            int periodo = 12 + (i / 6);
            val = ((i % periodo) * (16000 / periodo)) - 8000;
        } else if (tipo == 2) {
            int periodo = (i < 64) ? 24 : 16;
            val = ((i % periodo) < (periodo / 2)) ? 9000 : -9000;
        } else if (tipo == 3) {
            int periodo = 40 + (i / 4);
            val = ((i % periodo) < (periodo / 2)) ? 14000 : -14000;
        } else if (tipo == 4) {
            int periodo = (i < 64) ? 14 : 9;
            val = ((i % periodo) < (periodo / 2)) ? 15000 : -15000;
        } else {
            int periodo = (i < 42) ? 28 : ((i < 84) ? 21 : 14);
            val = ((i % periodo) < (periodo / 2)) ? 11000 : -11000;
        }
        *(sfx_buf + i) = (short)val;
    }
    audsrv_play_audio((const char *)sfx_buf, amostras * 2);
}

int salvar_jogo(SaveData *sd) {
    int fd = open("mc0:/DARKSTONE_V2.SAV", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return 0;
    sd->magic = 0x53543032;
    write(fd, sd, sizeof(SaveData));
    close(fd);
    return 1;
}

int carregar_jogo(SaveData *sd) {
    int fd = open("mc0:/DARKSTONE_V2.SAV", O_RDONLY);
    if (fd < 0) return 0;
    read(fd, sd, sizeof(SaveData));
    close(fd);
    return (sd->magic == 0x53543032);
}

void esperar_botao(u32 botao) {
    struct padButtonStatus buttons;
    u32 paddata = 0, old_pad = 0, new_pad = 0;
    while (1) {
        if (padGetState(0, 0) == PAD_STATE_STABLE) {
            if (padRead(0, 0, &buttons) != 0) {
                paddata = 0xffff ^ buttons.btns;
                new_pad = paddata & ~old_pad;
                old_pad = paddata;
                if (new_pad & botao) break;
            }
        }
        usleep(40000);
    }
}

void tela_historia_inicio(int tem_save) {
    scr_clear();
    scr_setfontcolor(COR_OURO);
    scr_setXY(10, 2);  scr_printf("=== DARK STONE SOULS: SOUL TRANSPOSITION ===");

    scr_setfontcolor(COR_CIANO);
    scr_setXY(33, 4);  scr_printf("    /\\    ");
    scr_setXY(33, 5);  scr_printf("   /==\\   ");
    scr_setfontcolor(COR_LARANJA);
    scr_setXY(33, 6);  scr_printf("  (*)(*)  ");
    scr_setfontcolor(COR_CIANO);
    scr_setXY(33, 7);  scr_printf("   \\==/   ");
    scr_setXY(33, 8);  scr_printf("    \\/    ");

    scr_setfontcolor(COR_BRANCO);
    scr_setXY(4, 10); scr_printf("30 Salas em 5 Reinos: Sente nas Fogueiras (Triang) para Upar e Viajar!");
    scr_setXY(4, 11); scr_printf("Transmute Almas de Chefes no Ferreiro: Dragon Slayer, Zangetsu e Foice!");
    scr_setXY(4, 12); scr_printf("Entregue Reliquias ao Ferreiro e conquiste Runas nos Lordes!");

    scr_setfontcolor(COR_CIANO);
    scr_setXY(4, 14); scr_printf("(Quad) Golpe | (Triang) Pesado/Fogueira/Ferreiro | (R1) Escudo/Parry");
    scr_setXY(4, 15); scr_printf("(X) Rolar | (Bola) Raio | (L1) Arma | (SELECT) Escudo | (L2/R2) Itens");

    scr_setfontcolor(COR_OURO);
    if (tem_save) {
        scr_setXY(14, 19); scr_printf("SAVE ENCONTRADO NO MEMORY CARD (mc0:)!");
        scr_setXY(14, 21); scr_printf("(X) Continuar Save  |  (Circulo) Novo Jogo");
    } else {
        scr_setXY(21, 21); scr_printf("Pressione (X) para encarar o abismo...");
    }
    scr_setfontcolor(COR_BRANCO);
}

int tela_escolher_classe(void) {
    struct padButtonStatus buttons;
    u32 paddata = 0, old_pad = 0, new_pad = 0;
    int sel = 0;
    scr_clear();

    while (1) {
        if (padGetState(0, 0) == PAD_STATE_STABLE) {
            if (padRead(0, 0, &buttons) != 0) {
                paddata = 0xffff ^ buttons.btns;
                new_pad = paddata & ~old_pad;
                old_pad = paddata;
            }
        }
        if ((new_pad & PAD_UP) && sel > 0) { sel--; tocar_som(0); }
        if ((new_pad & PAD_DOWN) && sel < 2) { sel++; tocar_som(0); }
        if (new_pad & PAD_CROSS) { tocar_som(2); scr_clear(); return sel; }

        scr_setfontcolor(COR_OURO);
        scr_setXY(18, 3); scr_printf("=== ESCOLHA SUA CLASSE (MODO HARDCORE) ===");

        scr_setfontcolor(sel == 0 ? COR_OURO : COR_BRANCO);
        scr_setXY(8, 7);  scr_printf("%s 1. CAVALEIRO DE ASTORA", sel == 0 ? "-->" : "   ");
        scr_setfontcolor(COR_CINZA);
        scr_setXY(12, 8);  scr_printf("VIG:14 END:12 STR:12 INT:8  | Escudo Medio + Espada Longa");

        scr_setfontcolor(sel == 1 ? COR_LARANJA : COR_BRANCO);
        scr_setXY(8, 11); scr_printf("%s 2. PIROMANTE DO PANTANO", sel == 1 ? "-->" : "   ");
        scr_setfontcolor(COR_CINZA);
        scr_setXY(12, 12); scr_printf("VIG:10 END:10 STR:10 INT:16 | +Resinas de Fogo e Alto Dano Magico");

        scr_setfontcolor(sel == 2 ? COR_ROXO : COR_BRANCO);
        scr_setXY(8, 15); scr_printf("%s 3. MERCENARIO DE CARIM", sel == 2 ? "-->" : "   ");
        scr_setfontcolor(COR_CINZA);
        scr_setXY(12, 16); scr_printf("VIG:11 END:15 STR:13 INT:9  | Broquel de Parry + Arco de Caca");

        scr_setfontcolor(COR_OURO);
        scr_setXY(18, 21); scr_printf("(Cima/Baixo) Selecionar  |  (X) Iniciar Jornada");
        scr_setfontcolor(COR_BRANCO);
        usleep(40000);
    }
}

void tela_historia_final(int nivel, int arma_nv, int mortes) {
    tocar_som(5);
    scr_clear();
    scr_setfontcolor(COR_OURO);
    scr_setXY(16, 3);  scr_printf("=== VOCE CONQUISTOU A PEDRA DA ALMA! ===");
    scr_setfontcolor(COR_CIANO);
    scr_setXY(33, 5);  scr_printf("   .|.    ");
    scr_setXY(33, 6);  scr_printf("  -(*)-   ");
    scr_setXY(33, 7);  scr_printf("   '|'    ");

    scr_setfontcolor(COR_BRANCO);
    scr_setXY(6, 10); scr_printf("Todas as 30 Salas foram purificadas e o Lorde das Cinzas caiu!");
    scr_setXY(6, 11); scr_printf("As 5 Grandes Almas foram reunidas na Pedra Ancestral.");
    scr_setfontcolor(COR_VERDE);
    scr_setXY(6, 13); scr_printf("Nivel Final: %d  |  Forja: +%d  |  Total de Quedas: %d", nivel, arma_nv, mortes);
    scr_setfontcolor(COR_OURO);
    scr_setXY(6, 15); scr_printf("Lenda absoluta do PlayStation 2!");

    scr_setXY(20, 20); scr_printf("Pressione (X) para iniciar Nova Jornada+...");
    scr_setfontcolor(COR_BRANCO);
    esperar_botao(PAD_CROSS);
    scr_clear();
}

const char* nome_arma(int id) {
    if (id == 0) return "ESPADA LONGA";
    if (id == 1) return "MARTELO COLOSSAL";
    if (id == 2) return "ARCO DE CACA";
    if (id == 3) return "DRAGON SLAYER";
    if (id == 4) return "ZANGETSU";
    return "FOICE CEIFADOR";
}

const char* nome_escudo(int id) {
    if (id == 0) return "BROQUEL PARRY";
    if (id == 1) return "ESCUDO MEDIO";
    return "ESCUDO GRANDE";
}

const char* nome_item(int id) {
    if (id == 0) return "ESTUS";
    if (id == 1) return "ELIXIR";
    if (id == 2) return "RESINA";
    return "ANTIDOTO";
}

const char* nome_runa(int id) {
    if (id == 1) return "FOGO";
    if (id == 2) return "VENENO";
    if (id == 3) return "GELO";
    if (id == 4) return "VAMP";
    return "NENHUMA";
}

const char* nome_anel(int id) {
    if (id == 1) return "ANEL VITAL";
    if (id == 2) return "ANEL FORCA";
    if (id == 3) return "ANEL CLORANTO";
    if (id == 4) return "ANEL ARCANO";
    return "NENHUM";
}

const char* nome_reino(int id) {
    if (id == 0) return "SANTUARIO DO FOGO";
    if (id == 1) return "1. FLORESTA SOMBRIA";
    if (id == 2) return "2. PANTANO VENENOSO";
    if (id == 3) return "3. CAVERNA DE CRISTAL";
    if (id == 4) return "4. CATACUMBAS ANTIGAS";
    return "5. CASTELO DAS CINZAS";
}

const char* nome_guardiao(int id, int eh_sub) {
    if (eh_sub) {
        if (id == 1) return "LOBO ALFA SOMBRIO";
        if (id == 2) return "HIDRA DO PANTANO";
        if (id == 3) return "GOLEM DE QUARTZO";
        if (id == 4) return "CAVALEIRO NEGRO";
        return "DRAGAO DE CINZAS";
    }
    if (id == 1) return "CAVALEIRO DE FERRO";
    if (id == 2) return "BRUXA DO PANTANO";
    if (id == 3) return "COLOSSO DE CRISTAL";
    if (id == 4) return "CEIFADOR DE ALMAS";
    return "LORDE DAS CINZAS";
}

int main(void) {
    struct padButtonStatus buttons;
    u32 paddata = 0, old_pad = 0, new_pad = 0;
    char *fala = (char *)&falaMem;
    char *barra = (char *)&barraMem;
    SaveData save;

    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    SifLoadModule("rom0:MCMAN", 0, NULL);
    SifLoadModule("rom0:MCSERV", 0, NULL);

    padInit(0);
    padPortOpen(0, 0, &padBuf);
    mcInit(MC_TYPE_MC);
    iniciar_som();

    init_scr();
    scr_setCursor(0);
    scr_clear();

    while (1) {
        int tem_save = carregar_jogo(&save);
        int usar_save = 0;
        tela_historia_inicio(tem_save);

        while (1) {
            if (padGetState(0, 0) == PAD_STATE_STABLE) {
                if (padRead(0, 0, &buttons) != 0) {
                    paddata = 0xffff ^ buttons.btns;
                    new_pad = paddata & ~old_pad;
                    old_pad = paddata;
                    if (new_pad & PAD_CROSS) { usar_save = tem_save; break; }
                    if (tem_save && (new_pad & PAD_CIRCLE)) { usar_save = 0; break; }
                }
            }
            usleep(40000);
        }

        int classe, nivel, almas, custo_nivel;
        int st_vig, st_end, st_str, st_int;
        int arma_atual, armas_mask, arma_nv;
        int escudo_atual, escudos_mask;
        int minerios, almas_chefe_mask, chefes_mortos, subchefes_mask;
        int fogueiras_mask, brasas_entregues, mortes, frascos_max;
        int elixires, resinas, antidotos, anel_equipado, runa_equipada;
        int aneis_mask = 1;
        int runas_mask = 1;
        int quest_mask = 0;
        int baus_mask = 0;

        if (usar_save) {
            classe = save.classe;
            nivel = save.nivel;
            almas = save.almas;
            custo_nivel = save.custo_nivel;
            st_vig = save.st_vig;
            st_end = save.st_end;
            st_str = save.st_str;
            st_int = save.st_int;
            arma_atual = save.arma_atual;
            armas_mask = save.armas_mask | 7;
            arma_nv = save.arma_nv;
            escudo_atual = save.escudo_atual;
            escudos_mask = save.escudos_mask | 3;
            minerios = save.minerios;
            almas_chefe_mask = save.almas_chefe_mask;
            chefes_mortos = save.chefes_mortos;
            subchefes_mask = save.subchefes_mask;
            fogueiras_mask = save.fogueiras_mask | 1;
            brasas_entregues = save.brasas_entregues;
            mortes = save.mortes;
            frascos_max = save.frascos_max;
            elixires = save.elixires;
            resinas = save.resinas;
            antidotos = save.antidotos;
            anel_equipado = save.anel_equipado;
            aneis_mask = save.aneis_mask | 1;
            runa_equipada = save.runa_equipada;
            runas_mask = save.runas_mask | 1;
            quest_mask = save.quest_mask;
            baus_mask = save.baus_mask;
        } else {
            classe = tela_escolher_classe();
            nivel = 1; almas = 0; custo_nivel = 60;
            st_vig = (classe == 0) ? 14 : ((classe == 1) ? 10 : 11);
            st_end = (classe == 2) ? 15 : ((classe == 0) ? 12 : 10);
            st_str = (classe == 2) ? 13 : ((classe == 0) ? 12 : 10);
            st_int = (classe == 1) ? 16 : ((classe == 2) ? 9 : 8);
            arma_atual = (classe == 2) ? 2 : 0;
            armas_mask = 7;
            arma_nv = 0;
            escudo_atual = (classe == 2) ? 0 : 1;
            escudos_mask = 3;
            minerios = 0; almas_chefe_mask = 0; chefes_mortos = 0; subchefes_mask = 0;
            fogueiras_mask = 1;
            brasas_entregues = 0; mortes = 0;
            frascos_max = 4;
            elixires = (classe == 1) ? 3 : 1;
            resinas = (classe == 1) ? 3 : 1;
            antidotos = 2;
            anel_equipado = 0;
            runa_equipada = 0;
        }

        int hp_max = (st_vig * 3) + (anel_equipado == 1 ? 18 : 0);
        int mp_max = st_int * 2;
        int stam_max = 40 + (st_end * 5);
        int hp = hp_max, mp = mp_max, stam = stam_max;
        int frascos = frascos_max;
        int item_atual = 0;

        int jogo_zerado = 0;
        int reiniciar = 0;

        int area = 0;
        int sala = 1;
        int horda_restante = 0;
        int px = 12, py = 15;
        int roll_timer = 0, roll_dir = 1;
        int atk_timer = 0, heavy_atk = 0;
        int parry_timer = 0, defendendo = 0;
        int resina_timer = 0, veneno_timer = 0;
        int raio_timer = 0, raio_x = 0, raio_y = 14;
        int flecha_ativa = 0, fx = 0, fy = 0;
        int getsuga_ativa = 0, gx = 0, gy = 0;
        int hitstop = 0, shake = 0;

        int ex = 58, ey = 15, ehp = 0, ehp_max = 15;
        int e_postura = 0, e_postura_max = 14;
        int e_sangramento = 0;
        int e_stun = 0;
        int e_burn = 0, e_chill = 0, e_poison = 0;
        int e_arquetipo = 0;
        int e_ai_estado = 0;
        int e_ai_timer = 0;
        int e_proj_ativo = 0, epx = 0, epy = 15, e_proj_tipo = 0;

        int tipo_chefe = 0;
        int eh_subchefe = 0;
        int chefe_fase = 1;
        int onda_ativa = 0, onda_x = 0, onda_y = 15;
        int raio_chefe_timer = 0, rc_x = 0, rc_y = 15;
        int frame = 0;

        sprintf(fala, usar_save ? "Save carregado! Sente na Fogueira (Triang) para Upar ou Viajar." :
                                  "Santuario: NPCs (esq), Fogueira (centro), Ferreiro (dir) com (Triang).");

        while (!jogo_zerado && !reiniciar) {
            frame++;
            hp_max = (st_vig * 3) + (anel_equipado == 1 ? 18 : 0);
            mp_max = st_int * 2;
            stam_max = 40 + (st_end * 5);
            int atk_base = st_str / 2;
            int mag_bonus = st_int / 3;

            if (hp > hp_max) hp = hp_max;
            if (mp > mp_max) mp = mp_max;

            if (hitstop > 0) {
                hitstop--;
                usleep(25000);
                continue;
            }

            if (padGetState(0, 0) == PAD_STATE_STABLE) {
                if (padRead(0, 0, &buttons) != 0) {
                    paddata = 0xffff ^ buttons.btns;
                    new_pad = paddata & ~old_pad;
                    old_pad = paddata;
                }
            }

            if (veneno_timer > 0) {
                veneno_timer--;
                if (veneno_timer % 30 == 0 && hp > 1) hp--;
            }
            if (resina_timer > 0) resina_timer--;

            if (roll_timer == 0 && atk_timer == 0 && !defendendo && stam < stam_max) {
                stam += (anel_equipado == 3) ? 6 : 3;
                if (stam > stam_max) stam = stam_max;
            }

            /* === MENU DE PAUSA (START) === */
            if (new_pad & PAD_START) {
                int cursor = 0;
                int pausado = 1;
                old_pad = paddata;
                while (pausado) {
                    if (padGetState(0, 0) == PAD_STATE_STABLE) {
                        if (padRead(0, 0, &buttons) != 0) {
                            paddata = 0xffff ^ buttons.btns;
                            new_pad = paddata & ~old_pad;
                            old_pad = paddata;
                        }
                    }

                    if ((new_pad & PAD_UP) && cursor > 0) { cursor--; tocar_som(0); }
                    if ((new_pad & PAD_DOWN) && cursor < 5) { cursor++; tocar_som(0); }
                    if (new_pad & PAD_START) pausado = 0;

                    if (new_pad & PAD_CROSS) {
                        tocar_som(2);
                        if (cursor == 0) pausado = 0;
                        else if (cursor == 1) {
                            for (int k = 1; k <= 6; k++) {
                                int cand = (arma_atual + k) % 6;
                                if (armas_mask & (1 << cand)) { arma_atual = cand; break; }
                            }
                        } else if (cursor == 2) {
                            for (int k = 1; k <= 3; k++) {
                                int cand = (escudo_atual + k) % 3;
                                if (escudos_mask & (1 << cand)) { escudo_atual = cand; break; }
                            }
                        } else if (cursor == 3) {
                            for (int k = 1; k <= 5; k++) {
                                int cand = (runa_equipada + k) % 5;
                                if (runas_mask & (1 << cand)) { runa_equipada = cand; break; }
                            }
                        } else if (cursor == 4) {
                            for (int k = 1; k <= 5; k++) {
                                int cand = (anel_equipado + k) % 5;
                                if (aneis_mask & (1 << cand)) { anel_equipado = cand; break; }
                            }
} else if (cursor == 5) {
reiniciar = 1;
pausado = 0;
}
}
scr_setfontcolor(COR_OURO);
scr_setXY(11, 2);  scr_printf("+========================================================+");
scr_setXY(11, 3);  scr_printf("|             EQUIPAMENTO E STATUS (HARDCORE)            |");
scr_setXY(11, 4);  scr_printf("+--------------------------------------------------------+");
scr_setfontcolor(COR_BRANCO);
scr_setXY(11, 5);  scr_printf("| Nv:%-2d Almas:%-5d | VIG:%-2d END:%-2d STR:%-2d INT:%-2d        |",
nivel, almas, st_vig, st_end, st_str, st_int);
scr_setXY(11, 6);  scr_printf("| HP:%-3d/%-3d MP:%-2d/%-2d | Forja:+%-2d Minerios:%-2d Quedas:%-3d |",
hp, hp_max, mp, mp_max, arma_nv, minerios, mortes);
scr_setfontcolor(COR_OURO);
scr_setXY(11, 7);  scr_printf("+--------------------------------------------------------+");
scr_setfontcolor(cursor == 0 ? COR_OURO : COR_BRANCO);
scr_setXY(11, 8);  scr_printf("| %s Continuar Jornada                                  |", cursor == 0 ? "-->" : "   ");
scr_setfontcolor(cursor == 1 ? COR_CIANO : COR_BRANCO);
scr_setXY(11, 9);  scr_printf("| %s Arma: %-18s (X troca)                |", cursor == 1 ? "-->" : "   ", nome_arma(arma_atual));
scr_setfontcolor(cursor == 2 ? COR_VERDE : COR_BRANCO);
scr_setXY(11, 10); scr_printf("| %s Escudo: %-16s (X troca)                |", cursor == 2 ? "-->" : "   ", nome_escudo(escudo_atual));
scr_setfontcolor(cursor == 3 ? COR_LARANJA : COR_BRANCO);
scr_setXY(11, 11); scr_printf("| %s Pedra Runica: %-10s (X equipa)               |", cursor == 3 ? "-->" : "   ", nome_runa(runa_equipada));
scr_setfontcolor(cursor == 4 ? COR_ROXO : COR_BRANCO);
scr_setXY(11, 12); scr_printf("| %s Anel: %-16s (X equipa)                 |", cursor == 4 ? "-->" : "   ", nome_anel(anel_equipado));
scr_setfontcolor(cursor == 5 ? COR_VERMELHO : COR_BRANCO);
scr_setXY(11, 13); scr_printf("| %s Voltar ao Titulo                                   |", cursor == 5 ? "-->" : "   ");
scr_setfontcolor(COR_OURO);
scr_setXY(11, 14); scr_printf("+--------------------------------------------------------+");
scr_setXY(11, 15); scr_printf("| (Cima/Baixo) Mover | (X) Equipar | START Voltar        |");
scr_setXY(11, 16); scr_printf("+========================================================+");
scr_setfontcolor(COR_BRANCO);
usleep(40000);
}
scr_clear();
continue;
}
/* Trocar Arma (L1) */
if (new_pad & PAD_L1) {
for (int k = 1; k <= 6; k++) {
int cand = (arma_atual + k) % 6;
if (armas_mask & (1 << cand)) { arma_atual = cand; break; }
}
tocar_som(0);
sprintf(fala, "Arma equipada: %s +%d", nome_arma(arma_atual), arma_nv);
}
/* Trocar Escudo (SELECT) */
if (new_pad & PAD_SELECT) {
for (int k = 1; k <= 3; k++) {
int cand = (escudo_atual + k) % 3;
if (escudos_mask & (1 << cand)) { escudo_atual = cand; break; }
}
tocar_som(0);
sprintf(fala, "Escudo equipado: %s", nome_escudo(escudo_atual));
}
/* Trocar Consumivel (L2) */
if (new_pad & PAD_L2) {
item_atual = (item_atual + 1) % 4;
tocar_som(0);
sprintf(fala, "Item rapido: %s (Use com R2)", nome_item(item_atual));
}
/* Usar Consumivel (R2) */
if (new_pad & PAD_R2) {
if (item_atual == 0) {
if (frascos > 0 && hp < hp_max) {
frascos--;
hp += 16 + (st_vig);
if (hp > hp_max) hp = hp_max;
tocar_som(2);
sprintf(fala, "Estus bebido! HP restaurado (%d/%d frascos).", frascos, frascos_max);
} else sprintf(fala, "Sem Estus ou HP ja esta cheio!");
} else if (item_atual == 1) {
if (elixires > 0 && mp < mp_max) {
elixires--;
mp += 16;
if (mp > mp_max) mp = mp_max;
tocar_som(2);
sprintf(fala, "Elixir bebido! +16 MP (Restam %d).", elixires);
} else sprintf(fala, "Sem Elixires ou MP cheio!");
} else if (item_atual == 2) {
if (resinas > 0) {
resinas--;
resina_timer = 200;
tocar_som(1);
sprintf(fala, "RESINA DE FOGO aplicada na arma!");
} else sprintf(fala, "Sem Resinas de Fogo!");
} else {
if (antidotos > 0 && veneno_timer > 0) {
antidotos--;
veneno_timer = 0;
tocar_som(2);
sprintf(fala, "Antidoto usado! Veneno curado.");
} else sprintf(fala, "Voce nao esta envenenado ou esta sem Antidotos.");
}
}
/* Defesa e Parry (R1) */
if (new_pad & PAD_R1) {
if (escudo_atual != 2 && stam >= 10) {
parry_timer = (escudo_atual == 0) ? 8 : 5;
stam -= 10;
tocar_som(0);
}
}
defendendo = ((paddata & PAD_R1) && stam > 5 && roll_timer == 0 && atk_timer == 0);
/* Rolamento (X) */
if ((new_pad & PAD_CROSS) && roll_timer == 0) {
if (stam >= 20) {
stam -= 20;
roll_timer = 6;
roll_dir = (paddata & PAD_LEFT) ? -1 : 1;
tocar_som(0);
} else {
sprintf(fala, "Sem Vigor para rolar!");
}
}
/* Movimentacao 2.5D */
if (roll_timer > 0) {
px += (roll_dir * 2);
if (px < 2) px = 2;
if (px > 71) px = 71;
} else if (atk_timer == 0) {
int vel = defendendo ? 0 : 1;
if ((paddata & PAD_LEFT) && px > 2) px -= vel;
if ((paddata & PAD_RIGHT) && px < 71) px += vel;
if ((paddata & PAD_UP) && py > 11) py -= vel;
if ((paddata & PAD_DOWN) && py < 17) py += vel;
}
/* === PROGRESSAO DE 30 SALAS (5 REINOS x 6 SALAS) === */
if (area == 0 && px >= 70) {
area = 1; sala = 1; px = 4;
horda_restante = 3;
tipo_chefe = 0; eh_subchefe = 0; e_arquetipo = 0;
ex = 58; ey = 14; ehp_max = 18 + (nivel * 3); ehp = ehp_max;
e_stun = 0; e_burn = 0; e_chill = 0; e_poison = 0; e_sangramento = 0;
e_ai_estado = 0; e_ai_timer = 0; e_proj_ativo = 0;
sprintf(fala, "Reino 1 - Sala 1/6: Acenda a Fogueira do Reino com (Triang)!");
} else if (area > 0 && px <= 2) {
e_proj_ativo = 0; onda_ativa = 0; raio_chefe_timer = 0; getsuga_ativa = 0;
if (sala > 1) {
sala--; px = 66; horda_restante = 0; ehp = 0; tipo_chefe = 0; eh_subchefe = 0;
sprintf(fala, "Recuou para a Sala %d/6 do Reino %d.", sala, area);
} else {
area--; px = 66;
if (area == 0) {
sala = 1; horda_restante = 0; ehp = 0; tipo_chefe = 0;
sprintf(fala, "Voltou ao Santuario do Fogo.");
} else {
sala = 6; horda_restante = 0; ehp = 0; tipo_chefe = 0;
sprintf(fala, "Voltou para o Reino %d - Sala 6/6.", area);
}
}
} else if (area > 0 && px >= 70) {
if (horda_restante > 0 || ehp > 0) {
px = 69;
sprintf(fala, "Nevoa selada! Derrote todos os inimigos desta sala primeiro!");
} else if (sala < 6) {
sala++; px = 4;
e_stun = 0; e_burn = 0; e_chill = 0; e_poison = 0; e_sangramento = 0;
e_ai_estado = 0; e_ai_timer = 0; e_proj_ativo = 0;
if (sala == 3) {
horda_restante = 0;
if (!(subchefes_mask & (1 << area))) {
tipo_chefe = area; eh_subchefe = 1; chefe_fase = 1;
ex = 58; ey = 14;
ehp_max = 75 + (area * 42); ehp = ehp_max;
sprintf(fala, "SUB-CHEFE: %s guarda a passagem do Reino %d!", nome_guardiao(area, 1), area);
} else {
tipo_chefe = 0; eh_subchefe = 0; ehp = 0;
sprintf(fala, "Sala 3/6 (Sub-Chefe ja derrotado). Siga em frente!");
}
} else if (sala == 6) {
horda_restante = 0;
if (chefes_mortos < area) {
tipo_chefe = area; eh_subchefe = 0; chefe_fase = 1;
ex = 58; ey = 14;
ehp_max = 80 + (area * 88); ehp = ehp_max;
sprintf(fala, "LORDE GUARDIAO: %s despertou! (3 FASES DE COMBATE)", nome_guardiao(area, 0));
} else {
tipo_chefe = 0; eh_subchefe = 0; ehp = 0;
sprintf(fala, "Santuario do Lorde purificado. Avance para o proximo Reino!");
}
} else {
horda_restante = (sala == 2) ? 3 : ((sala == 4) ? 4 : 5);
tipo_chefe = 0; eh_subchefe = 0;
e_arquetipo = (area + sala) % 5;
ex = 58; ey = 14;
ehp_max = 16 + (area * 8) + (sala * 2) + (nivel * 3);
ehp = ehp_max;
sprintf(fala, "Reino %d - Sala %d/6: Horda de %d inimigos! %s",
area, sala, horda_restante, (sala == 4) ? "(Bau de Reliquia nesta sala!)" : "");
}
} else if (sala == 6 && area < 5) {
area++; sala = 1; px = 4;
horda_restante = 3;
tipo_chefe = 0; eh_subchefe = 0;
e_arquetipo = area % 5;
ex = 58; ey = 14; ehp_max = 18 + (area * 8) + (nivel * 3); ehp = ehp_max;
e_stun = 0; e_ai_estado = 0; e_ai_timer = 0; e_proj_ativo = 0;
sprintf(fala, "Entrou no Reino %d/5 - Sala 1/6! Acenda a Fogueira local!", area);
}
}
/* Veneno ambiental no Reino 2 */
if (area == 2 && frame % 85 == 0 && roll_timer == 0 && veneno_timer == 0) {
veneno_timer = 150;
sprintf(fala, "Gases toxicos do Pantano te envenenaram! Use Antidoto!");
}
/* Ataque Normal (Quadrado) */
if ((new_pad & PAD_SQUARE) && atk_timer == 0 && roll_timer == 0) {
int custo_stam = (arma_atual == 1 || arma_atual == 3) ? 28 : ((arma_atual == 4 || arma_atual == 5) ? 16 : 18);
if (stam >= custo_stam) {
stam -= custo_stam;
heavy_atk = 0;
tocar_som(0);
if (arma_atual == 1 || arma_atual == 3) atk_timer = 7;
else if (arma_atual == 2) {
atk_timer = 4;
if (!flecha_ativa) { flecha_ativa = 1; fx = px + 6; fy = py; }
} else atk_timer = 4;
} else sprintf(fala, "Vigor insuficiente!");
}
/* === BOTAO TRIANGULO: FOGUEIRA, FERREIRO, NPCS, BAUS OU GOLPE PESADO === */
if (new_pad & PAD_TRIANGLE) {
int abriu_menu = 0;
/* 1. SENTAR NA FOGUEIRA */
if ((area == 0 && px >= 18 && px <= 31) || (area > 0 && sala == 1 && px >= 12 && px <= 24)) {
abriu_menu = 1;
fogueiras_mask |= (1 << area);
hp = hp_max; mp = mp_max; stam = stam_max; frascos = frascos_max; veneno_timer = 0;
save.classe = classe; save.nivel = nivel; save.almas = almas; save.custo_nivel = custo_nivel;
save.st_vig = st_vig; save.st_end = st_end; save.st_str = st_str; save.st_int = st_int;
save.arma_atual = arma_atual; save.armas_mask = armas_mask; save.arma_nv = arma_nv;
save.escudo_atual = escudo_atual; save.escudos_mask = escudos_mask;
save.minerios = minerios; save.almas_chefe_mask = almas_chefe_mask;
save.chefes_mortos = chefes_mortos; save.subchefes_mask = subchefes_mask;
save.fogueiras_mask = fogueiras_mask; save.brasas_entregues = brasas_entregues;
save.mortes = mortes; save.frascos_max = frascos_max; save.elixires = elixires;
save.resinas = resinas; save.antidotos = antidotos; save.anel_equipado = anel_equipado;
save.aneis_mask = aneis_mask; save.runa_equipada = runa_equipada;
save.runas_mask = runas_mask; save.quest_mask = quest_mask; save.baus_mask = baus_mask;
int salvou = salvar_jogo(&save);
tocar_som(2);
int fog_cursor = 0;
int modo_viagem = 0;
int na_fogueira = 1;
old_pad = paddata;
scr_clear();
while (na_fogueira) {
hp_max = (st_vig * 3) + (anel_equipado == 1 ? 18 : 0);
mp_max = st_int * 2;
stam_max = 40 + (st_end * 5);
hp = hp_max; mp = mp_max; stam = stam_max;
if (padGetState(0, 0) == PAD_STATE_STABLE) {
if (padRead(0, 0, &buttons) != 0) {
paddata = 0xffff ^ buttons.btns;
new_pad = paddata & ~old_pad;
old_pad = paddata;
}
}
int max_op = modo_viagem ? 6 : 5;
if ((new_pad & PAD_UP) && fog_cursor > 0) { fog_cursor--; tocar_som(0); }
if ((new_pad & PAD_DOWN) && fog_cursor < max_op) { fog_cursor++; tocar_som(0); }
if (new_pad & PAD_TRIANGLE) na_fogueira = 0;
if (new_pad & PAD_CROSS) {
if (!modo_viagem) {
if (fog_cursor <= 3 && almas >= custo_nivel) {
almas -= custo_nivel;
nivel++;
custo_nivel += 35;
if (fog_cursor == 0) st_vig++;
else if (fog_cursor == 1) st_end++;
else if (fog_cursor == 2) st_str++;
else st_int++;
tocar_som(2);
} else if (fog_cursor == 4) {
modo_viagem = 1; fog_cursor = 0; tocar_som(1); scr_clear();
} else if (fog_cursor == 5) {
na_fogueira = 0;
} else {
tocar_som(3);
}
} else {
if (fog_cursor == 6) {
modo_viagem = 0; fog_cursor = 0; tocar_som(0); scr_clear();
} else if (fogueiras_mask & (1 << fog_cursor)) {
area = fog_cursor; sala = 1; px = 16; py = 15;
horda_restante = 0; ehp = 0; tipo_chefe = 0; eh_subchefe = 0;
na_fogueira = 0; tocar_som(1);
} else {
tocar_som(3);
}
}
}
scr_setfontcolor(COR_LARANJA);
scr_setXY(10, 2);  scr_printf("+==========================================================+");
scr_setXY(10, 3);  scr_printf("|             FOGUEIRA ANCESTRAL (DARK STONE)              |");
scr_setXY(10, 4);  scr_printf("+----------------------------------------------------------+");
scr_setfontcolor(COR_VERDE);
scr_setXY(10, 5);  scr_printf("| %-56s |", salvou ? "Progresso salvo no Memory Card (mc0:)!" : "Descansando (Insira Memory Card para salvar)");
scr_setfontcolor(COR_OURO);
scr_setXY(10, 6);  scr_printf("| Nivel: %-2d  |  Almas: %-6d  |  Custo Proximo Nv: %-5d  |", nivel, almas, custo_nivel);
scr_setXY(10, 7);  scr_printf("+----------------------------------------------------------+");
if (!modo_viagem) {
scr_setfontcolor(fog_cursor == 0 ? COR_OURO : COR_BRANCO);
scr_setXY(10, 8);  scr_printf("| %s Upar VITALIDADE (VIG:%-2d -> HP:%-3d)                    |", fog_cursor == 0 ? "-->" : "   ", st_vig, hp_max);
scr_setfontcolor(fog_cursor == 1 ? COR_VERDE : COR_BRANCO);
scr_setXY(10, 9);  scr_printf("| %s Upar FORTITUDE  (END:%-2d -> Vigor:%-3d)                 |", fog_cursor == 1 ? "-->" : "   ", st_end, stam_max);
scr_setfontcolor(fog_cursor == 2 ? COR_VERMELHO : COR_BRANCO);
scr_setXY(10, 10); scr_printf("| %s Upar FORCA      (STR:%-2d -> Dano:+%-2d)                  |", fog_cursor == 2 ? "-->" : "   ", st_str, st_str / 2);
scr_setfontcolor(fog_cursor == 3 ? COR_CIANO : COR_BRANCO);
scr_setXY(10, 11); scr_printf("| %s Upar INTELIG.   (INT:%-2d -> MP:%-2d)                     |", fog_cursor == 3 ? "-->" : "   ", st_int, mp_max);
scr_setfontcolor(fog_cursor == 4 ? COR_ROXO : COR_BRANCO);
scr_setXY(10, 12); scr_printf("| %s VIAJAR ENTRE FOGUEIRAS (Teleporte)                    |", fog_cursor == 4 ? "-->" : "   ");
scr_setfontcolor(fog_cursor == 5 ? COR_LARANJA : COR_BRANCO);
scr_setXY(10, 13); scr_printf("| %s Levantar da Fogueira                                  |", fog_cursor == 5 ? "-->" : "   ");
scr_setXY(10, 14); scr_printf("|                                                          |");
} else {
for (int r = 0; r < 6; r++) {
int acesa = (fogueiras_mask & (1 << r));
scr_setfontcolor(fog_cursor == r ? COR_OURO : (acesa ? COR_CIANO : COR_CINZA));
scr_setXY(10, 8 + r);
scr_printf("| %s %-24s %-27s |", fog_cursor == r ? "-->" : "   ", nome_reino(r), acesa ? "(ACESA)" : "(NAO ACESA)");
}
scr_setfontcolor(fog_cursor == 6 ? COR_LARANJA : COR_BRANCO);
scr_setXY(10, 14); scr_printf("| %s Voltar ao Menu da Fogueira                            |", fog_cursor == 6 ? "-->" : "   ");
}
scr_setfontcolor(COR_OURO);
scr_setXY(10, 15); scr_printf("+----------------------------------------------------------+");
scr_setXY(10, 16); scr_printf("| (Cima/Baixo) Escolher | (X) Confirmar | (Triang) Sair    |");
scr_setXY(10, 17); scr_printf("+==========================================================+");
scr_setfontcolor(COR_BRANCO);
usleep(40000);
}
scr_clear();
sprintf(fala, "Voce se levantou da Fogueira em %s.", nome_reino(area));
}
/* 2. MENU DO FERREIRO */
if (!abriu_menu && area == 0 && px >= 42 && px <= 57) {
abriu_menu = 1;
int fer_cursor = 0;
int no_ferreiro = 1;
old_pad = paddata;
tocar_som(0);
scr_clear();
while (no_ferreiro) {
if (padGetState(0, 0) == PAD_STATE_STABLE) {
if (padRead(0, 0, &buttons) != 0) {
paddata = 0xffff ^ buttons.btns;
new_pad = paddata & ~old_pad;
old_pad = paddata;
}
}
if ((new_pad & PAD_UP) && fer_cursor > 0) { fer_cursor--; tocar_som(0); }
if ((new_pad & PAD_DOWN) && fer_cursor < 5) { fer_cursor++; tocar_som(0); }
if (new_pad & PAD_TRIANGLE) no_ferreiro = 0;
int lim_forja = 5 + (brasas_entregues * 5);
if (lim_forja > 15) lim_forja = 15;
int custo_min = 2 + arma_nv;
if (new_pad & PAD_CROSS) {
if (fer_cursor == 0) {
if (arma_nv < lim_forja && minerios >= custo_min) {
minerios -= custo_min; arma_nv++; tocar_som(2);
} else tocar_som(3);
} else if (fer_cursor == 1) {
if ((almas_chefe_mask & (1 << 1)) && !(armas_mask & (1 << 3))) {
armas_mask |= (1 << 3); arma_atual = 3; tocar_som(5);
} else tocar_som(3);
} else if (fer_cursor == 2) {
if ((almas_chefe_mask & (1 << 2)) && !(armas_mask & (1 << 4))) {
armas_mask |= (1 << 4); arma_atual = 4; tocar_som(5);
} else tocar_som(3);
} else if (fer_cursor == 3) {
if ((almas_chefe_mask & (1 << 4)) && !(armas_mask & (1 << 5))) {
armas_mask |= (1 << 5); arma_atual = 5; tocar_som(5);
} else tocar_som(3);
} else if (fer_cursor == 4) {
int disp = 0;
if ((baus_mask & (1 << 1)) && brasas_entregues == 0) disp = 1;
if ((baus_mask & (1 << 3)) && brasas_entregues == 1) disp = 1;
if (disp) {
brasas_entregues++; tocar_som(5);
} else tocar_som(3);
} else {
no_ferreiro = 0;
}
}
int tem_a1 = (almas_chefe_mask & (1 << 1));
int tem_a2 = (almas_chefe_mask & (1 << 2));
int tem_a4 = (almas_chefe_mask & (1 << 4));
int pode_brasa = ((baus_mask & (1 << 1)) && brasas_entregues == 0) ||
((baus_mask & (1 << 3)) && brasas_entregues == 1);
scr_setfontcolor(COR_OURO);
scr_setXY(8, 2);  scr_printf("+==============================================================+");
scr_setXY(8, 3);  scr_printf("|          FORJA DE ANDRE: TRANSPOSICAO DE ALMAS               |");
scr_setXY(8, 4);  scr_printf("+--------------------------------------------------------------+");
scr_setfontcolor(COR_BRANCO);
scr_setXY(8, 5);  scr_printf("| Forja Atual: +%-2d (Limite: +%-2d)  |  Minerios: %-2d             |", arma_nv, lim_forja, minerios);
scr_setfontcolor(COR_OURO);
scr_setXY(8, 6);  scr_printf("+--------------------------------------------------------------+");
scr_setfontcolor(fer_cursor == 0 ? COR_OURO : COR_BRANCO);
scr_setXY(8, 7);  scr_printf("| %s Reforjar Armas (+%d -> +%d) [Custo: %d Minerios]           |",
fer_cursor == 0 ? "-->" : "   ", arma_nv, arma_nv + 1, custo_min);
scr_setfontcolor(fer_cursor == 1 ? COR_CIANO : (tem_a1 ? COR_BRANCO : COR_CINZA));
scr_setXY(8, 8);  scr_printf("| %s Forjar DRAGON SLAYER (Alma Cavaleiro Ferro) %-11s |",
fer_cursor == 1 ? "-->" : "   ", (armas_mask & (1 << 3)) ? "(FORJADA)" : (tem_a1 ? "(PRONTA)" : "(BLOQ)"));
scr_setfontcolor(fer_cursor == 2 ? COR_CIANO : (tem_a2 ? COR_BRANCO : COR_CINZA));
scr_setXY(8, 9);  scr_printf("| %s Forjar ZANGETSU      (Alma Bruxa Pantano)   %-11s |",
fer_cursor == 2 ? "-->" : "   ", (armas_mask & (1 << 4)) ? "(FORJADA)" : (tem_a2 ? "(PRONTA)" : "(BLOQ)"));
scr_setfontcolor(fer_cursor == 3 ? COR_ROXO : (tem_a4 ? COR_BRANCO : COR_CINZA));
scr_setXY(8, 10); scr_printf("| %s Forjar FOICE CEIFADOR(Alma Ceifador Almas)  %-11s |",
fer_cursor == 3 ? "-->" : "   ", (armas_mask & (1 << 5)) ? "(FORJADA)" : (tem_a4 ? "(PRONTA)" : "(BLOQ)"));
scr_setfontcolor(fer_cursor == 4 ? COR_LARANJA : (pode_brasa ? COR_VERDE : COR_CINZA));
scr_setXY(8, 11); scr_printf("| %s ENTREGAR RELIQUIA/COELHO DE FORJA (%d/2)     %-11s |",
fer_cursor == 4 ? "-->" : "   ", brasas_entregues, pode_brasa ? "(ENTREGAR)" : "(VAZIO)");
scr_setfontcolor(fer_cursor == 5 ? COR_VERMELHO : COR_BRANCO);
scr_setXY(8, 12); scr_printf("| %s Sair da Forja                                             |", fer_cursor == 5 ? "-->" : "   ");
scr_setfontcolor(COR_OURO);
scr_setXY(8, 13); scr_printf("+--------------------------------------------------------------+");
scr_setXY(8, 14); scr_printf("| (Cima/Baixo) Selecionar | (X) Forjar/Entregar | (Triang) Sair|");
scr_setXY(8, 15); scr_printf("+==============================================================+");
scr_setfontcolor(COR_BRANCO);
usleep(40000);
}
scr_clear();
sprintf(fala, "Ferreiro: Que as chamas guiem sua lamina (+%d).", arma_nv);
}
/* 3. NPCS DE QUESTS E BAU DO SANTUARIO */
if (!abriu_menu && area == 0) {
int q0 = (quest_mask >> 0) & 3;
int q1 = (quest_mask >> 2) & 3;
int q2 = (quest_mask >> 4) & 3;
int q3 = (quest_mask >> 6) & 3;
if (px >= 4 && px <= 16) {
if (q0 == 0) {
quest_mask = (quest_mask & ~3) | 1;
sprintf(fala, "QUEST 1: Ache o Coelho de Cinzas na Sala 4 da Floresta!");
} else if (q0 == 1 && (baus_mask & (1 << 1))) {
quest_mask = (quest_mask & ~3) | 2;
aneis_mask |= (1 << 2); anel_equipado = 2; tocar_som(5);
sprintf(fala, "QUEST 1 CONCLUIDA! Ganhou o ANEL DA FORCA (+Dano Fisico)!");
} else if (q1 == 0 && chefes_mortos >= 1) {
quest_mask = (quest_mask & ~(3 << 2)) | (1 << 2);
sprintf(fala, "QUEST 2: Ache a Erva Rara na Sala 4 do Pantano (Reino 2)!");
} else if (q1 == 1 && (baus_mask & (1 << 2))) {
quest_mask = (quest_mask & ~(3 << 2)) | (2 << 2);
frascos_max = 6; frascos = 6; tocar_som(5);
sprintf(fala, "QUEST 2 CONCLUIDA! Seus Frascos de Estus subiram para 6!");
} else if (q2 == 0 && chefes_mortos >= 2) {
quest_mask = (quest_mask & ~(3 << 4)) | (1 << 4);
sprintf(fala, "QUEST 3: Pegue o Cristal Solar na Sala 4 da Caverna (Reino 3)!");
} else if (q2 == 1 && (baus_mask & (1 << 3))) {
quest_mask = (quest_mask & ~(3 << 4)) | (2 << 4);
aneis_mask |= (1 << 3); anel_equipado = 3; tocar_som(5);
sprintf(fala, "QUEST 3 CONCLUIDA! Ganhou o ANEL CLORANTO (Vigor x2)!");
} else if (q3 == 0 && chefes_mortos >= 3) {
quest_mask = (quest_mask & ~(3 << 6)) | (1 << 6);
sprintf(fala, "QUEST 4: Pegue o Tomo Negro na Sala 4 das Catacumbas (Reino 4)!");
} else if (q3 == 1 && (baus_mask & (1 << 4))) {
quest_mask = (quest_mask & ~(3 << 6)) | (2 << 6);
aneis_mask |= (1 << 4); anel_equipado = 4; tocar_som(5);
sprintf(fala, "QUEST 4 CONCLUIDA! Ganhou o ANEL ARCANO (+Dano Magico)!");
} else {
sprintf(fala, "NPCs: Explore a Sala 4 de cada Reino e traga as reliquias!");
}
tocar_som(2);
} else if (!(baus_mask & 1) && px >= 59 && px <= 66) {
baus_mask |= 1; aneis_mask |= (1 << 1); anel_equipado = 1; tocar_som(2);
sprintf(fala, "BAU ABERTO! Encontrou o ANEL VITAL (+18 HP Maximo)!");
}
}
/* 4. ABRIR BAU DA SALA 4 OU DESFERIR GOLPE PESADO */
if (!abriu_menu && area > 0) {
if (sala == 4 && !(baus_mask & (1 << area)) && px >= 30 && px <= 42) {
baus_mask |= (1 << area); minerios += 3; elixires++; resinas++; antidotos++;
if (area == 3) escudos_mask |= (1 << 2);
tocar_som(2);
sprintf(fala, area == 3 ? "RELIQUIA + ESCUDO GRANDE DE FERRO coletados!" :
"RELIQUIA DA SALA 4 COLETADA! +3 Minerios e consumiveis!");
} else if (atk_timer == 0 && roll_timer == 0 && stam >= 32) {
stam -= 32;
atk_timer = 7;
heavy_atk = 1;
tocar_som(0);
if ((arma_atual == 4 || arma_atual == 3) && !getsuga_ativa) {
getsuga_ativa = 1; gx = px + 6; gy = py;
tocar_som(1);
}
}
}
}
/* Magia: Relampago (Circulo) */
int custo_mp = (anel_equipado == 4) ? 2 : 3;
if ((new_pad & PAD_CIRCLE) && raio_timer == 0 && mp >= custo_mp) {
mp -= custo_mp;
raio_timer = 6;
raio_x = (area > 0 && ehp > 0) ? ex : (px + 10);
raio_y = (area > 0 && ehp > 0) ? ey : py;
tocar_som(1);
if (area > 0 && ehp > 0) {
int dano_raio = atk_base + mag_bonus + arma_nv + 6 + (anel_equipado == 4 ? 4 : 0);
ehp -= dano_raio;
e_stun = 14;
e_postura += 6;
hitstop = 3;
sprintf(fala, "RELAMPAGO! Causou %d de dano + Atordoamento!", dano_raio);
} else sprintf(fala, "Relampago invocado nos ceus!");
}
/* Movimento de Projeteis do Jogador */
if (flecha_ativa) { fx += 4; if (fx > 74) flecha_ativa = 0; }
if (getsuga_ativa) {
gx += 3;
if (gx > 74) getsuga_ativa = 0;
if (area > 0 && ehp > 0 && getsuga_ativa && abs_val(gy - ey) <= 1 && gx >= ex - 2 && gx <= ex + 5) {
int d_get = atk_base + mag_bonus + arma_nv + 5;
ehp -= d_get;
getsuga_ativa = 0;
hitstop = 3;
tocar_som(1);
sprintf(fala, arma_atual == 4 ? "GETSUGA TENSHOU! Onda cortou o alvo (-%d HP)!" :
"ONDA COLOSSAL da Dragon Slayer (-%d HP)!", d_get);
}
}
/* Projeteis Inimigos */
if (e_proj_ativo) {
epx -= (e_proj_tipo == 1) ? 2 : 3;
if (e_proj_tipo == 1 && frame % 3 == 0) { if (epy < py) epy++; else if (epy > py) epy--; }
if (epx < 2) e_proj_ativo = 0;
if (e_proj_ativo && abs_val(epy - py) <= 1 && abs_val(epx - px) <= 2) {
if (parry_timer > 0) {
e_proj_ativo = 0;
if (ehp > 0) ehp -= (atk_base + arma_nv + 5);
tocar_som(4);
sprintf(fala, "PARRY! Voce rebateu o projetil de volta no inimigo!");
} else if (roll_timer == 0) {
int red_esc = (escudo_atual == 2) ? 1 : ((escudo_atual == 1) ? 2 : 3);
int d_proj = defendendo ? red_esc : (4 + area);
hp -= d_proj;
e_proj_ativo = 0;
tocar_som(3);
sprintf(fala, defendendo ? "Escudo bloqueou projetil (-%d HP)." : "Atingido a distancia (-%d HP)!", d_proj);
}
}
}
if (onda_ativa) {
onda_x -= 2;
if (onda_x < 2) onda_ativa = 0;
if (onda_ativa && roll_timer == 0 && abs_val(py - onda_y) <= 1 && abs_val(px - onda_x) <= 2) {
hp -= (6 + area);
onda_ativa = 0;
tocar_som(3);
sprintf(fala, "Onda sismica te atingiu! Use (X) para rolar por cima!");
}
}
/* === INTELIGENCIA ARTIFICIAL TATICA + 3 FASES DE CHEFE === */
if (area > 0 && ehp > 0) {
if (e_burn > 0) { e_burn--; if (e_burn % 20 == 0) ehp -= 2; }
if (e_poison > 0) { e_poison--; if (e_poison % 25 == 0) ehp -= 2; }
if (e_chill > 0) e_chill--;
if (tipo_chefe > 0 && !eh_subchefe) {
if (chefe_fase == 1 && ehp <= (ehp_max * 60) / 100) {
chefe_fase = 2; shake = 8; tocar_som(1);
sprintf(fala, "FASE 2/3! %s despertou seu poder elemental!", nome_guardiao(tipo_chefe, 0));
} else if (chefe_fase == 2 && ehp <= (ehp_max * 25) / 100) {
chefe_fase = 3; shake = 10; tocar_som(1);
sprintf(fala, "FASE FINAL 3/3! %s entrou em FRENESI MORTAL!", nome_guardiao(tipo_chefe, 0));
}
}
if (e_stun > 0) {
e_stun--;
} else if (e_chill > 0 && frame % 2 == 0) {
/* Congelamento da Runa de Gelo retarda o inimigo */
} else if (tipo_chefe == 0) {
int dist = ex - px;
if (e_ai_estado == 0) {
if (e_arquetipo == 2 || e_arquetipo == 3) {
if (dist < 22 && ex < 68) ex++;
else if (dist > 34) ex--;
if (frame % 4 == 0) { if (ey < py) ey++; else if (ey > py) ey--; }
e_ai_timer++;
if (e_ai_timer >= 32 && abs_val(ey - py) <= 1) { e_ai_estado = 1; e_ai_timer = 0; }
} else {
int vel_e = (e_arquetipo == 4) ? 3 : 2;
if (frame % vel_e == 0 && dist > 5) ex--;
if (frame % 4 == 0) { if (ey < py) ey++; else if (ey > py) ey--; }
if (dist <= 6 && abs_val(ey - py) <= 1) { e_ai_estado = 1; e_ai_timer = 0; }
}
} else if (e_ai_estado == 1) {
e_ai_timer++;
int tempo_tele = (e_arquetipo == 4) ? 24 : 16;
if (e_ai_timer >= tempo_tele) { e_ai_estado = 2; e_ai_timer = 0; }
} else if (e_ai_estado == 2) {
if (e_arquetipo == 2 || e_arquetipo == 3) {
if (!e_proj_ativo) {
e_proj_ativo = 1; epx = ex - 2; epy = ey;
e_proj_tipo = (e_arquetipo == 3) ? 1 : 0;
tocar_som(1);
}
} else if (abs_val(px - ex) <= 6 && abs_val(py - ey) <= 1) {
if (roll_timer > 0) {
/* Esquiva perfeita */
} else if (parry_timer > 0 && e_arquetipo != 4) {
e_stun = 24;
ehp -= (atk_base + arma_nv + 8);
e_postura += 10;
hitstop = 4;
tocar_som(4);
sprintf(fala, "PARRY! Contra-ataque devastador!");
} else if (defendendo) {
int d_blk = (escudo_atual == 2) ? 0 : 1;
stam -= (escudo_atual == 2) ? 22 : 15;
hp -= d_blk; tocar_som(0);
sprintf(fala, d_blk == 0 ? "ESCUDO GRANDE bloqueou 100%% do golpe!" : "Escudo absorveu o golpe (-1 HP)!");
} else {
int d_e = (e_arquetipo == 4) ? (5 + area) : (3 + area);
hp -= d_e; tocar_som(3);
sprintf(fala, "Golpeado (-%d HP)! Leia o aviso ( ! )!", d_e);
}
}
e_ai_estado = 3; e_ai_timer = 0;
} else if (e_ai_estado == 3) {
e_ai_timer++;
if (e_ai_timer >= 18) { e_ai_estado = 0; e_ai_timer = 0; }
}
} else {
int dist = ex - px;
int vel_c = (chefe_fase == 3) ? 1 : ((chefe_fase == 2) ? 2 : 3);
if (tipo_chefe == 2 && !eh_subchefe) {
if (dist < 20 && ex < 66) ex++;
else if (dist > 36) ex--;
} else if (frame % vel_c == 0 && dist > 5) ex--;
if (frame % 4 == 0) { if (ey < py) ey++; else if (ey > py) ey--; }
if (e_ai_estado == 0) {
e_ai_timer++;
int espera = (chefe_fase == 3) ? 16 : ((chefe_fase == 2) ? 24 : 32);
if (e_ai_timer >= espera) { e_ai_estado = 1; e_ai_timer = 0; }
} else if (e_ai_estado == 1) {
e_ai_timer++;
int tele = (chefe_fase == 3) ? 14 : 18;
if (e_ai_timer >= tele) { e_ai_estado = 2; e_ai_timer = 0; }
} else if (e_ai_estado == 2) {
if (tipo_chefe == 2) {
if (!e_proj_ativo) { e_proj_ativo = 1; epx = ex - 2; epy = ey; e_proj_tipo = 1; tocar_som(1); }
if (chefe_fase >= 2 && raio_chefe_timer == 0) { raio_chefe_timer = 16; rc_x = px; rc_y = py; }
} else if (tipo_chefe == 3 || chefe_fase == 3) {
if (!onda_ativa) { onda_ativa = 1; onda_x = ex - 2; onda_y = ey; shake = 5; tocar_som(3); }
if (chefe_fase == 3 && raio_chefe_timer == 0) { raio_chefe_timer = 16; rc_x = px; rc_y = py; }
} else if (tipo_chefe == 4 || tipo_chefe == 5) {
if (raio_chefe_timer == 0) { raio_chefe_timer = 15; rc_x = px; rc_y = py; }
}
if (abs_val(px - ex) <= 7 && abs_val(py - ey) <= 1 && roll_timer == 0) {
if (parry_timer > 0 && tipo_chefe != 3) {
e_stun = 24; ehp -= (atk_base + arma_nv + 10); e_postura += 10;
hitstop = 4; tocar_som(4);
sprintf(fala, "PARRY LENDARIO! Postura do Guardiao abalada!");
} else if (defendendo) {
int d_blk = (escudo_atual == 2) ? 1 : 2;
stam -= 24; hp -= d_blk; tocar_som(0);
sprintf(fala, "Escudo segurou o golpe brutal (-%d HP)!", d_blk);
} else {
int d_g = (chefe_fase * 2) + 3 + area;
hp -= d_g; tocar_som(3);
sprintf(fala, "Impacto devastador (-%d HP)!", d_g);
}
}
e_ai_estado = 3; e_ai_timer = 0;
} else if (e_ai_estado == 3) {
e_ai_timer++;
int exaustao = (chefe_fase == 3) ? 12 : 18;
if (e_ai_timer >= exaustao) { e_ai_estado = 0; e_ai_timer = 0; }
}
}
if (raio_chefe_timer > 0) {
raio_chefe_timer--;
if (raio_chefe_timer == 1) {
tocar_som(1);
if (roll_timer == 0 && abs_val(px - rc_x) <= 2 && abs_val(py - rc_y) <= 1) {
hp -= (6 + area);
tocar_som(3);
sprintf(fala, "Raio sombrio te atingiu! Saia da marca '( !! )'!");
}
}
}
/* Acerto de Flecha */
if (flecha_ativa && abs_val(fy - ey) <= 1 && fx >= ex - 2 && fx <= ex + 5) {
flecha_ativa = 0;
int d_a = atk_base + arma_nv + 3 + (anel_equipado == 2 ? 3 : 0) + (resina_timer > 0 ? 4 : 0);
ehp -= d_a; e_postura += 4; hitstop = 2; tocar_som(0);
if (runa_equipada == 1) e_burn = 80;
if (runa_equipada == 2) e_poison = 120;
if (runa_equipada == 3) e_chill = 90;
if (runa_equipada == 4 && hp < hp_max) hp++;
sprintf(fala, "Flecha acertou (-%d HP)!", d_a);
}
/* Acerto Corpo a Corpo */
int alcance = (arma_atual == 3 || arma_atual == 5) ? 10 : 8;
if (atk_timer == 2 && abs_val(px - ex) <= alcance && abs_val(py - ey) <= 1) {
if (e_arquetipo == 1 && !heavy_atk && e_stun == 0 && tipo_chefe == 0 && runa_equipada != 1 && arma_atual != 5) {
tocar_som(0);
sprintf(fala, "ESCUDO BLOQUEOU! Use Golpe Pesado, Foice ou Runa de FOGO!");
} else {
int mult = heavy_atk ? 2 : 1;
int bonus_arma = (arma_atual == 3) ? 5 : ((arma_atual == 4 || arma_atual == 5) ? 3 : 1);
int bonus = bonus_arma + (anel_equipado == 2 ? 3 : 0) + (resina_timer > 0 ? 4 : 0);
int d_m = (atk_base + arma_nv + bonus) * mult;
ehp -= d_m;
e_postura += (heavy_atk || arma_atual == 1 || arma_atual == 3) ? 8 : 4;
hitstop = heavy_atk ? 4 : 2;
if (heavy_atk || arma_atual == 3) shake = 4;
tocar_som(0);
if (runa_equipada == 1) e_burn = 80;
else if (runa_equipada == 2) e_poison = 120;
else if (runa_equipada == 3) e_chill = 90;
else if (runa_equipada == 4 && hp < hp_max) hp++;
if (arma_atual == 5) {
e_sangramento++;
if (e_sangramento >= 4) {
e_sangramento = 0;
int d_bleed = 14 + (area * 3);
ehp -= d_bleed;
tocar_som(4);
sprintf(fala, "SANGRAMENTO DA FOICE! Explodiu por -%d HP extra!", d_bleed);
} else {
sprintf(fala, "Corte da Foice (-%d HP) [Sangramento: %d/4]", d_m, e_sangramento);
}
} else if (e_postura >= e_postura_max) {
e_postura = 0; e_stun = 30;
int d_crit = d_m + 12;
ehp -= d_crit;
tocar_som(4);
sprintf(fala, "POSTURA QUEBRADA! GOLPE CRITICO (-%d HP)!", d_crit);
} else {
sprintf(fala, heavy_atk ? "GOLPE PESADO (-%d HP)!" : "Acerto com %s (-%d HP)!", nome_arma(arma_atual), d_m);
}
}
}
/* === MORTE DO INIMIGO, SUB-CHEFE OU LORDE === */
if (ehp <= 0) {
int eh_boss = (tipo_chefe > 0);
int ganho_almas = eh_boss ? (eh_subchefe ? 95 : 210) : (32 + area * 8);
almas += ganho_almas;
minerios += eh_boss ? (eh_subchefe ? 2 : 4) : 1;
tocar_som(eh_boss ? 5 : 2);
if (eh_boss && eh_subchefe) {
subchefes_mask |= (1 << area);
tipo_chefe = 0; eh_subchefe = 0; ehp = 0;
sprintf(fala, "SUB-CHEFE VENCIDO (+%d Almas)! Caminho para Sala 4 aberto!", ganho_almas);
} else if (eh_boss && !eh_subchefe) {
if (chefes_mortos < area) chefes_mortos = area;
almas_chefe_mask |= (1 << area);
if (area <= 4) {
runas_mask |= (1 << area);
runa_equipada = area;
}
tipo_chefe = 0; ehp = 0;
onda_ativa = 0; raio_chefe_timer = 0;
if (area == 5) {
jogo_zerado = 1;
break;
}
sprintf(fala, "LORDE VENCIDO! Ganhou GRANDE ALMA + PEDRA RUNICA DE %s!", nome_runa(area));
} else {
if (horda_restante > 0) horda_restante--;
if (horda_restante > 0) {
ex = 65; ey = 14; e_stun = 0; e_ai_estado = 0; e_postura = 0;
e_burn = 0; e_chill = 0; e_poison = 0; e_sangramento = 0;
e_arquetipo = (e_arquetipo + 1) % 5;
ehp_max = 16 + (area * 8) + (sala * 2) + (nivel * 3);
ehp = ehp_max;
sprintf(fala, "Inimigo abatido (+%d Almas)! Restam %d na horda!", ganho_almas, horda_restante);
} else {
ehp = 0;
sprintf(fala, "SALA PURIFICADA (+%d Almas)! Siga para a direita (-->)!", ganho_almas);
}
}
}
}
/* Morte do Jogador */
if (area > 0 && hp <= 0) {
mortes++;
area = 0; sala = 1; horda_restante = 0; ehp = 0;
px = 12; py = 15;
hp = hp_max; mp = mp_max; stam = stam_max; frascos = frascos_max;
veneno_timer = 0; tipo_chefe = 0; eh_subchefe = 0; onda_ativa = 0; raio_chefe_timer = 0;
tocar_som(3);
sprintf(fala, "VOCE MORREU... Despertou na Fogueira do Santuario (Quedas: %d).", mortes);
}
if (atk_timer > 0) atk_timer--;
if (raio_timer > 0) raio_timer--;
if (roll_timer > 0) roll_timer--;
if (parry_timer > 0) parry_timer--;
int sx = (shake > 0) ? ((shake-- % 2 == 0) ? 1 : -1) : 0;
/* Limpa arena */
for (int y = 4; y <= 19; y++) {
scr_setXY(0, y);
scr_printf("                                                                              ");
}
/* HUD Superior */
scr_setfontcolor(COR_OURO);
scr_setXY(1, 1);
if (area == 0) scr_printf("SANTUARIO LV:%d", nivel);
else scr_printf("R:%d S:%d/6 LV:%d", area, sala, nivel);
scr_setfontcolor(veneno_timer > 0 ? COR_VERDE : COR_VERMELHO);
scr_setXY(17, 1); scr_printf("HP:%d/%d", hp, hp_max);
scr_setfontcolor(COR_CIANO);
scr_setXY(28, 1); scr_printf("MP:%d/%d", mp, mp_max);
scr_setfontcolor(COR_VERDE);
scr_setXY(38, 1); scr_printf("VIG:%d%%", (stam * 100) / stam_max);
scr_setfontcolor(COR_LARANJA);
scr_setXY(47, 1); scr_printf("%s:%d", nome_item(item_atual),
item_atual == 0 ? frascos : (item_atual == 1 ? elixires : (item_atual == 2 ? resinas : antidotos)));
scr_setfontcolor(COR_CIANO);
scr_setXY(57, 1); scr_printf("RUNA:%-6s", nome_runa(runa_equipada));
scr_setfontcolor(COR_OURO);
scr_setXY(69, 1); scr_printf("ALM:%-5d", almas);
scr_setfontcolor(COR_CINZA);
scr_setXY(1, 2);
if (area > 0 && horda_restante > 0) {
scr_printf("ARMA:%s(+%d) | ESC:%s | HORDA: %d | (START)Menu",
nome_arma(arma_atual), arma_nv, nome_escudo(escudo_atual), horda_restante);
} else {
scr_printf("ARMA:%s(+%d) | ESC:%s | MIN:%d | (START)Menu",
nome_arma(arma_atual), arma_nv, nome_escudo(escudo_atual), minerios);
}
/* Horizonte */
scr_setfontcolor(COR_CINZA);
scr_setXY(0, 8);
scr_printf("..---/\\----...   ...---/\\----...   ...---/\\---...");
scr_setXY(0, 10);
scr_printf("------------------------------------------------------------------------------");
/* Cenarios */
if (area == 0) {
scr_setfontcolor(COR_ROXO);
scr_setXY(6, 12);  scr_printf(" (O_O) ");
scr_setXY(6, 13);  scr_printf("(QUESTS)");
scr_setfontcolor(COR_LARANJA);
scr_setXY(21, 11); scr_printf(frame % 4 < 2 ? "  ( | )  " : "  ) | (  ");
scr_setXY(21, 12); scr_printf("   (|)   ");
scr_setfontcolor(COR_OURO);
scr_setXY(20, 13); scr_printf("(FOGUEIRA)");
scr_setfontcolor(COR_CIANO);
scr_setXY(45, 11); scr_printf(" .-----. ");
scr_setXY(45, 12); scr_printf(" |FORJA| ");
scr_setXY(45, 13); scr_printf(" /-----/ ");
scr_setfontcolor((baus_mask & 1) ? COR_CINZA : COR_OURO);
scr_setXY(60, 12); scr_printf((baus_mask & 1) ? "(___)" : "(=*=)");
scr_setfontcolor(COR_VERDE);
scr_setXY(67, 14); scr_printf("REINO 1 ->");
} else {
if (sala == 1) {
int fog_acesa = (fogueiras_mask & (1 << area));
scr_setfontcolor(fog_acesa ? COR_LARANJA : COR_CINZA);
scr_setXY(15, 11); scr_printf(fog_acesa ? "  ( | )  " : "   .|.   ");
scr_setXY(15, 12); scr_printf("   (|)   ");
scr_setfontcolor(fog_acesa ? COR_OURO : COR_CINZA);
scr_setXY(14, 13); scr_printf("(FOGUEIRA)");
}
if (sala == 4) {
int bau_aberto = (baus_mask & (1 << area));
scr_setfontcolor(bau_aberto ? COR_CINZA : COR_OURO);
scr_setXY(34, 11); scr_printf(bau_aberto ? "(___)" : "(=*=)");
scr_setXY(34, 12); scr_printf(" BAU ");
}
scr_setfontcolor(COR_OURO);
scr_setXY(2, 14); scr_printf("<- VOLTAR");
int sala_limpa = (horda_restante == 0 && ehp <= 0);
scr_setfontcolor(sala_limpa ? COR_VERDE : COR_CINZA);
scr_setXY(64, 14);
if (!sala_limpa) scr_printf("(BLOQUEADO)");
else if (sala < 6) scr_printf("SALA %d ->", sala + 1);
else if (area < 5) scr_printf("REINO %d ->", area + 1);
}
/* Linha de Mira e Marcas */
if (area > 0 && ehp > 0 && tipo_chefe == 0 && e_arquetipo == 2 && e_ai_estado == 1) {
scr_setfontcolor(COR_AMARELO);
for (int mx = px + 6; mx < ex; mx += 3) {
scr_setXY(mx, ey - 1); scr_printf("- -");
}
}
if (raio_chefe_timer > 0) {
scr_setfontcolor(raio_chefe_timer > 3 ? COR_ROXO : COR_VERMELHO);
scr_setXY(rc_x, rc_y);
scr_printf(raio_chefe_timer > 3 ? "( !! )" : "VVVVVV");
}
if (onda_ativa) {
scr_setfontcolor(COR_LARANJA);
scr_setXY(onda_x, onda_y); scr_printf("~~~");
}
/* Ordenacao 2.5D */
int jogador_atras = (py < ey);
for (int passo = 0; passo < 2; passo++) {
if ((passo == 0 && jogador_atras) || (passo == 1 && !jogador_atras)) {
int jx = px + sx;
if (jx < 1) jx = 1;
scr_setfontcolor(parry_timer > 0 ? COR_OURO : (roll_timer > 0 ? COR_CIANO : (resina_timer > 0 ? COR_LARANJA : COR_BRANCO)));
if (roll_timer > 0) {
scr_setXY(jx, py - 1); scr_printf(" .-. ");
scr_setXY(jx, py);     scr_printf("( @ )");
} else {
scr_setXY(jx, py - 2); scr_printf(arma_nv > 0 ? " ^o^ " : "  o  ");
scr_setXY(jx, py - 1);
if (parry_timer > 0)      scr_printf("(#)!PARRY");
else if (defendendo)      scr_printf(escudo_atual == 2 ? "(##)|DEF " : "(#)|-DEF ");
else if (atk_timer > 0) {
if (arma_atual == 3)      scr_printf("(#)|======>>");
else if (arma_atual == 5) scr_printf("(#)|----/)  ");
else                      scr_printf(heavy_atk ? "(#)|===>>" : "(#)|===>");
} else {
if (arma_atual == 3)      scr_printf("(#)|==== ");
else if (arma_atual == 5) scr_printf("(#)|---/ ");
else                      scr_printf("(#)|- -  ");
}
scr_setXY(jx, py);
scr_printf((paddata & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN)) && (frame % 4 < 2) ? " / | " : " | | ");
}
} else if (area > 0 && ehp > 0) {
int ix = ex + sx;
if (ix < 1) ix = 1;
if (e_stun > 0) {
scr_setfontcolor(COR_CIANO);
scr_setXY(ix + 1, ey - 4); scr_printf("STUN");
} else if (e_ai_estado == 1) {
scr_setfontcolor(COR_AMARELO);
scr_setXY(ix + 2, ey - 4); scr_printf("( ! )");
} else if (e_ai_estado == 3) {
scr_setfontcolor(COR_CINZA);
scr_setXY(ix + 2, ey - 4); scr_printf(" ... ");
}
scr_setfontcolor(e_stun > 0 ? COR_CIANO : (e_burn > 0 ? COR_LARANJA : (e_poison > 0 ? COR_VERDE : (chefe_fase >= 2 ? COR_ROXO : COR_VERMELHO))));
if (tipo_chefe > 0) {
scr_setXY(ix, ey - 3); scr_printf(eh_subchefe ? "  .---.  " : "  .===.  ");
scr_setXY(ix, ey - 2); scr_printf(e_stun > 0 ? " (x _ x) " : " (O _ O) ");
scr_setXY(ix, ey - 1); scr_printf(chefe_fase == 3 ? "<|#####|>" : " /|#####|");
scr_setXY(ix, ey);     scr_printf("  /   |  ");
} else {
scr_setXY(ix, ey - 2);
scr_printf(e_arquetipo == 2 ? "  .<->.  " : (e_arquetipo == 3 ? "  /^^^\  " : "  .---.  "));
scr_setXY(ix, ey - 1);
scr_printf(e_arquetipo == 1 ? "||-|###| " : (e_arquetipo == 2 ? "(-|###|  " : " /|###|  "));
scr_setXY(ix, ey);
scr_printf(frame % 6 < 3 ? "  / |    " : "  | \    ");
}
}    
}
/* Projeteis na Tela */
if (flecha_ativa) { scr_setfontcolor(COR_OURO); scr_setXY(fx, fy - 1); scr_printf("--->"); }
if (getsuga_ativa) { scr_setfontcolor(COR_CIANO); scr_setXY(gx, gy - 1); scr_printf(")))>"); }
if (e_proj_ativo) {
scr_setfontcolor(e_proj_tipo == 1 ? COR_ROXO : COR_VERMELHO);
scr_setXY(epx, epy - 1); scr_printf(e_proj_tipo == 1 ? "(**)" : "<---");
}
if (raio_timer > 0) {
scr_setfontcolor(COR_CIANO);
for (int ry = 4; ry <= raio_y; ry++) {
scr_setXY(raio_x + (ry % 2 == 0 ? 1 : 0), ry);
scr_printf(ry % 2 == 0 ? "VV" : "AA");
}
}
/* Chao Colorido por Reino */
scr_setXY(0, 18);
if (area == 0)      { scr_setfontcolor(COR_OURO);    scr_printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"); }
else if (area == 1) { scr_setfontcolor(COR_VERDE);   scr_printf(",.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,."); }
else if (area == 2) { scr_setfontcolor(COR_LARANJA); scr_printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"); }
else if (area == 3) { scr_setfontcolor(COR_CIANO);   scr_printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^"); }
else if (area == 4) { scr_setfontcolor(COR_CINZA);   scr_printf("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+="); }
else                { scr_setfontcolor(COR_ROXO);    scr_printf("=============================================================================="); }
/* Barra de Vida do Sub-Chefe ou Lorde Guardiao */
if (area > 0 && tipo_chefe > 0 && ehp > 0) {
int cheios = (ehp * 24) / ehp_max;
if (cheios < 0) cheios = 0;
if (cheios > 24) cheios = 24;
for (int b = 0; b < 24; b++) *(barra + b) = (b < cheios) ? '#' : '.';
*(barra + 24) = '\0';
scr_setfontcolor(chefe_fase == 3 ? COR_ROXO : (chefe_fase == 2 ? COR_LARANJA : COR_OURO));
scr_setXY(2, 19);
if (eh_subchefe) scr_printf("%s: ", nome_guardiao(tipo_chefe, 1));
else scr_printf("%s (F%d/3): ", nome_guardiao(tipo_chefe, 0), chefe_fase);
scr_setfontcolor(COR_VERMELHO);
scr_printf("(%s) %d/%d", barra, ehp > 0 ? ehp : 0, ehp_max);
}
/* Caixa de Mensagens */
scr_setfontcolor(COR_BRANCO);
scr_setXY(2, 20);
scr_printf("+----------------------------------------------------------------------------+");
scr_setXY(2, 21);
scr_printf("| %-74s |", fala);
scr_setXY(2, 22);
scr_printf("+----------------------------------------------------------------------------+");
usleep(45000);
}
if (!reiniciar) {
tela_historia_final(nivel, arma_nv, mortes);
}
}
return 0;
}
