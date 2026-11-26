#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdlib.h>
#include <string.h>
#include <libpad.h>
#include <gsKit.h>
#include <dmaKit.h>

static u64 padBuf[32] __attribute__((aligned(64)));

/* Sprites 5x5 dos Naipes (0=Copas, 1=Ouros, 2=Espadas, 3=Paus) */
static const char NAIPES[4][5][6] = {
    { "01010", "11111", "11111", "01110", "00100" },
    { "00100", "01110", "11111", "01110", "00100" },
    { "00100", "01110", "11111", "00100", "01110" },
    { "01110", "11111", "01110", "00100", "01110" }
};

/* Sprites 3x5 dos Valores das Cartas (2..10, J, Q, K, A) */
static const char DIGITOS[15][5][4] = {
    { "000","000","000","000","000" },
    { "010","110","010","010","111" },
    { "111","001","111","100","111" },
    { "111","001","111","001","111" },
    { "101","101","111","001","001" },
    { "111","100","111","001","111" },
    { "111","100","111","101","111" },
    { "111","001","001","001","001" },
    { "111","101","111","101","111" },
    { "111","101","111","001","111" },
    { "101","101","010","101","101" },
    { "001","001","001","101","111" },
    { "111","101","101","111","001" },
    { "101","101","110","101","101" },
    { "010","101","111","101","101" }
};

typedef struct {
    int valor;
    int naipe;
    int sel;
} Carta;

void desenhar_pixel_art(GSGLOBAL *gs, float x, float y, float esc, int w, int h, int stride, const char *grade, u64 cor) {
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            if (grade[r * stride + c] == '1') {
                gsKit_prim_sprite(gs, x + c * esc, y + r * esc,
                                  x + (c + 1) * esc, y + (r + 1) * esc, 5, cor);
            }
        }
    }
}

void desenhar_carta(GSGLOBAL *gs, Carta c, float x, float y, int foco) {
    u64 c_borda = foco ? GS_SETREG_RGBAQ(0xFF, 0xCC, 0x22, 0x80, 0x00) : GS_SETREG_RGBAQ(0x15, 0x15, 0x15, 0x80, 0x00);
    u64 c_fundo = GS_SETREG_RGBAQ(0xF2, 0xEE, 0xE5, 0x80, 0x00);
    u64 c_naipe = (c.naipe < 2) ? GS_SETREG_RGBAQ(0xDD, 0x22, 0x22, 0x80, 0x00) : GS_SETREG_RGBAQ(0x18, 0x18, 0x22, 0x80, 0x00);

    gsKit_prim_sprite(gs, x + 4, y + 4, x + 80, y + 116, 2, GS_SETREG_RGBAQ(0x08, 0x20, 0x10, 0x80, 0x00));
    gsKit_prim_sprite(gs, x - 3, y - 3, x + 79, y + 115, 3, c_borda);
    gsKit_prim_sprite(gs, x, y, x + 76, y + 112, 4, c_fundo);

    desenhar_pixel_art(gs, x + 8, y + 8, 4.0f, 3, 5, 4, (const char *)DIGITOS[c.valor], c_naipe);
    desenhar_pixel_art(gs, x + 23, y + 44, 6.0f, 5, 5, 6, (const char *)NAIPES[c.naipe], c_naipe);
}

int avaliar_jogada(Carta mao[5]) {
    int cont[15] = {0};
    int soma = 0, pares = 0, trinca = 0, quadra = 0;
    for (int i = 0; i < 5; i++) {
        if (mao[i].sel) {
            cont[mao[i].valor]++;
            soma += mao[i].valor;
        }
    }
    if (soma == 0) return 0;
    for (int v = 2; v <= 14; v++) {
        if (cont[v] == 2) pares++;
        else if (cont[v] == 3) trinca = 1;
        else if (cont[v] == 4) quadra = 1;
    }
    if (quadra) return soma * 8 + 200;
    if (trinca && pares) return soma * 6 + 120;
    if (trinca) return soma * 4 + 75;
    if (pares >= 2) return soma * 3 + 45;
    if (pares == 1) return soma * 2 + 20;
    return soma;
}

