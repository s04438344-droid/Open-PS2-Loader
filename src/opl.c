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
static int opt_musica = 1;
static int opt_sfx = 1;
static int opt_shake = 1;
static int opt_pesadelo = 0;

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
    int elmo_atual;
    int peito_atual;
    int perna_atual;
    int armaduras_mask;
    int npc_sol_prog;
    int npc_ceb_prog;
    int npc_mag_prog;
    int poca_area;
    int poca_sala;
    int poca_x;
    int poca_y;
    int poca_almas;
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
    if (!som_ativo || !opt_sfx || !audsrv_play_audio) return;
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

void atualizar_musica(int frame, int area, int tipo_chefe, int invasor) {
    if (!som_ativo || !opt_musica || !audsrv_play_audio) return;
    int ritmo = (tipo_chefe > 0 || invasor) ? 6 : ((area == 0) ? 14 : 10);
    if (frame % ritmo != 0) return;

    static const int melodia_hub[]   = { 44, 36, 33, 29, 33, 36, 44, 48 };
    static const int melodia_reino[] = { 36, 36, 30, 33, 36, 28, 30, 33 };
    static const int melodia_boss[]  = { 24, 24, 20, 24, 18, 20, 22, 24 };

    int idx = (frame / ritmo) % 8;
    int periodo = (tipo_chefe > 0 || invasor) ? melodia_boss[idx] :
                  ((area == 0) ? melodia_hub[idx] : melodia_reino[idx]);

    short *bgm_buf = ((short *)&sfxMem) + 128;
    int amostras = 96;
    for (int i = 0; i < amostras; i++) {
        int env = (i < 16) ? (i * 200) : (3200 - (i * 24));
        if (env < 0) env = 0;
        *(bgm_buf + i) = ((i % periodo) < (periodo / 2)) ? (short)env : (short)(-env);
    }
    audsrv_play_audio((const char *)bgm_buf, amostras * 2);
}

int salvar_jogo(SaveData *sd) {
    int fd = open("mc0:/DARKSTONE_V3.SAV", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return 0;
    sd->magic = 0x53543033;
    write(fd, sd, sizeof(SaveData));
    close(fd);
    return 1;
}

int carregar_jogo(SaveData *sd) {
    int fd = open("mc0:/DARKSTONE_V3.SAV", O_RDONLY);
    if (fd < 0) return 0;
    read(fd, sd, sizeof(SaveData));
    close(fd);
    return (sd->magic == 0x53543033);
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

void tela_opcoes(void) {
    struct padButtonStatus buttons;
    u32 paddata = 0, old_pad = 0, new_pad = 0;
    int cur = 0;
    scr_clear();
    while (1) {
        if (padGetState(0, 0) == PAD_STATE_STABLE) {
            if (padRead(0, 0, &buttons) != 0) {
                paddata = 0xffff ^ buttons.btns;
                new_pad = paddata & ~old_pad;
                old_pad = paddata;
            }
        }
        if ((new_pad & PAD_UP) && cur > 0) { cur--; tocar_som(0); }
        if ((new_pad & PAD_DOWN) && cur < 4) { cur++; tocar_som(0); }
        if (new_pad & (PAD_CROSS | PAD_LEFT | PAD_RIGHT)) {
            if (cur == 0) { opt_musica = !opt_musica; tocar_som(0); }
            else if (cur == 1) { opt_sfx = !opt_sfx; tocar_som(0); }
            else if (cur == 2) { opt_shake = !opt_shake; tocar_som(0); }
            else if (cur == 3) { opt_pesadelo = !opt_pesadelo; tocar_som(0); }
            else if (cur == 4 && (new_pad & PAD_CROSS)) { tocar_som(2); scr_clear(); return; }
        }
        if (new_pad & PAD_TRIANGLE) { tocar_som(2); scr_clear(); return; }

        scr_setfontcolor(COR_OURO);
        scr_setXY(14, 3); scr_printf("+================================================+");
        scr_setXY(14, 4); scr_printf("|               OPCOES DO SISTEMA                |");
        scr_setXY(14, 5); scr_printf("+------------------------------------------------+");

        scr_setfontcolor(cur == 0 ? COR_OURO : COR_BRANCO);
        scr_setXY(14, 7); scr_printf("| %s Musica de Fundo:      %-18s |", cur == 0 ? "-->" : "   ", opt_musica ? "LIGADA" : "DESLIGADA");
        scr_setfontcolor(cur == 1 ? COR_OURO : COR_BRANCO);
        scr_setXY(14, 9); scr_printf("| %s Efeitos Sonoros (SFX):%-18s |", cur == 1 ? "-->" : "   ", opt_sfx ? "LIGADOS" : "DESLIGADOS");
        scr_setfontcolor(cur == 2 ? COR_OURO : COR_BRANCO);
        scr_setXY(14, 11); scr_printf("| %s Tremor de Tela:       %-18s |", cur == 2 ? "-->" : "   ", opt_shake ? "LIGADO" : "DESLIGADO");
        scr_setfontcolor(cur == 3 ? COR_ROXO : COR_BRANCO);
        scr_setXY(14, 13); scr_printf("| %s Modo Dificuldade:     %-18s |", cur == 3 ? "-->" : "   ", opt_pesadelo ? "PESADELO (NG+)" : "PADRAO (SOULS)");
        scr_setfontcolor(cur == 4 ? COR_VERDE : COR_BRANCO);
        scr_setXY(14, 16); scr_printf("| %s Voltar ao Menu Principal                   |", cur == 4 ? "-->" : "   ");

        scr_setfontcolor(COR_OURO);
        scr_setXY(14, 18); scr_printf("+------------------------------------------------+");
        scr_setXY(14, 19); scr_printf("| (Cima/Baixo) Mover | (X) Alterar | (Tri) Voltar|");
        scr_setXY(14, 20); scr_printf("+================================================+");
        scr_setfontcolor(COR_BRANCO);
        usleep(40000);
    }
}

int tela_menu_principal(int tem_save) {
    struct padButtonStatus buttons;
    u32 paddata = 0, old_pad = 0, new_pad = 0;
    int sel = tem_save ? 0 : 1;
    int confirmando_novo = 0;
    int conf_sel = 0;
    scr_clear();

    while (1) {
        if (padGetState(0, 0) == PAD_STATE_STABLE) {
            if (padRead(0, 0, &buttons) != 0) {
                paddata = 0xffff ^ buttons.btns;
                new_pad = paddata & ~old_pad;
                old_pad = paddata;
            }
        }

        if (!confirmando_novo) {
            if ((new_pad & PAD_UP) && sel > 0) { sel--; tocar_som(0); }
            if ((new_pad & PAD_DOWN) && sel < 2) { sel++; tocar_som(0); }
            if (new_pad & PAD_CROSS) {
                if (sel == 0) {
                    if (tem_save) { tocar_som(2); scr_clear(); return 1; }
else tocar_som(3);
} else if (sel == 1) {
if (tem_save) {
confirmando_novo = 1;
conf_sel = 0;
tocar_som(1);
scr_clear();
} else {
tocar_som(2); scr_clear(); return 0;
}
} else if (sel == 2) {
tocar_som(0);
tela_opcoes();
}
}
scr_setfontcolor(COR_OURO);
scr_setXY(10, 2);  scr_printf("=== DARK STONE SOULS: SOUL TRANSPOSITION ===");
scr_setfontcolor(COR_CIANO);
scr_setXY(33, 4);  scr_printf("    /\\    ");
scr_setXY(33, 5);  scr_printf("   /==\\   ");
scr_setfontcolor(COR_LARANJA);
scr_setXY(33, 6);  scr_printf("  ()()  ");
scr_setfontcolor(COR_CIANO);
scr_setXY(33, 7);  scr_printf("   \\==/   ");
scr_setXY(33, 8);  scr_printf("    \\/    ");
scr_setfontcolor(COR_CINZA);
scr_setXY(6, 10); scr_printf("30 Salas em 5 Reinos | Combate 2.5D | NPCs Viajantes | Armaduras");
scr_setfontcolor(sel == 0 ? (tem_save ? COR_OURO : COR_VERMELHO) : (tem_save ? COR_BRANCO : COR_CINZA));
scr_setXY(20, 13); scr_printf("%s CONTINUAR JORNADA %s", sel == 0 ? "-->" : "   ", tem_save ? "(SAVE OK)" : "(SEM SAVE)");
scr_setfontcolor(sel == 1 ? COR_OURO : COR_BRANCO);
scr_setXY(20, 15); scr_printf("%s NOVO JOGO", sel == 1 ? "-->" : "   ");
scr_setfontcolor(sel == 2 ? COR_CIANO : COR_BRANCO);
scr_setXY(20, 17); scr_printf("%s OPCOES", sel == 2 ? "-->" : "   ");
scr_setfontcolor(COR_OURO);
scr_setXY(17, 21); scr_printf("(Cima/Baixo) Selecionar  |  (X) Confirmar");
scr_setfontcolor(COR_BRANCO);
} else {
if ((new_pad & PAD_UP) && conf_sel > 0) { conf_sel--; tocar_som(0); }
if ((new_pad & PAD_DOWN) && conf_sel < 1) { conf_sel++; tocar_som(0); }
if (new_pad & PAD_TRIANGLE) { confirmando_novo = 0; tocar_som(0); scr_clear(); }
if (new_pad & PAD_CROSS) {
if (conf_sel == 0) {
confirmando_novo = 0; tocar_som(0); scr_clear();
} else {
tocar_som(2); scr_clear(); return 0;
}
}
scr_setfontcolor(COR_VERMELHO);
scr_setXY(10, 6);  scr_printf("+==========================================================+");
scr_setXY(10, 7);  scr_printf("|       ATENCAO: SAVE ENCONTRADO NO MEMORY CARD!           |");
scr_setXY(10, 8);  scr_printf("+----------------------------------------------------------+");
scr_setfontcolor(COR_BRANCO);
scr_setXY(10, 9);  scr_printf("| Tem certeza que deseja iniciar um NOVO JOGO?             |");
scr_setXY(10, 10); scr_printf("| O save anterior sera substituido ao usar a Fogueira.     |");
scr_setfontcolor(COR_VERMELHO);
scr_setXY(10, 11); scr_printf("+----------------------------------------------------------+");
scr_setfontcolor(conf_sel == 0 ? COR_VERDE : COR_BRANCO);
scr_setXY(10, 13); scr_printf("| %s NAO, VOLTAR E MANTER MEU SAVE                        |", conf_sel == 0 ? "-->" : "   ");
scr_setfontcolor(conf_sel == 1 ? COR_VERMELHO : COR_CINZA);
scr_setXY(10, 15); scr_printf("| %s SIM, TENHO CERTEZA (INICIAR NOVO JOGO)               |", conf_sel == 1 ? "-->" : "   ");
scr_setfontcolor(COR_VERMELHO);
scr_setXY(10, 17); scr_printf("+==========================================================+");
scr_setfontcolor(COR_BRANCO);
}
usleep(40000);
}
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
if ((new_pad & PAD_DOWN) && sel < 3) { sel++; tocar_som(0); }
if (new_pad & PAD_CROSS) { tocar_som(2); scr_clear(); return sel; }
scr_setfontcolor(COR_OURO);
scr_setXY(18, 2); scr_printf("=== ESCOLHA SUA CLASSE (MODO HARDCORE) ===");
scr_setfontcolor(sel == 0 ? COR_OURO : COR_BRANCO);
scr_setXY(8, 5);  scr_printf("%s 1. CAVALEIRO DE ASTORA", sel == 0 ? "-->" : "   ");
scr_setfontcolor(COR_CINZA);
scr_setXY(12, 6);  scr_printf("VIG:14 END:12 STR:12 INT:8  | Armadura Completa + Espada");
scr_setfontcolor(sel == 1 ? COR_LARANJA : COR_BRANCO);
scr_setXY(8, 9);  scr_printf("%s 2. PIROMANTE DO PANTANO", sel == 1 ? "-->" : "   ");
scr_setfontcolor(COR_CINZA);
scr_setXY(12, 10); scr_printf("VIG:10 END:10 STR:10 INT:16 | Manto Leve + Resinas de Fogo");
scr_setfontcolor(sel == 2 ? COR_ROXO : COR_BRANCO);
scr_setXY(8, 13); scr_printf("%s 3. MERCENARIO DE CARIM", sel == 2 ? "-->" : "   ");
scr_setfontcolor(COR_CINZA);
scr_setXY(12, 14); scr_printf("VIG:11 END:15 STR:13 INT:9  | Broquel Parry + Arco de Caca");
scr_setfontcolor(sel == 3 ? COR_CIANO : COR_BRANCO);
scr_setXY(8, 17); scr_printf("%s 4. DESPOJADO (BONECO DE PALITO)", sel == 3 ? "-->" : "   ");
scr_setfontcolor(COR_CINZA);
scr_setXY(12, 18); scr_printf("VIG:11 END:14 STR:14 INT:11 | Sem Armadura (Fast Roll + Agilidade)");
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
scr_setXY(13, 20); scr_printf("Pressione (X), (Triangulo) ou (O) para voltar ao Menu...");
scr_setfontcolor(COR_BRANCO);
esperar_botao(PAD_CROSS | PAD_TRIANGLE | PAD_CIRCLE);
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
const char* nome_armadura(int id) {
if (id == 0) return "NENHUMA (PALITO)";
if (id == 1) return "COURO LEVE";
if (id == 2) return "ACO DE ASTORA";
return "ARMADURA CINZAS";
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
        int usar_save = tela_menu_principal(tem_save);

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
        int elmo_atual = 0, peito_atual = 0, perna_atual = 0, armaduras_mask = 3;
        int npc_sol_prog = 0, npc_ceb_prog = 0, npc_mag_prog = 0;
        int poca_area = 0, poca_sala = 0, poca_x = 0, poca_y = 0, poca_almas = 0;

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
            elmo_atual = save.elmo_atual;
            peito_atual = save.peito_atual;
            perna_atual = save.perna_atual;
            armaduras_mask = save.armaduras_mask | 3;
            npc_sol_prog = save.npc_sol_prog;
            npc_ceb_prog = save.npc_ceb_prog;
            npc_mag_prog = save.npc_mag_prog;
            poca_area = save.poca_area;
            poca_sala = save.poca_sala;
            poca_x = save.poca_x;
            poca_y = save.poca_y;
            poca_almas = save.poca_almas;
        } else {
            classe = tela_escolher_classe();
            nivel = 1; almas = 0; custo_nivel = 60;
            st_vig = (classe == 0) ? 14 : ((classe == 1) ? 10 : 11);
            st_end = (classe == 2) ? 15 : ((classe == 3) ? 14 : ((classe == 0) ? 12 : 10));
            st_str = (classe == 3) ? 14 : ((classe == 2) ? 13 : ((classe == 0) ? 12 : 10));
            st_int = (classe == 1) ? 16 : ((classe == 3) ? 11 : ((classe == 2) ? 9 : 8));
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
            if (classe == 0) { elmo_atual = 2; peito_atual = 2; perna_atual = 2; armaduras_mask = 7; }
            else if (classe == 1 || classe == 2) { elmo_atual = 1; peito_atual = 1; perna_atual = 1; armaduras_mask = 3; }
            else { elmo_atual = 0; peito_atual = 0; perna_atual = 0; armaduras_mask = 3; }
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
        int px = 12, py = 15, p_dir = 1;
        int roll_timer = 0, roll_dir = 1;
        int atk_timer = 0, heavy_atk = 0;
        int combo = 0, combo_timer = 0;
        int saque_timer = 0, arma_sacada = 0;
        int parry_timer = 0, defendendo = 0;
        int resina_timer = 0, veneno_timer = 0;
        int raio_timer = 0, raio_x = 0, raio_y = 14;
        int flecha_ativa = 0, fx = 0, fy = 0, f_dir = 1;
        int getsuga_ativa = 0, gx = 0, gy = 0, g_dir = 1;
        int hitstop = 0, shake = 0;

        /* Inimigo 1 */
        int ex = 58, ey = 14, ehp = 0, ehp_max = 15, e_dir = -1;
        int e_postura = 0, e_postura_max = 14;
        int e_sangramento = 0, e_stun = 0;
        int e_burn = 0, e_chill = 0, e_poison = 0;
        int e_arquetipo = 0, e_ai_estado = 0, e_ai_timer = 0;
        int e_proj_ativo = 0, epx = 0, epy = 15, e_proj_tipo = 0, e_proj_dir = -1;

        /* Inimigo 2 (Duplas e Lacaios de Chefe) */
        int e2_ex = 64, e2_ey = 16, e2_hp = 0, e2_hp_max = 15, e2_dir = -1;
        int e2_timer = 0, e2_arq = 2;

        /* Mecanicas Soulslike e NPCs Aleatorios */
        int invasor_ativo = 0, invasor_estus = 1;
        int aliado_ativo = 0, aliado_x = 18, aliado_y = 15, aliado_hp = 0, aliado_timer = 0;
        int mimico_ativo = 0;
        int npc_sol_r = 1, npc_sol_s = 1;
        int npc_ceb_r = 2, npc_ceb_s = 2;
        int npc_mag_r = 3, npc_mag_s = 2;

        int tipo_chefe = 0, eh_subchefe = 0, chefe_fase = 1;
        int onda_ativa = 0, onda_x = 0, onda_y = 15, onda_dir = -1;
        int raio_chefe_timer = 0, rc_x = 0, rc_y = 15;
        int frame = 0;

        sprintf(fala, usar_save ? "Save carregado! Use (START) para Equipar Armaduras e Armas." :
                                  "Santuario: Guardia (esq), Fogueira (centro), Ferreiro Andre (dir).");

        while (!jogo_zerado && !reiniciar) {
            frame++;
            atualizar_musica(frame, area, tipo_chefe, invasor_ativo);

            hp_max = (st_vig * 3) + (anel_equipado == 1 ? 18 : 0);
            mp_max = st_int * 2;
            stam_max = 40 + (st_end * 5);
            int atk_base = st_str / 2;
            int mag_bonus = st_int / 3;
            int peso_arm = elmo_atual + peito_atual + perna_atual;
            int def_arm = peso_arm / 2;

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
            if (saque_timer > 0) saque_timer--;
            if (combo_timer > 0) combo_timer--;
            else combo = 0;

            if (roll_timer == 0 && atk_timer == 0 && !defendendo && stam < stam_max) {
                int reg = (anel_equipado == 3) ? 9 : 6;
                if (peso_arm == 0) reg += 2;
                stam += reg;
                if (stam > stam_max) stam = stam_max;
            }

            /* === MENU DE PAUSA E ARMADURAS (START) === */
            if (new_pad & PAD_START) {
                int cursor = 0;
                int pausado = 1;
                old_pad = paddata;
                scr_clear();
                while (pausado) {
                    if (padGetState(0, 0) == PAD_STATE_STABLE) {
                        if (padRead(0, 0, &buttons) != 0) {
                            paddata = 0xffff ^ buttons.btns;
                            new_pad = paddata & ~old_pad;
                            old_pad = paddata;
                        }
                    }

                    if ((new_pad & PAD_UP) && cursor > 0) { cursor--; tocar_som(0); }
                    if ((new_pad & PAD_DOWN) && cursor < 8) { cursor++; tocar_som(0); }
                    if (new_pad & PAD_START) pausado = 0;

                    if (new_pad & PAD_CROSS) {
                        tocar_som(2);
                        if (cursor == 0) pausado = 0;
                        else if (cursor == 1) {
                            for (int k = 1; k <= 6; k++) {
                                int cand = (arma_atual + k) % 6;
                                if (armas_mask & (1 << cand)) { arma_atual = cand; saque_timer = 4; arma_sacada = 1; break; }
                            }
                        } else if (cursor == 2) {
                            for (int k = 1; k <= 3; k++) {
                                int cand = (escudo_atual + k) % 3;
                                if (escudos_mask & (1 << cand)) { escudo_atual = cand; break; }
                            }
                        } else if (cursor == 3) {
for (int k = 1; k <= 4; k++) {
int cand = (elmo_atual + k) % 4;
if (cand == 0 || (armaduras_mask & (1 << (cand - 1)))) { elmo_atual = cand; break; }
}
} else if (cursor == 4) {
for (int k = 1; k <= 4; k++) {
int cand = (peito_atual + k) % 4;
if (cand == 0 || (armaduras_mask & (1 << (cand - 1)))) { peito_atual = cand; break; }
}
} else if (cursor == 5) {
for (int k = 1; k <= 4; k++) {
int cand = (perna_atual + k) % 4;
if (cand == 0 || (armaduras_mask & (1 << (cand - 1)))) { perna_atual = cand; break; }
}
} else if (cursor == 6) {
for (int k = 1; k <= 5; k++) {
int cand = (runa_equipada + k) % 5;
if (runas_mask & (1 << cand)) { runa_equipada = cand; break; }
}
} else if (cursor == 7) {
for (int k = 1; k <= 5; k++) {
int cand = (anel_equipado + k) % 5;
if (aneis_mask & (1 << cand)) { anel_equipado = cand; break; }
}
} else if (cursor == 8) {
reiniciar = 1;
pausado = 0;
}
}
peso_arm = elmo_atual + peito_atual + perna_atual;
scr_setfontcolor(COR_OURO);
scr_setXY(10, 1);  scr_printf("+==========================================================+");
scr_setXY(10, 2);  scr_printf("|        EQUIPAMENTO, ARMADURAS E STATUS (SOULS)           |");
scr_setXY(10, 3);  scr_printf("+----------------------------------------------------------+");
scr_setfontcolor(COR_BRANCO);
scr_setXY(10, 4);  scr_printf("| Nv:%-2d Almas:%-5d | Peso:%d/9 (%s) | Def:+%-2d      |",
nivel, almas, peso_arm, peso_arm == 0 ? "PALITO/LEVE" : (peso_arm <= 4 ? "MEDIO" : "PESADO"), peso_arm / 2);
scr_setfontcolor(COR_OURO);
scr_setXY(10, 5);  scr_printf("+----------------------------------------------------------+");
scr_setfontcolor(cursor == 0 ? COR_OURO : COR_BRANCO);
scr_setXY(10, 6);  scr_printf("| %s Continuar Jornada                                    |", cursor == 0 ? "-->" : "   ");
scr_setfontcolor(cursor == 1 ? COR_CIANO : COR_BRANCO);
scr_setXY(10, 7);  scr_printf("| %s Arma:    %-18s (X troca)                  |", cursor == 1 ? "-->" : "   ", nome_arma(arma_atual));
scr_setfontcolor(cursor == 2 ? COR_VERDE : COR_BRANCO);
scr_setXY(10, 8);  scr_printf("| %s Escudo:  %-18s (X troca)                  |", cursor == 2 ? "-->" : "   ", nome_escudo(escudo_atual));
scr_setfontcolor(cursor == 3 ? COR_AMARELO : COR_BRANCO);
scr_setXY(10, 9);  scr_printf("| %s Elmo:    %-18s (X equipa/tira)            |", cursor == 3 ? "-->" : "   ", nome_armadura(elmo_atual));
scr_setfontcolor(cursor == 4 ? COR_AMARELO : COR_BRANCO);
scr_setXY(10, 10); scr_printf("| %s Peito:   %-18s (X equipa/tira)            |", cursor == 4 ? "-->" : "   ", nome_armadura(peito_atual));
scr_setfontcolor(cursor == 5 ? COR_AMARELO : COR_BRANCO);
scr_setXY(10, 11); scr_printf("| %s Grevas:  %-18s (X equipa/tira)            |", cursor == 5 ? "-->" : "   ", nome_armadura(perna_atual));
scr_setfontcolor(cursor == 6 ? COR_LARANJA : COR_BRANCO);
scr_setXY(10, 12); scr_printf("| %s Runa:    %-18s (X equipa)                 |", cursor == 6 ? "-->" : "   ", nome_runa(runa_equipada));
scr_setfontcolor(cursor == 7 ? COR_ROXO : COR_BRANCO);
scr_setXY(10, 13); scr_printf("| %s Anel:    %-18s (X equipa)                 |", cursor == 7 ? "-->" : "   ", nome_anel(anel_equipado));
scr_setfontcolor(cursor == 8 ? COR_VERMELHO : COR_BRANCO);
scr_setXY(10, 14); scr_printf("| %s Voltar ao Menu Principal                             |", cursor == 8 ? "-->" : "   ");
scr_setfontcolor(COR_OURO);
scr_setXY(10, 15); scr_printf("+----------------------------------------------------------+");
scr_setXY(10, 16); scr_printf("| (Cima/Baixo) Mover | (X) Alterar | START Voltar          |");
scr_setXY(10, 17); scr_printf("+==========================================================+");
scr_setfontcolor(COR_BRANCO);
usleep(40000);
}
scr_clear();
continue;
}
/* Trocar Arma com Animacao de Saque (L1) */
if (new_pad & PAD_L1) {
for (int k = 1; k <= 6; k++) {
int cand = (arma_atual + k) % 6;
if (armas_mask & (1 << cand)) { arma_atual = cand; break; }
}
saque_timer = 5;
arma_sacada = 1;
tocar_som(4);
sprintf(fala, "Sacou: %s +%d!", nome_arma(arma_atual), arma_nv);
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
hp += 16 + st_vig + (npc_sol_prog >= 2 ? 4 : 0);
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
/* Defesa e Parry (R1) - cancela ataque */
if (new_pad & PAD_R1) {
if (escudo_atual != 2 && stam >= 10) {
atk_timer = 0;
parry_timer = (escudo_atual == 0) ? 8 : 5;
stam -= 10;
tocar_som(0);
}
}
defendendo = ((paddata & PAD_R1) && stam > 5 && roll_timer == 0);
/* Rolamento (X) - Fast Roll se estiver leve/palito */
if ((new_pad & PAD_CROSS) && roll_timer == 0) {
int custo_roll = (peso_arm == 0) ? 14 : (peso_arm <= 4 ? 18 : 24);
if (stam >= custo_roll) {
atk_timer = 0;
stam -= custo_roll;
roll_timer = (peso_arm == 0) ? 7 : 6;
if (paddata & PAD_LEFT) { roll_dir = -1; p_dir = -1; }
else if (paddata & PAD_RIGHT) { roll_dir = 1; p_dir = 1; }
else roll_dir = p_dir;
tocar_som(0);
} else {
sprintf(fala, "Sem Vigor para rolar!");
}
}
/* Movimentacao 2.5D Livre (Olha e anda para ambos os lados sem travar) */
if (roll_timer > 0) {
int passo_roll = (peso_arm == 0) ? 3 : 2;
px += (roll_dir * passo_roll);
if (px < 2) px = 2;
if (px > 71) px = 71;
} else {
int vel = defendendo ? 1 : 2;
if (paddata & PAD_LEFT) {
p_dir = -1;
if (px > 2) px -= vel;
}
if (paddata & PAD_RIGHT) {
p_dir = 1;
if (px < 71) px += vel;
}
if ((paddata & PAD_UP) && py > 11) py -= 1;
if ((paddata & PAD_DOWN) && py < 17) py += 1;
}
/* === PROGRESSAO DE 30 SALAS (5 REINOS x 6 SALAS) === */
            if (area == 0 && px >= 70) {
                area = 1; sala = 1; px = 4; p_dir = 1;
                horda_restante = 3;
                tipo_chefe = 0; eh_subchefe = 0; e_arquetipo = 0;
                invasor_ativo = 0; mimico_ativo = 0; aliado_ativo = 0;
                ex = 58; ey = 14; e_dir = -1;
                ehp_max = (18 + (nivel * 3)) * (opt_pesadelo ? 2 : 1); ehp = ehp_max;
                e2_hp = 0;
                e_stun = 0; e_burn = 0; e_chill = 0; e_poison = 0; e_sangramento = 0;
                e_ai_estado = 0; e_ai_timer = 0; e_proj_ativo = 0;
                sprintf(fala, "Reino 1 - Sala 1/6: Acenda a Fogueira do Reino com (Triang)!");
            } else if (area > 0 && px <= 2) {
                e_proj_ativo = 0; onda_ativa = 0; raio_chefe_timer = 0; getsuga_ativa = 0;
                invasor_ativo = 0; mimico_ativo = 0; e2_hp = 0;
                if (sala > 1) {
                    sala--; px = 66; p_dir = -1; horda_restante = 0; ehp = 0; tipo_chefe = 0; eh_subchefe = 0;
                    sprintf(fala, "Recuou para a Sala %d/6 do Reino %d.", sala, area);
                } else {
                    area--; px = 66; p_dir = -1;
                    if (area == 0) {
                        sala = 1; horda_restante = 0; ehp = 0; tipo_chefe = 0; aliado_ativo = 0;
                        sprintf(fala, "Voltou ao Santuario do Fogo.");
                    } else {
                        sala = 6; horda_restante = 0; ehp = 0; tipo_chefe = 0;
                        sprintf(fala, "Voltou para o Reino %d - Sala 6/6.", area);
                    }
                }
            } else if (area > 0 && px >= 70) {
                if (horda_restante > 0 || ehp > 0 || e2_hp > 0 || mimico_ativo) {
                    px = 69;
                    sprintf(fala, "Nevoa selada! Derrote todos os inimigos desta sala primeiro!");
                } else if (sala < 6) {
                    sala++; px = 4; p_dir = 1;
                    e_stun = 0; e_burn = 0; e_chill = 0; e_poison = 0; e_sangramento = 0;
                    e_ai_estado = 0; e_ai_timer = 0; e_proj_ativo = 0;
                    invasor_ativo = 0; mimico_ativo = 0; e2_hp = 0;
                    if (aliado_ativo) { aliado_x = 10; aliado_y = 15; }

                    if (sala == 3) {
                        horda_restante = 0;
                        if (!(subchefes_mask & (1 << area))) {
                            tipo_chefe = area; eh_subchefe = 1; chefe_fase = 1;
                            ex = 58; ey = 14; e_dir = -1;
                            ehp_max = (75 + (area * 42)) * (opt_pesadelo ? 2 : 1); ehp = ehp_max;
                            sprintf(fala, "SUB-CHEFE: %s guarda a passagem do Reino %d!", nome_guardiao(area, 1), area);
                        } else {
                            tipo_chefe = 0; eh_subchefe = 0; ehp = 0;
                            sprintf(fala, "Sala 3/6 (Sub-Chefe ja derrotado). Siga em frente!");
                        }
                    } else if (sala == 6) {
                        horda_restante = 0;
                        if (chefes_mortos < area) {
                            tipo_chefe = area; eh_subchefe = 0; chefe_fase = 1;
                            ex = 58; ey = 14; e_dir = -1;
                            ehp_max = (80 + (area * 88)) * (opt_pesadelo ? 2 : 1); ehp = ehp_max;
                            sprintf(fala, "LORDE GUARDIAO: %s despertou! (Sinal de Aliado no chao!)", nome_guardiao(area, 0));
                        } else {
                            tipo_chefe = 0; eh_subchefe = 0; ehp = 0;
                            sprintf(fala, "Santuario do Lorde purificado. Avance para o proximo Reino!");
                        }
                    } else {
                        /* Chance de Invasao de Espirito Sombrio (Red Phantom) nas salas 2, 4 ou 5 */
                        int seed_inv = (frame + area * 17 + sala * 31) % 10;
                        if (seed_inv < 3 && (sala == 2 || sala == 5)) {
                            invasor_ativo = 1; invasor_estus = 1;
                            horda_restante = 1;
                            tipo_chefe = 0; eh_subchefe = 0; e_arquetipo = 4;
                            ex = 60; ey = 14; e_dir = -1;
                            ehp_max = (55 + (area * 28) + (nivel * 4)) * (opt_pesadelo ? 2 : 1);
                            ehp = ehp_max;
                            tocar_som(1);
                            sprintf(fala, "INVASAO! ESPIRITO SOMBRIO DE KIRK INVADIU SEU MUNDO!");
                        } else {
                            horda_restante = (sala == 2) ? 3 : ((sala == 4) ? 4 : 5);
                            tipo_chefe = 0; eh_subchefe = 0;
                            e_arquetipo = (area + sala) % 5;
                            ex = 56; ey = 13; e_dir = -1;
                            ehp_max = (16 + (area * 8) + (sala * 2) + (nivel * 3)) * (opt_pesadelo ? 2 : 1);
                            ehp = ehp_max;

                            /* Combate em Dupla (2 inimigos simultaneos) em varias salas */
                            if (sala == 2 || sala == 4 || sala == 5) {
                                e2_ex = 64; e2_ey = 16; e2_dir = -1;
                                e2_arq = (e_arquetipo + 2) % 5;
                                e2_hp_max = (ehp_max * 75) / 100;
                                e2_hp = e2_hp_max;
                                if (horda_restante > 1) horda_restante--;
                            }
                            sprintf(fala, "Reino %d - Sala %d/6: Emboscada em Dupla! %s",
                                area, sala, (sala == 4) ? "(Bau misterioso nesta sala!)" : "");
                        }
                    }
                } else if (sala == 6 && area < 5) {
                    area++; sala = 1; px = 4; p_dir = 1;
                    horda_restante = 3;
                    tipo_chefe = 0; eh_subchefe = 0;
                    invasor_ativo = 0; mimico_ativo = 0; e2_hp = 0;
                    e_arquetipo = area % 5;
                    ex = 58; ey = 14; e_dir = -1;
                    ehp_max = (18 + (area * 8) + (nivel * 3)) * (opt_pesadelo ? 2 : 1); ehp = ehp_max;
                    e_stun = 0; e_ai_estado = 0; e_ai_timer = 0; e_proj_ativo = 0;
                    sprintf(fala, "Entrou no Reino %d/5 - Sala 1/6! Acenda a Fogueira local!", area);
                }
            }

            /* Veneno ambiental no Reino 2 */
            if (area == 2 && frame % 85 == 0 && roll_timer == 0 && veneno_timer == 0) {
                veneno_timer = 150;
                sprintf(fala, "Gases toxicos do Pantano te envenenaram! Use Antidoto!");
            }

            /* Ataque Normal (Quadrado) - Combo 3 Hits + Saque Rapido */
            if ((new_pad & PAD_SQUARE) && atk_timer <= 1 && roll_timer == 0) {
                int custo_stam = (arma_atual == 1 || arma_atual == 3) ? 18 : 10;
                if (peso_arm == 0 && custo_stam > 8) custo_stam -= 2;
                if (stam >= custo_stam) {
                    stam -= custo_stam;
                    heavy_atk = 0;
                    if (!arma_sacada) { arma_sacada = 1; saque_timer = 3; }
                    combo = (combo % 3) + 1;
                    combo_timer = 18;
                    px += (p_dir * 2);
                    if (px < 2) px = 2;
                    if (px > 71) px = 71;
                    tocar_som(combo == 3 ? 4 : 0);
                    if (arma_atual == 1 || arma_atual == 3) atk_timer = 5;
                    else if (arma_atual == 2) {
                        atk_timer = 3;
                        if (!flecha_ativa) { flecha_ativa = 1; f_dir = p_dir; fx = px + (p_dir * 5); fy = py; }
                    } else atk_timer = 3;
                } else sprintf(fala, "Vigor insuficiente!");
            }

            /* === BOTAO TRIANGULO: POCA, SINAL ALIADO, FOGUEIRA, FERREIRO, NPCS, BAUS/MIMICO OU PESADO === */
            if (new_pad & PAD_TRIANGLE) {
                int abriu_menu = 0;

                /* 0A. RECUPERAR POCA DE SANGUE (ALMAS PERDIDAS) */
                if (!abriu_menu && poca_almas > 0 && area == poca_area && sala == poca_sala && abs_val(px - poca_x) <= 4) {
                    abriu_menu = 1;
                    almas += poca_almas;
                    tocar_som(5);
                    sprintf(fala, "ALMAS RECUPERADAS! +%d Almas resgatadas da poca de sangue!", poca_almas);
                    poca_almas = 0; poca_area = 0; poca_sala = 0;
                }

                /* 0B. INVOCAR ESPECTRO ALIADO NO SINAL DOURADO (SALA 6) */
                if (!abriu_menu && area > 0 && sala == 6 && !aliado_ativo && px >= 10 && px <= 22 && mp >= 4) {
                    abriu_menu = 1;
                    mp -= 4;
                    aliado_ativo = 1;
                    aliado_x = px + 4; aliado_y = py;
                    aliado_hp = 60 + (area * 25);
                    aliado_timer = 600;
                    tocar_som(5);
                    sprintf(fala, "ESPECTRO DOURADO INVOCADO! Cavaleiro aliado entrou na luta!");
                }

                /* 1. SENTAR NA FOGUEIRA */
                if (!abriu_menu && ((area == 0 && px >= 18 && px <= 31) || (area > 0 && sala == 1 && px >= 12 && px <= 24))) {
                    abriu_menu = 1;
                    fogueiras_mask |= (1 << area);
                    hp = hp_max; mp = mp_max; stam = stam_max; frascos = frascos_max; veneno_timer = 0;
                    arma_sacada = 0;

                    /* Sorteia novas salas aleatorias para os NPCs viajantes */
                    npc_sol_r = 1 + ((frame + nivel) % 4);
                    npc_sol_s = 1 + ((frame * 3) % 5);
                    if (npc_sol_s == 3) npc_sol_s = 2;

                    npc_ceb_r = 1 + ((frame + chefes_mortos + 1) % 4);
                    npc_ceb_s = 2 + ((frame * 5) % 4);
if (npc_ceb_s == 3) npc_ceb_s = 4;
npc_mag_r = 2 + ((frame + almas) % 3);
npc_mag_s = 2 + ((frame * 7) % 4);
if (npc_mag_s == 3) npc_mag_s = 5;
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
save.elmo_atual = elmo_atual; save.peito_atual = peito_atual; save.perna_atual = perna_atual;
save.armaduras_mask = armaduras_mask;
save.npc_sol_prog = npc_sol_prog; save.npc_ceb_prog = npc_ceb_prog; save.npc_mag_prog = npc_mag_prog;
save.poca_area = poca_area; save.poca_sala = poca_sala;
save.poca_x = poca_x; save.poca_y = poca_y; save.poca_almas = poca_almas;
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
horda_restante = 0; ehp = 0; e2_hp = 0; tipo_chefe = 0; eh_subchefe = 0;
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
/* 2. MENU DO FERREIRO ANDRE */
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
sprintf(fala, peso_arm == 0 ? "Andre: Haha! Vai encarar o abismo feito um palito? Tome cuidado!" :
"Andre: Que as chamas guiem sua lamina (+%d)!", arma_nv);
}
/* 3. GUARDIA DO FOGO E BAU DO SANTUARIO */
if (!abriu_menu && area == 0) {
int q0 = (quest_mask >> 0) & 3;
int q1 = (quest_mask >> 2) & 3;
int q2 = (quest_mask >> 4) & 3;
int q3 = (quest_mask >> 6) & 3;
if (px >= 4 && px <= 16) {
abriu_menu = 1;
if (q0 == 0) {
quest_mask = (quest_mask & ~3) | 1;
sprintf(fala, "Guardia: Ache o Coelho de Cinzas na Sala 4 da Floresta!");
} else if (q0 == 1 && (baus_mask & (1 << 1))) {
quest_mask = (quest_mask & ~3) | 2;
aneis_mask |= (1 << 2); anel_equipado = 2; tocar_som(5);
sprintf(fala, "Guardia: Reliquia recebida! Ganhou o ANEL DA FORCA!");
} else if (q1 == 0 && chefes_mortos >= 1) {
quest_mask = (quest_mask & ~(3 << 2)) | (1 << 2);
sprintf(fala, "Guardia: Ache a Erva Rara na Sala 4 do Pantano (Reino 2)!");
} else if (q1 == 1 && (baus_mask & (1 << 2))) {
quest_mask = (quest_mask & ~(3 << 2)) | (2 << 2);
frascos_max = 6; frascos = 6; tocar_som(5);
sprintf(fala, "Guardia: Chama reforçada! Frascos de Estus subiram para 6!");
} else if (q2 == 0 && chefes_mortos >= 2) {
quest_mask = (quest_mask & ~(3 << 4)) | (1 << 4);
sprintf(fala, "Guardia: Pegue o Cristal Solar na Sala 4 da Caverna!");
} else if (q2 == 1 && (baus_mask & (1 << 3))) {
quest_mask = (quest_mask & ~(3 << 4)) | (2 << 4);
aneis_mask |= (1 << 3); anel_equipado = 3; tocar_som(5);
sprintf(fala, "Guardia: Ganhou o ANEL CLORANTO (Vigor super rapido)!");
} else if (q3 == 0 && chefes_mortos >= 3) {
quest_mask = (quest_mask & ~(3 << 6)) | (1 << 6);
sprintf(fala, "Guardia: Pegue o Tomo Negro na Sala 4 das Catacumbas!");
} else if (q3 == 1 && (baus_mask & (1 << 4))) {
quest_mask = (quest_mask & ~(3 << 6)) | (2 << 6);
aneis_mask |= (1 << 4); anel_equipado = 4; tocar_som(5);
sprintf(fala, "Guardia: Ganhou o ANEL ARCANO (+Dano Magico)!");
} else {
sprintf(fala, "Guardia: Que a chama guie voce. Procure os viajantes nos Reinos!");
}
tocar_som(2);
} else if (!(baus_mask & 1) && px >= 59 && px <= 66) {
abriu_menu = 1;
baus_mask |= 1; aneis_mask |= (1 << 1); anel_equipado = 1; tocar_som(2);
sprintf(fala, "BAU ABERTO! Encontrou o ANEL VITAL (+18 HP Maximo)!");
}
}
/* 3B. CONVERSAR COM NPCS VIAJANTES ALEATORIOS NOS REINOS */
if (!abriu_menu && area > 0) {
if (area == npc_sol_r && sala == npc_sol_s && px >= 22 && px <= 33) {
abriu_menu = 1;
npc_sol_prog++;
tocar_som(5);
if (npc_sol_prog == 1) {
resinas += 2;
sprintf(fala, "Solaire: Louvado seja o Sol! \\[T]/ Tome estas Resinas de Fogo!");
} else if (npc_sol_prog == 2) {
armaduras_mask |= (1 << 2);
sprintf(fala, "Solaire: Minha armadura e forte! Ganhou ARMADURA DE ASTORA!");
} else {
almas += 150;
sprintf(fala, "Solaire: Eu encontrei meu proprio Sol! (+150 Almas)");
}
} else if (area == npc_ceb_r && sala == npc_ceb_s && px >= 38 && px <= 49) {
abriu_menu = 1;
npc_ceb_prog++;
tocar_som(5);
if (npc_ceb_prog == 1) {
elixires += 2; antidotos += 2;
sprintf(fala, "Cebolao: Hmmm... Oh! Desculpe, cochilei! Pegue estes Elixires!");
} else if (npc_ceb_prog == 2) {
escudos_mask |= (1 << 2);
sprintf(fala, "Cebolao: Um brinde a nossa amizade! Ganhou ESCUDO GRANDE!");
} else {
minerios += 3;
sprintf(fala, "Cebolao: Zzz... Oh! Tome estes +3 Minerios de Titanita!");
}
} else if (area == npc_mag_r && sala == npc_mag_s && px >= 46 && px <= 57) {
abriu_menu = 1;
npc_mag_prog++;
tocar_som(5);
if (npc_mag_prog == 1) {
st_int++;
sprintf(fala, "Feiticeiro: Sua mente se expande! (+1 Inteligencia permanente)");
} else {
armaduras_mask |= (1 << 3);
sprintf(fala, "Feiticeiro: Segredos das Cinzas! Ganhou ARMADURA DAS CINZAS!");
}
}
}
/* 4. ABRIR BAU DA SALA 4 (OU ACORDAR MIMICO!) OU DESFERIR GOLPE PESADO */
if (!abriu_menu && area > 0) {
if (sala == 4 && !(baus_mask & (1 << area)) && px >= 30 && px <= 42) {
if ((area == 2 || area == 4) && !mimico_ativo) {
mimico_ativo = 1;
ex = 36; ey = 14; e_dir = (px < ex) ? -1 : 1;
ehp_max = 65 + (area * 30);
ehp = ehp_max;
e_arquetipo = 4;
tocar_som(3);
sprintf(fala, "ARMADILHA! ERA UM BAU MIMICO! Ele acordou furioso!");
} else if (!mimico_ativo) {
baus_mask |= (1 << area); minerios += 3; elixires++; resinas++; antidotos++;
if (area == 3) escudos_mask |= (1 << 2);
tocar_som(2);
sprintf(fala, area == 3 ? "RELIQUIA + ESCUDO GRANDE DE FERRO coletados!" :
"RELIQUIA DA SALA 4 COLETADA! +3 Minerios e consumiveis!");
}
} else if (atk_timer == 0 && roll_timer == 0 && stam >= 26) {
stam -= 26;
atk_timer = 6;
heavy_atk = 1;
if (!arma_sacada) { arma_sacada = 1; saque_timer = 3; }
tocar_som(0);
if ((arma_atual == 4 || arma_atual == 3) && !getsuga_ativa) {
getsuga_ativa = 1; g_dir = p_dir; gx = px + (p_dir * 5); gy = py;
tocar_som(1);
}
}
}
/* Magia: Relampago (Circulo) */
            int custo_mp = (anel_equipado == 4) ? 2 : 3;
            if ((new_pad & PAD_CIRCLE) && raio_timer == 0 && mp >= custo_mp) {
                mp -= custo_mp;
                raio_timer = 6;
                raio_x = (area > 0 && ehp > 0) ? ex : (px + p_dir * 10);
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

            /* Movimento de Projeteis do Jogador (Esquerda e Direita) */
            if (flecha_ativa) {
                fx += (f_dir * 4);
                if (fx < 2 || fx > 74) flecha_ativa = 0;
            }
            if (getsuga_ativa) {
                gx += (g_dir * 3);
                if (gx < 2 || gx > 74) getsuga_ativa = 0;
                if (area > 0 && ehp > 0 && getsuga_ativa && abs_val(gy - ey) <= 2 && abs_val(gx - ex) <= 4) {
                    int d_get = atk_base + mag_bonus + arma_nv + 5;
                    ehp -= d_get;
                    getsuga_ativa = 0;
                    hitstop = 3;
                    tocar_som(1);
                    sprintf(fala, arma_atual == 4 ? "GETSUGA TENSHOU! Onda cortou o alvo (-%d HP)!" :
                                                    "ONDA COLOSSAL da Dragon Slayer (-%d HP)!", d_get);
                } else if (area > 0 && e2_hp > 0 && getsuga_ativa && abs_val(gy - e2_ey) <= 2 && abs_val(gx - e2_ex) <= 4) {
                    int d_get = atk_base + mag_bonus + arma_nv + 5;
                    e2_hp -= d_get;
                    getsuga_ativa = 0;
                    tocar_som(1);
                    sprintf(fala, "ONDA CORTANTE atingiu o segundo inimigo (-%d HP)!", d_get);
                }
            }

            /* Projeteis Inimigos (Bidirecionais) */
            if (e_proj_ativo) {
                epx += (e_proj_dir * ((e_proj_tipo == 1) ? 2 : 3));
                if (e_proj_tipo == 1 && frame % 3 == 0) { if (epy < py) epy++; else if (epy > py) epy--; }
                if (epx < 2 || epx > 74) e_proj_ativo = 0;
                if (e_proj_ativo && abs_val(epy - py) <= 1 && abs_val(epx - px) <= 2) {
                    if (parry_timer > 0) {
                        e_proj_ativo = 0;
                        if (ehp > 0) ehp -= (atk_base + arma_nv + 5);
                        tocar_som(4);
                        sprintf(fala, "PARRY! Voce rebateu o projetil de volta no inimigo!");
                    } else if (roll_timer == 0) {
                        int red_esc = (escudo_atual == 2) ? 1 : ((escudo_atual == 1) ? 2 : 3);
                        int d_proj = defendendo ? red_esc : (4 + area - def_arm);
                        if (d_proj < 1) d_proj = 1;
                        hp -= d_proj;
                        e_proj_ativo = 0;
                        tocar_som(3);
                        sprintf(fala, defendendo ? "Escudo bloqueou projetil (-%d HP)." : "Atingido a distancia (-%d HP)!", d_proj);
                    }
                }
            }
            if (onda_ativa) {
                onda_x += (onda_dir * 2);
                if (onda_x < 2 || onda_x > 74) onda_ativa = 0;
                if (onda_ativa && roll_timer == 0 && abs_val(py - onda_y) <= 1 && abs_val(px - onda_x) <= 2) {
                    hp -= (6 + area);
                    onda_ativa = 0;
                    tocar_som(3);
                    sprintf(fala, "Onda sismica te atingiu! Role por cima com (X)!");
                }
            }

            /* === ESPECTRO ALIADO (INVOCACAO DOURADA) === */
            if (area > 0 && aliado_ativo && aliado_hp > 0) {
                aliado_timer--;
                if (aliado_timer <= 0) { aliado_ativo = 0; sprintf(fala, "O Espectro Aliado retornou ao seu mundo."); }
                else if (ehp > 0) {
                    if (abs_val(aliado_x - ex) > 5) aliado_x += (aliado_x < ex) ? 1 : -1;
                    if (aliado_y < ey) aliado_y++; else if (aliado_y > ey) aliado_y--;
                    if (frame % 16 == 0 && abs_val(aliado_x - ex) <= 6 && abs_val(aliado_y - ey) <= 1) {
                        ehp -= (4 + area * 2);
                        tocar_som(0);
                    }
                }
            }

            /* === INTELIGENCIA ARTIFICIAL LIVRE (VIRA PARA OS DOIS LADOS + FLANQUEIO) === */
            if (area > 0 && ehp > 0) {
                if (e_burn > 0) { e_burn--; if (e_burn % 20 == 0) ehp -= 2; }
                if (e_poison > 0) { e_poison--; if (e_poison % 25 == 0) ehp -= 2; }
                if (e_chill > 0) e_chill--;

                /* Espirito Sombrio bebe Estus se ficar com pouca vida */
                if (invasor_ativo && invasor_estus > 0 && ehp <= (ehp_max * 35) / 100 && e_stun == 0) {
                    invasor_estus--;
                    ehp += (ehp_max * 40) / 100;
                    tocar_som(2);
                    sprintf(fala, "ESPIRITO SOMBRIO BEBEU ESTUS E RECUPEROU VIDA!");
                }

                if (tipo_chefe > 0 && !eh_subchefe) {
                    if (chefe_fase == 1 && ehp <= (ehp_max * 60) / 100) {
                        chefe_fase = 2; shake = 8; tocar_som(1);
                        if (e2_hp <= 0) {
                            e2_ex = (ex > 35) ? ex - 8 : ex + 8; e2_ey = 16;
                            e2_hp_max = 22 + area * 12; e2_hp = e2_hp_max; e2_arq = 0;
                        }
                        sprintf(fala, "FASE 2/3! %s despertou seu poder e invocou um Lacaio!", nome_guardiao(tipo_chefe, 0));
                    } else if (chefe_fase == 2 && ehp <= (ehp_max * 25) / 100) {
                        chefe_fase = 3; shake = 10; tocar_som(1);
                        sprintf(fala, "FASE FINAL 3/3! %s entrou em FRENESI MORTAL!", nome_guardiao(tipo_chefe, 0));
                    }
                }

                if (e_stun > 0) {
                    e_stun--;
                } else if (e_chill > 0 && frame % 2 == 0) {
                    /* Lentidao da Runa de Gelo */
                } else if (tipo_chefe == 0) {
                    int alvo_x = (aliado_ativo && aliado_hp > 0 && (frame % 60 < 30)) ? aliado_x : px;
                    int alvo_y = (aliado_ativo && aliado_hp > 0 && (frame % 60 < 30)) ? aliado_y : py;
                    int dx = alvo_x - ex;
                    int dist_x = abs_val(dx);
                    int flanco_y = alvo_y + (((frame / 30) % 2 == 0) ? -1 : 1);
                    if (flanco_y < 11) flanco_y = 11;
                    if (flanco_y > 17) flanco_y = 17;

                    if (e_ai_estado == 0) {
                        if (dx > 1) e_dir = 1;
                        else if (dx < -1) e_dir = -1;

                        if (e_arquetipo == 2 || e_arquetipo == 3) {
                            if (dist_x < 18) { ex -= e_dir; if (ex < 3) ex = 3; if (ex > 68) ex = 68; }
                            else if (dist_x > 30) ex += e_dir;
                            if (frame % 3 == 0) { if (ey < alvo_y) ey++; else if (ey > alvo_y) ey--; }
                            e_ai_timer++;
                            if (e_ai_timer >= 28 && abs_val(ey - alvo_y) <= 1) { e_ai_estado = 1; e_ai_timer = 0; }
                        } else {
                            int vel_e = (e_arquetipo == 4 || invasor_ativo) ? 2 : 3;
                            if (frame % vel_e == 0 && dist_x > 5) ex += e_dir;
                            if (frame % 3 == 0) { if (ey < flanco_y) ey++; else if (ey > flanco_y) ey--; }
                            if (dist_x <= 6 && abs_val(ey - alvo_y) <= 1) { e_ai_estado = 1; e_ai_timer = 0; }
                        }
                    } else if (e_ai_estado == 1) {
                        e_ai_timer++;
                        int tempo_tele = (e_arquetipo == 4 || invasor_ativo) ? 16 : 14;
                        if (e_ai_timer >= tempo_tele) { e_ai_estado = 2; e_ai_timer = 0; }
                    } else if (e_ai_estado == 2) {
                        if (e_arquetipo == 2 || e_arquetipo == 3) {
                            if (!e_proj_ativo) {
                                e_proj_ativo = 1; e_proj_dir = e_dir;
                                epx = ex + (e_dir * 3); epy = ey;
                                e_proj_tipo = (e_arquetipo == 3) ? 1 : 0;
                                tocar_som(1);
                            }
                        } else if (abs_val(px - ex) <= 6 && abs_val(py - ey) <= 1 && ((px - ex) * e_dir >= -1)) {
                            if (roll_timer > 0) {
                                /* Esquiva perfeita */
                            } else if (parry_timer > 0 && e_arquetipo != 4) {
                                e_stun = 24; ehp -= (atk_base + arma_nv + 8); e_postura += 10;
                                hitstop = 4; tocar_som(4);
                                sprintf(fala, "PARRY! Contra-ataque devastador!");
                            } else if (defendendo) {
                                int d_blk = (escudo_atual == 2) ? 0 : 1;
                                stam -= (escudo_atual == 2) ? 20 : 14;
                                hp -= d_blk; tocar_som(0);
                                sprintf(fala, d_blk == 0 ? "ESCUDO GRANDE bloqueou 100%%!" : "Escudo absorveu o golpe (-1 HP)!");
                            } else {
                                int d_e = ((e_arquetipo == 4 || invasor_ativo || mimico_ativo) ? (6 + area) : (3 + area)) - def_arm;
                                if (d_e < 1) d_e = 1;
                                hp -= d_e; tocar_som(3);
sprintf(fala, "Golpeado (-%d HP)! Leia o aviso ( ! )!", d_e);
}
} else if (aliado_ativo && aliado_hp > 0 && abs_val(aliado_x - ex) <= 6) {
aliado_hp -= (4 + area);
if (aliado_hp <= 0) { aliado_ativo = 0; sprintf(fala, "Seu Espectro Aliado foi derrotado!"); }
}
e_ai_estado = 3; e_ai_timer = 0;
} else if (e_ai_estado == 3) {
e_ai_timer++;
/* Recuo tatico apos atacar */
if (e_ai_timer < 6 && ex - e_dir > 3 && ex - e_dir < 68) ex -= e_dir;
if (e_ai_timer >= 15) { e_ai_estado = 0; e_ai_timer = 0; }
}
} else {
/* IA DOS SUB-CHEFES E LORDES (TAMBEM VIRAM PARA AMBOS OS LADOS!) */
int alvo_x = (aliado_ativo && aliado_hp > 0 && (frame % 50 < 25)) ? aliado_x : px;
int alvo_y = (aliado_ativo && aliado_hp > 0 && (frame % 50 < 25)) ? aliado_y : py;
int dx = alvo_x - ex;
int dist_x = abs_val(dx);
int vel_c = (chefe_fase == 3) ? 1 : ((chefe_fase == 2) ? 2 : 3);
if (e_ai_estado == 0) {
if (dx > 1) e_dir = 1;
else if (dx < -1) e_dir = -1;
if (tipo_chefe == 2 && !eh_subchefe) {
if (dist_x < 18) { ex -= e_dir; if (ex < 4) ex = 4; if (ex > 66) ex = 66; }
else if (dist_x > 34) ex += e_dir;
} else if (frame % vel_c == 0 && dist_x > 5) ex += e_dir;
if (frame % 3 == 0) { if (ey < alvo_y) ey++; else if (ey > alvo_y) ey--; }
e_ai_timer++;
int espera = (chefe_fase == 3) ? 14 : ((chefe_fase == 2) ? 22 : 28);
if (e_ai_timer >= espera) { e_ai_estado = 1; e_ai_timer = 0; }
} else if (e_ai_estado == 1) {
e_ai_timer++;
int tele = (chefe_fase == 3) ? 13 : 17;
if (e_ai_timer >= tele) { e_ai_estado = 2; e_ai_timer = 0; }
} else if (e_ai_estado == 2) {
if (tipo_chefe == 2) {
if (!e_proj_ativo) { e_proj_ativo = 1; e_proj_dir = e_dir; epx = ex + (e_dir * 3); epy = ey; e_proj_tipo = 1; tocar_som(1); }
if (chefe_fase >= 2 && raio_chefe_timer == 0) { raio_chefe_timer = 16; rc_x = px; rc_y = py; }
} else if (tipo_chefe == 3 || chefe_fase == 3) {
if (!onda_ativa) { onda_ativa = 1; onda_dir = e_dir; onda_x = ex + (e_dir * 3); onda_y = ey; shake = 5; tocar_som(3); }
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
int d_g = ((chefe_fase * 2) + 3 + area) - def_arm;
if (d_g < 1) d_g = 1;
hp -= d_g; tocar_som(3);
sprintf(fala, "Impacto devastador (-%d HP)!", d_g);
}
} else if (aliado_ativo && aliado_hp > 0 && abs_val(aliado_x - ex) <= 7) {
aliado_hp -= (6 + area * 2);
if (aliado_hp <= 0) { aliado_ativo = 0; sprintf(fala, "Seu Espectro Aliado caiu em batalha!"); }
}
e_ai_estado = 3; e_ai_timer = 0;
} else if (e_ai_estado == 3) {
e_ai_timer++;
int exaustao = (chefe_fase == 3) ? 12 : 16;
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
}
/* === IA DO SEGUNDO INIMIGO (DUPLAS E LACAIOS) === */
if (area > 0 && e2_hp > 0) {
e2_timer++;
int dx2 = px - e2_ex;
e2_dir = (dx2 >= 0) ? 1 : -1;
if (frame % 3 == 0 && abs_val(dx2) > 6) e2_ex += e2_dir;
if (frame % 4 == 0) { if (e2_ey < py) e2_ey++; else if (e2_ey > py) e2_ey--; }
if (e2_timer >= 36) {
e2_timer = 0;
if (abs_val(px - e2_ex) <= 6 && abs_val(py - e2_ey) <= 1 && roll_timer == 0) {
int d2 = defendendo ? 1 : ((2 + area) - def_arm);
if (d2 < 1) d2 = 1;
hp -= d2; tocar_som(3);
sprintf(fala, "Atacado pelo segundo inimigo (-%d HP)!", d2);
} else if ((e2_arq == 2 || e2_arq == 3) && !e_proj_ativo) {
e_proj_ativo = 1; e_proj_dir = e2_dir; epx = e2_ex + (e2_dir * 2); epy = e2_ey; e_proj_tipo = 0;
}
}
}
/* Acerto de Flecha */
if (flecha_ativa && area > 0 && ehp > 0 && abs_val(fy - ey) <= 1 && abs_val(fx - ex) <= 3) {
flecha_ativa = 0;
int d_a = atk_base + arma_nv + 3 + (anel_equipado == 2 ? 3 : 0) + (resina_timer > 0 ? 4 : 0);
ehp -= d_a; e_postura += 4; hitstop = 2; tocar_som(0);
if (runa_equipada == 1) e_burn = 80;
if (runa_equipada == 2) e_poison = 120;
if (runa_equipada == 3) e_chill = 90;
if (runa_equipada == 4 && hp < hp_max) hp++;
sprintf(fala, "Flecha acertou (-%d HP)!", d_a);
} else if (flecha_ativa && area > 0 && e2_hp > 0 && abs_val(fy - e2_ey) <= 1 && abs_val(fx - e2_ex) <= 3) {
flecha_ativa = 0;
int d_a = atk_base + arma_nv + 3;
e2_hp -= d_a; tocar_som(0);
if (e2_hp <= 0) { almas += 25 + area * 6; sprintf(fala, "Segundo inimigo abatido com flecha!"); }
}
/* Acerto Corpo a Corpo (Bidirecional + Combo 3 Hits + Knockback!) */
int alcance = (arma_atual == 3 || arma_atual == 5) ? 11 : 9;
if (area > 0 && atk_timer >= 2) {
int acertou_e1 = (ehp > 0 && abs_val(py - ey) <= 2 && ((p_dir == 1 && ex >= px && ex - px <= alcance) || (p_dir == -1 && ex <= px && px - ex <= alcance)));
int acertou_e2 = (e2_hp > 0 && abs_val(py - e2_ey) <= 2 && ((p_dir == 1 && e2_ex >= px && e2_ex - px <= alcance) || (p_dir == -1 && e2_ex <= px && px - e2_ex <= alcance)));
if (acertou_e2) {
atk_timer = 1;
int d2 = atk_base + arma_nv + 3 + (combo == 3 ? 4 : 0);
e2_hp -= d2;
e2_ex += (p_dir * 4);
if (e2_ex < 3) e2_ex = 3;
if (e2_ex > 68) e2_ex = 68;
tocar_som(0);
if (e2_hp <= 0) { almas += 28 + area * 6; sprintf(fala, "Segundo inimigo da dupla abatido (+Almas)!"); }
else sprintf(fala, "Acertou segundo inimigo (-%d HP)!", d2);
}
if (acertou_e1) {
atk_timer = 1;
if (e_arquetipo == 1 && !heavy_atk && combo != 3 && e_stun == 0 && tipo_chefe == 0 && runa_equipada != 1 && arma_atual != 5 && (p_dir != e_dir)) {
tocar_som(0);
sprintf(fala, "ESCUDO BLOQUEOU! Use 3o golpe do Combo, Pesado ou ataque pelas costas!");
} else {
int mult = heavy_atk ? 2 : 1;
int bonus_combo = (!heavy_atk && combo == 3) ? (4 + arma_nv) : 0;
int bonus_arma = (arma_atual == 3) ? 5 : ((arma_atual == 4 || arma_atual == 5) ? 3 : 1);
int bonus = bonus_arma + bonus_combo + (anel_equipado == 2 ? 3 : 0) + (resina_timer > 0 ? 4 : 0);
int d_m = (atk_base + arma_nv + bonus) * mult;
ehp -= d_m;
e_postura += (heavy_atk || combo == 3 || arma_atual == 1 || arma_atual == 3) ? 8 : 4;
hitstop = (heavy_atk || combo == 3) ? 3 : 1;
if (heavy_atk || combo == 3 || arma_atual == 3) shake = 4;
tocar_som(combo == 3 ? 4 : 0);
/* Recuo (Knockback) na direcao do golpe */
int recuo = (heavy_atk || combo == 3) ? 5 : 2;
if (tipo_chefe > 0) recuo = (heavy_atk || combo == 3) ? 2 : 1;
ex += (p_dir * recuo);
if (ex < 3) ex = 3;
if (ex > 68) ex = 68;
if (tipo_chefe == 0) { e_ai_estado = 0; e_ai_timer = 0; }
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
sprintf(fala, "Corte da Foice [Hit %d/3] (-%d HP) [Sangue: %d/4]", combo, d_m, e_sangramento);
}
} else if (e_postura >= e_postura_max) {
e_postura = 0; e_stun = 30;
int d_crit = d_m + 12;
ehp -= d_crit;
tocar_som(4);
sprintf(fala, "POSTURA QUEBRADA! GOLPE CRITICO (-%d HP)!", d_crit);
} else if (heavy_atk) {
sprintf(fala, "GOLPE PESADO (-%d HP)!", d_m);
} else if (combo == 3) {
sprintf(fala, "FINALIZADOR DE COMBO [3/3] (-%d HP)!", d_m);
} else {
sprintf(fala, "Combo [%d/3] com %s (-%d HP)!", combo, nome_arma(arma_atual), d_m);
}
}
}
/* === MORTE DO INIMIGO, MIMICO, INVASOR, SUB-CHEFE OU LORDE === */
if (ehp <= 0 && (acertou_e1 || raio_timer > 0 || flecha_ativa || getsuga_ativa || e_burn > 0 || e_poison > 0 || aliado_ativo)) {
int eh_boss = (tipo_chefe > 0);
int ganho_almas = eh_boss ? (eh_subchefe ? 95 : 210) : (mimico_ativo ? 140 : (invasor_ativo ? 160 : (32 + area * 8)));
almas += ganho_almas;
minerios += eh_boss ? (eh_subchefe ? 2 : 4) : ((mimico_ativo || invasor_ativo) ? 2 : 1);
tocar_som(eh_boss || mimico_ativo || invasor_ativo ? 5 : 2);
if (mimico_ativo) {
mimico_ativo = 0; ehp = 0;
baus_mask |= (1 << area);
minerios += 3; elixires += 2; resinas += 2;
if (area == 2) armaduras_mask |= (1 << 2);
if (area == 4) armaduras_mask |= (1 << 3);
sprintf(fala, "MIMICO DERROTADO! Ganhou Reliquia + Armadura Rara + %d Almas!", ganho_almas);
} else if (invasor_ativo) {
invasor_ativo = 0; horda_restante = 0; ehp = 0;
frascos = frascos_max;
sprintf(fala, "ESPIRITO SOMBRIO BANIDO (+%d Almas)! Estus restaurados!", ganho_almas);
} else if (eh_boss && eh_subchefe) {
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
tipo_chefe = 0; ehp = 0; e2_hp = 0; aliado_ativo = 0;
onda_ativa = 0; raio_chefe_timer = 0;
if (area == 5) {
jogo_zerado = 1;
break;
}
sprintf(fala, "LORDE VENCIDO! Ganhou GRANDE ALMA + PEDRA RUNICA DE %s!", nome_runa(area));
} else {
if (horda_restante > 0) horda_restante--;
if (horda_restante > 0) {
ex = (px < 38) ? 65 : 10; ey = 14; e_dir = (px < ex) ? -1 : 1;
e_stun = 0; e_ai_estado = 0; e_postura = 0;
e_burn = 0; e_chill = 0; e_poison = 0; e_sangramento = 0;
e_arquetipo = (e_arquetipo + 1) % 5;
ehp_max = (16 + (area * 8) + (sala * 2) + (nivel * 3)) * (opt_pesadelo ? 2 : 1);
ehp = ehp_max;
sprintf(fala, "Inimigo abatido (+%d Almas)! Restam %d na horda!", ganho_almas, horda_restante);
} else {
ehp = 0;
sprintf(fala, e2_hp > 0 ? "Primeiro alvo caiu! Elimine o segundo inimigo!" : "SALA PURIFICADA (+%d Almas)! Siga em frente!", ganho_almas);
}
}
}
}
/* Morte do Jogador -> Cria Poca de Sangue no local da morte! */
if (area > 0 && hp <= 0) {
mortes++;
poca_area = area;
poca_sala = sala;
poca_x = px;
poca_y = py;
poca_almas = almas;
almas = 0;
area = 0; sala = 1; horda_restante = 0; ehp = 0; e2_hp = 0;
invasor_ativo = 0; mimico_ativo = 0; aliado_ativo = 0;
px = 12; py = 15; p_dir = 1;
hp = hp_max; mp = mp_max; stam = stam_max; frascos = frascos_max;
veneno_timer = 0; tipo_chefe = 0; eh_subchefe = 0; onda_ativa = 0; raio_chefe_timer = 0;
tocar_som(3);
sprintf(fala, "VOCE MORREU... %d Almas ficaram na Poca de Sangue (Reino %d Sala %d)!", poca_almas, poca_area, poca_sala);
}
/* Garante vitoria caso o inimigo morra por Relampago, Veneno ou Aliado */
            if (area > 0 && ehp <= 0 && (tipo_chefe > 0 || invasor_ativo || mimico_ativo || horda_restante > 0)) {
                int eh_boss = (tipo_chefe > 0);
                int ganho_almas = eh_boss ? (eh_subchefe ? 95 : 210) : (mimico_ativo ? 140 : (invasor_ativo ? 160 : (32 + area * 8)));
                almas += ganho_almas;
                minerios += eh_boss ? (eh_subchefe ? 2 : 4) : 1;
                tocar_som(eh_boss ? 5 : 2);
                if (mimico_ativo) { mimico_ativo = 0; baus_mask |= (1 << area); sprintf(fala, "MIMICO DERROTADO (+%d Almas)!", ganho_almas); }
                else if (invasor_ativo) { invasor_ativo = 0; horda_restante = 0; frascos = frascos_max; sprintf(fala, "ESPIRITO SOMBRIO BANIDO (+%d Almas)!", ganho_almas); }
                else if (eh_boss && eh_subchefe) { subchefes_mask |= (1 << area); tipo_chefe = 0; eh_subchefe = 0; sprintf(fala, "SUB-CHEFE VENCIDO (+%d Almas)!", ganho_almas); }
                else if (eh_boss && !eh_subchefe) {
                    if (chefes_mortos < area) chefes_mortos = area;
                    almas_chefe_mask |= (1 << area);
                    if (area <= 4) { runas_mask |= (1 << area); runa_equipada = area; }
                    tipo_chefe = 0; e2_hp = 0; aliado_ativo = 0; onda_ativa = 0; raio_chefe_timer = 0;
                    if (area == 5) { jogo_zerado = 1; break; }
                    sprintf(fala, "LORDE VENCIDO! Ganhou GRANDE ALMA + PEDRA RUNICA!");
                } else {
                    if (horda_restante > 0) horda_restante--;
                    if (horda_restante > 0) {
                        ex = (px < 38) ? 65 : 10; ey = 14; e_dir = (px < ex) ? -1 : 1;
                        ehp_max = (16 + (area * 8) + (sala * 2) + (nivel * 3)) * (opt_pesadelo ? 2 : 1);
                        ehp = ehp_max;
                        sprintf(fala, "Inimigo abatido (+%d Almas)! Restam %d!", ganho_almas, horda_restante);
                    } else sprintf(fala, "SALA PURIFICADA (+%d Almas)!", ganho_almas);
                }
            }

            if (atk_timer > 0) atk_timer--;
            if (raio_timer > 0) raio_timer--;
            if (roll_timer > 0) roll_timer--;
            if (parry_timer > 0) parry_timer--;
            int sx = (opt_shake && shake > 0) ? ((shake-- % 2 == 0) ? 1 : -1) : 0;
            if (!opt_shake) shake = 0;

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
            if (area > 0 && (horda_restante > 0 || e2_hp > 0)) {
                scr_printf("ARMA:%s(+%d) | PESO:%d/9 | HORDA:%d | (START)Equip",
                    nome_arma(arma_atual), arma_nv, peso_arm, horda_restante + (e2_hp > 0 ? 1 : 0));
            } else {
                scr_printf("ARMA:%s(+%d) | PESO:%d/9 | MIN:%d | (START)Equip",
                    nome_arma(arma_atual), arma_nv, peso_arm, minerios);
            }

            /* Horizonte */
            scr_setfontcolor(COR_CINZA);
            scr_setXY(0, 8);
            scr_printf("..---/\\----...   ...---/\\----...   ...---/\\---...");
            scr_setXY(0, 10);
            scr_printf("------------------------------------------------------------------------------");

            /* Cenarios e NPCs */
            if (area == 0) {
                scr_setfontcolor(COR_ROXO);
                scr_setXY(6, 12);  scr_printf(" (._.) ");
                scr_setXY(6, 13);  scr_printf("GUARDIA");
                scr_setfontcolor(COR_LARANJA);
                scr_setXY(21, 11); scr_printf(frame % 4 < 2 ? "  ( | )  " : "  ) | (  ");
                scr_setXY(21, 12); scr_printf("   (|)   ");
                scr_setfontcolor(COR_OURO);
                scr_setXY(20, 13); scr_printf("(FOGUEIRA)");
                scr_setfontcolor(COR_CIANO);
                scr_setXY(45, 11); scr_printf(" (B_B) ");
                scr_setXY(45, 12); scr_printf(" |FORJA| ");
                scr_setXY(45, 13); scr_printf(" [ANDRE] ");
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
                if (sala == 4 && !mimico_ativo) {
                    int bau_aberto = (baus_mask & (1 << area));
                    scr_setfontcolor(bau_aberto ? COR_CINZA : COR_OURO);
                    scr_setXY(34, 11); scr_printf(bau_aberto ? "(___)" : "(=*=)");
                    scr_setXY(34, 12); scr_printf(" BAU ");
                }
                if (sala == 6 && !aliado_ativo && chefes_mortos < area) {
                    scr_setfontcolor(COR_OURO);
                    scr_setXY(13, 16); scr_printf("[ + ] SINAL ALIADO");
                }
                if (poca_almas > 0 && area == poca_area && sala == poca_sala) {
                    scr_setfontcolor(COR_VERDE);
                    scr_setXY(poca_x, poca_y); scr_printf("( * )");
                }
                /* NPCs Viajantes Aleatorios */
                if (area == npc_sol_r && sala == npc_sol_s) {
                    scr_setfontcolor(COR_AMARELO);
                    scr_setXY(25, 11); scr_printf(" \\[T]/ ");
                    scr_setXY(25, 12); scr_printf("SOLAIRE");
                }
                if (area == npc_ceb_r && sala == npc_ceb_s) {
                    scr_setfontcolor(COR_BRANCO);
                    scr_setXY(41, 11); scr_printf(" ((O)) ");
                    scr_setXY(41, 12); scr_printf("CEBOLAO");
                }
                if (area == npc_mag_r && sala == npc_mag_s) {
                    scr_setfontcolor(COR_CIANO);
                    scr_setXY(49, 11); scr_printf(" /^\\o_o");
                    scr_setXY(49, 12); scr_printf(" MAGO  ");
                }

                scr_setfontcolor(COR_OURO);
                scr_setXY(2, 14); scr_printf("<- VOLTAR");
                int sala_limpa = (horda_restante == 0 && ehp <= 0 && e2_hp <= 0 && !mimico_ativo);
                scr_setfontcolor(sala_limpa ? COR_VERDE : COR_CINZA);
                scr_setXY(64, 14);
                if (!sala_limpa) scr_printf("(BLOQUEADO)");
                else if (sala < 6) scr_printf("SALA %d ->", sala + 1);
                else if (area < 5) scr_printf("REINO %d ->", area + 1);
            }

            /* Linha de Mira e Marcas */
            if (area > 0 && ehp > 0 && tipo_chefe == 0 && e_arquetipo == 2 && e_ai_estado == 1) {
                scr_setfontcolor(COR_AMARELO);
                int ini_m = (px < ex) ? px + 5 : ex + 5;
                int fim_m = (px < ex) ? ex : px;
                for (int mx = ini_m; mx < fim_m; mx += 3) {
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

            /* === DESENHO DO JOGADOR (ARMADURAS / PALITO + ANIMACAO DE SAQUE E ESPADA + 2 LADOS) === */
            int jx = px + sx;
            if (jx < 1) jx = 1;
            scr_setfontcolor(parry_timer > 0 ? COR_OURO : (roll_timer > 0 ? COR_CIANO : (resina_timer > 0 ? COR_LARANJA : COR_BRANCO)));
            if (roll_timer > 0) {
                scr_setXY(jx, py - 1); scr_printf(" .-. ");
                scr_setXY(jx, py);     scr_printf("( @ )");
            } else {
                /* Cabeca (Elmo ou Palito) */
                scr_setXY(jx, py - 2);
                if (elmo_atual == 0) scr_printf("  o  ");
                else if (elmo_atual == 1) scr_printf(" ^o^ ");
                else scr_printf(" [O] ");

                /* Tronco + Animacao da Espada/Saque para Direita ou Esquerda */
                const char *corpo = (peito_atual == 0) ? "-|-" : (peito_atual == 1 ? "(#)" : "[#]");
                scr_setXY(jx, py - 1);
if (p_dir == 1) {
if (parry_timer > 0)       scr_printf("%s!PARRY", corpo);
else if (defendendo)       scr_printf(escudo_atual == 2 ? "%s|DEF " : "%s|-DEF", corpo);
else if (saque_timer > 0)  scr_printf(saque_timer > 2 ? "/%s   " : "%s|/  ", corpo);
else if (!arma_sacada)     scr_printf("/%s   ", corpo);
else if (atk_timer > 0) {
if (heavy_atk)         scr_printf(atk_timer > 3 ? "\\%s   " : "%s|===>>", corpo);
else if (combo == 1)   scr_printf(atk_timer > 1 ? "%s|===>" : "%s|---)", corpo);
else if (combo == 2)   scr_printf(atk_timer > 1 ? "%s|///'" : "%s|---'", corpo);
else                   scr_printf(atk_timer > 1 ? "%s|====>>" : "%s|===", corpo);
} else {
scr_printf(arma_atual == 3 ? "%s|=== " : "%s|--  ", corpo);
}
} else {
int jx_esq = (jx > 4) ? jx - 4 : 1;
scr_setXY(jx_esq, py - 1);
if (parry_timer > 0)       scr_printf("PARRY!%s", corpo);
else if (defendendo)       scr_printf(escudo_atual == 2 ? " DEF|%s" : "DEF-|%s", corpo);
else if (saque_timer > 0)  scr_printf(saque_timer > 2 ? "   %s\\ " : "  \\|%s", corpo);
else if (!arma_sacada)     scr_printf("   %s\\", corpo);
else if (atk_timer > 0) {
if (heavy_atk)         scr_printf(atk_timer > 3 ? "   %s/" : "<<===|%s", corpo);
else if (combo == 1)   scr_printf(atk_timer > 1 ? "<===|%s" : "(---|%s", corpo);
else if (combo == 2)   scr_printf(atk_timer > 1 ? "\\\\\\|%s" : "---|%s", corpo);
else                   scr_printf(atk_timer > 1 ? "<<====|%s" : "===|%s", corpo);
} else {
scr_printf(arma_atual == 3 ? " ===|%s" : "  --|%s", corpo);
}
}
/* Pernas (Grevas ou Palito) */
scr_setXY(jx, py);
int andando = (paddata & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN)) && (frame % 4 < 2);
if (perna_atual == 0) scr_printf(andando ? " / \\ " : " | | ");
else scr_printf(andando ? " / | " : " | | ");
}
/* === DESENHO DO INIMIGO 1 (VIRA PARA OS DOIS LADOS) === */
if (area > 0 && ehp > 0) {
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
scr_setfontcolor(invasor_ativo ? COR_VERMELHO : (mimico_ativo ? COR_OURO : (e_stun > 0 ? COR_CIANO : (e_burn > 0 ? COR_LARANJA : (chefe_fase >= 2 ? COR_ROXO : COR_VERMELHO)))));
if (mimico_ativo) {
scr_setXY(ix, ey - 2); scr_printf(" [VVVVV] ");
scr_setXY(ix, ey - 1); scr_printf(" [^^^^^] ");
scr_setXY(ix, ey);     scr_printf(frame % 4 < 2 ? " / | | \\ " : " | / \\ | ");
} else if (tipo_chefe > 0) {
scr_setXY(ix, ey - 3); scr_printf(eh_subchefe ? "  .---.  " : "  .===.  ");
scr_setXY(ix, ey - 2); scr_printf(e_stun > 0 ? " (x _ x) " : " (O _ O) ");
scr_setXY(ix, ey - 1); scr_printf(e_dir == -1 ? "<|#####| " : " |#####|>");
scr_setXY(ix, ey);     scr_printf("  /   |  ");
} else {
scr_setXY(ix, ey - 2);
scr_printf(invasor_ativo ? "  [X_X]  " : (e_arquetipo == 3 ? "  /^^^\\  " : "  .---.  "));
scr_setXY(ix, ey - 1);
if (e_dir == -1) scr_printf(e_arquetipo == 1 ? "||-|###| " : "<--|###| ");
else             scr_printf(e_arquetipo == 1 ? " |###|-||" : " |###|-->");
scr_setXY(ix, ey);
scr_printf(frame % 6 < 3 ? "  / |    " : "  | \\    ");
}
}
/* === DESENHO DO INIMIGO 2 (DUPLA / LACAIO) === */
if (area > 0 && e2_hp > 0) {
scr_setfontcolor(COR_LARANJA);
scr_setXY(e2_ex, e2_ey - 2); scr_printf(" .-. ");
scr_setXY(e2_ex, e2_ey - 1); scr_printf(e2_dir == -1 ? "<-|#| " : " |#|->");
scr_setXY(e2_ex, e2_ey);     scr_printf(" / \\ ");
}
/* === DESENHO DO ESPECTRO ALIADO INVOCADO === */
if (area > 0 && aliado_ativo && aliado_hp > 0) {
scr_setfontcolor(COR_OURO);
scr_setXY(aliado_x, aliado_y - 2); scr_printf(" [T] ");
scr_setXY(aliado_x, aliado_y - 1); scr_printf("(#)|=>");
scr_setXY(aliado_x, aliado_y);     scr_printf(" | | ");
}
/* Projeteis na Tela */
if (flecha_ativa) { scr_setfontcolor(COR_OURO); scr_setXY(fx, fy - 1); scr_printf(f_dir == 1 ? "--->" : "<---"); }
if (getsuga_ativa) { scr_setfontcolor(COR_CIANO); scr_setXY(gx, gy - 1); scr_printf(g_dir == 1 ? ")))>" : "<((("); }
if (e_proj_ativo) {
scr_setfontcolor(e_proj_tipo == 1 ? COR_ROXO : COR_VERMELHO);
scr_setXY(epx, epy - 1); scr_printf(e_proj_tipo == 1 ? "(**)" : (e_proj_dir == -1 ? "<---" : "--->"));
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
else if (area == 2) { scr_setfontcolor(COR_LARANJA); scr_printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"); }
else if (area == 3) { scr_setfontcolor(COR_CIANO);   scr_printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^"); }
else if (area == 4) { scr_setfontcolor(COR_CINZA);   scr_printf("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+="); }
else                { scr_setfontcolor(COR_ROXO);    scr_printf("=============================================================================="); }
/* Barra de Vida do Chefe, Invasor ou Mimico */
if (area > 0 && ehp > 0 && (tipo_chefe > 0 || invasor_ativo || mimico_ativo)) {
int cheios = (ehp * 24) / ehp_max;
if (cheios < 0) cheios = 0;
if (cheios > 24) cheios = 24;
for (int b = 0; b < 24; b++) *(barra + b) = (b < cheios) ? '#' : '.';
*(barra + 24) = '\0';
scr_setfontcolor(invasor_ativo ? COR_VERMELHO : (chefe_fase >= 2 ? COR_ROXO : COR_OURO));
scr_setXY(2, 19);
if (invasor_ativo) scr_printf("ESPIRITO SOMBRIO KIRK: ");
else if (mimico_ativo) scr_printf("BAU MIMICO VORAZ: ");
else if (eh_subchefe) scr_printf("%s: ", nome_guardiao(tipo_chefe, 1));
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
if (jogo_zerado && !reiniciar) {
tela_historia_final(nivel, arma_nv, mortes);
}
}
return 0;
}