int main(void) {
    struct padButtonStatus btn;
    u32 pad = 0, old = 0, np = 0;

    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, padBuf);

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    GSGLOBAL *gs = gsKit_init_global();
    gs->PSM = GS_PSM_CT32;
    gs->DoubleBuffering = GS_SETTING_ON;
    gs->ZBuffering = GS_SETTING_OFF;
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);

    Carta mao[5];
    int cursor = 0, pontos = 0, meta = 40, maos = 4, descartes = 3;
    srand(777);
    for (int i = 0; i < 5; i++) {
        mao[i].valor = 2 + (rand() % 13);
        mao[i].naipe = rand() % 4;
        mao[i].sel = 0;
    }

    u64 cor_mesa  = GS_SETREG_RGBAQ(0x14, 0x48, 0x26, 0x80, 0x00);
    u64 cor_borda = GS_SETREG_RGBAQ(0x3B, 0x22, 0x12, 0x80, 0x00);
    u64 cor_barra = GS_SETREG_RGBAQ(0x22, 0xCC, 0x55, 0x80, 0x00);
    u64 cor_ouro  = GS_SETREG_RGBAQ(0xFF, 0xCC, 0x22, 0x80, 0x00);
    u64 cor_verm  = GS_SETREG_RGBAQ(0xDD, 0x33, 0x33, 0x80, 0x00);

    while (1) {
        if (padGetState(0, 0) == PAD_STATE_STABLE && padRead(0, 0, &btn) != 0) {
            pad = 0xffff ^ btn.btns;
            np = pad & ~old;
            old = pad;
        }

        if ((np & PAD_LEFT) && cursor > 0) cursor--;
        if ((np & PAD_RIGHT) && cursor < 4) cursor++;
        if (np & PAD_CROSS) mao[cursor].sel = !mao[cursor].sel;

        /* Jogar Mao (Quadrado) */
        if ((np & PAD_SQUARE) && maos > 0) {
            int ganho = avaliar_jogada(mao);
            if (ganho > 0) {
                pontos += ganho;
                maos--;
                for (int i = 0; i < 5; i++) {
                    if (mao[i].sel) {
                        mao[i].valor = 2 + (rand() % 13);
                        mao[i].naipe = rand() % 4;
                        mao[i].sel = 0;
                    }
                }
                if (pontos >= meta) {
                    pontos = 0;
                    meta += 35;
                    maos = 4;
                    descartes = 3;
                }
            }
        }

        /* Descartar (Triangulo) */
        if ((np & PAD_TRIANGLE) && descartes > 0) {
            int trocou = 0;
            for (int i = 0; i < 5; i++) {
                if (mao[i].sel) {
                    mao[i].valor = 2 + (rand() % 13);
                    mao[i].naipe = rand() % 4;
                    mao[i].sel = 0;
                    trocou = 1;
                }
            }
            if (trocou) descartes--;
        }

        /* Reiniciar (START ou apos derrota) */
        if ((np & PAD_START) || (maos == 0 && pontos < meta)) {
            if (maos == 0 && pontos < meta && !(np & PAD_CROSS) && !(np & PAD_START)) {
                gsKit_clear(gs, cor_verm);
                gsKit_queue_exec(gs);
                gsKit_sync_flip(gs);
                continue;
            }
            pontos = 0;
            meta = 40;
            maos = 4;
            descartes = 3;
            for (int i = 0; i < 5; i++) {
                mao[i].valor = 2 + (rand() % 13);
                mao[i].naipe = rand() % 4;
                mao[i].sel = 0;
            }
        }

        /* Renderizacao Pixel Art */
        gsKit_clear(gs, cor_borda);
        gsKit_prim_sprite(gs, 24, 24, 616, 424, 1, cor_mesa);

        /* Barra de Meta de Pontos no Topo */
        gsKit_prim_sprite(gs, 60, 48, 580, 74, 2, cor_borda);
        float pct = (float)pontos / (float)meta;
        if (pct > 1.0f) pct = 1.0f;
        gsKit_prim_sprite(gs, 64, 52, 64 + (512.0f * pct), 70, 3, cor_barra);

        /* Fichas de Maos (Ouro) e Descartes (Vermelho) */
        for (int m = 0; m < maos; m++)
            gsKit_prim_sprite(gs, 64 + m * 36, 94, 90 + m * 36, 114, 3, cor_ouro);
        for (int d = 0; d < descartes; d++)
            gsKit_prim_sprite(gs, 470 + d * 36, 94, 496 + d * 36, 114, 3, cor_verm);

        /* Desenhar as 5 Cartas */
        for (int i = 0; i < 5; i++) {
            float cy = mao[i].sel ? 200.0f : 235.0f;
            desenhar_carta(gs, mao[i], 85.0f + i * 98.0f, cy, (i == cursor));
        }

        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}
