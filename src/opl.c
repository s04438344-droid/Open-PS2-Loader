#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>
#include <gsKit.h>
#include <dmaKit.h>

static GSGLOBAL *gsGlobal;

static struct {
    u64 a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p;
    u64 q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F;
} padBuf __attribute__((aligned(64)));

#define C_CEU      GS_SETREG_RGBAQ(0x14, 0x18, 0x36, 0x80, 0x00)
#define C_SOL      GS_SETREG_RGBAQ(0xFF, 0x55, 0x22, 0x80, 0x00)
#define C_MONTE    GS_SETREG_RGBAQ(0x22, 0x1C, 0x38, 0x80, 0x00)
#define C_TATAME   GS_SETREG_RGBAQ(0x3B, 0x28, 0x20, 0x80, 0x00)
#define C_FAIXA_CH GS_SETREG_RGBAQ(0x8C, 0x62, 0x39, 0x80, 0x00)
#define C_SOMBRA   GS_SETREG_RGBAQ(0x0A, 0x08, 0x12, 0x60, 0x00)
#define C_PELE     GS_SETREG_RGBAQ(0xF5, 0xC2, 0x9B, 0x80, 0x00)
#define C_KIMONO1  GS_SETREG_RGBAQ(0xEE, 0xEE, 0xF5, 0x80, 0x00)
#define C_FAIXA1   GS_SETREG_RGBAQ(0xEE, 0x22, 0x22, 0x80, 0x00)
#define C_KIMONO2  GS_SETREG_RGBAQ(0x22, 0x44, 0xAA, 0x80, 0x00)
#define C_FAIXA2   GS_SETREG_RGBAQ(0xFF, 0xCC, 0x11, 0x80, 0x00)
#define C_GOLPE    GS_SETREG_RGBAQ(0xFF, 0xEE, 0x33, 0x80, 0x00)
#define C_ENERGIA  GS_SETREG_RGBAQ(0x33, 0xDD, 0xFF, 0x80, 0x00)
#define C_VIDA     GS_SETREG_RGBAQ(0xEE, 0xCC, 0x22, 0x80, 0x00)
#define C_FUNDO_HP GS_SETREG_RGBAQ(0x88, 0x11, 0x11, 0x80, 0x00)

static float abs_f(float v) { return (v < 0.0f) ? -v : v; }

static void retangulo(float x, float y, float w, float h, u64 cor) {
    gsKit_prim_sprite(gsGlobal, x, y, x + w, y + h, 1, cor);
}

static void desenhar_lutador(float x, float y, int dir, int golpe, int pulando, u64 cor_roupa, u64 cor_faixa) {
    float dist_chao = 370.0f - y;
    float s_larg = 34.0f - (dist_chao * 0.12f);
    if (s_larg < 10.0f) s_larg = 10.0f;
    retangulo(x - s_larg, 366.0f, s_larg * 2.0f, 8.0f, C_SOMBRA);

    float abertura = pulando ? 18.0f : 11.0f;
    gsKit_prim_triangle(gsGlobal, x - 4, y - 38, x - abertura - 6, y, x - abertura + 5, y, 1, cor_roupa);
    if (golpe == 2) {
        gsKit_prim_triangle(gsGlobal, x + 4, y - 38, x + (dir * 52.0f), y - 36, x + (dir * 48.0f), y - 24, 1, cor_roupa);
        gsKit_prim_triangle(gsGlobal, x + (dir * 48.0f), y - 38, x + (dir * 62.0f), y - 30, x + (dir * 48.0f), y - 22, 1, C_GOLPE);
    } else {
        gsKit_prim_triangle(gsGlobal, x + 4, y - 38, x + abertura - 5, y, x + abertura + 6, y, 1, cor_roupa);
    }

    gsKit_prim_quad(gsGlobal, x - 16, y - 78, x + 16, y - 78, x - 10, y - 38, x + 10, y - 38, 1, cor_roupa);
    retangulo(x - 11.0f, y - 43.0f, 22.0f, 6.0f, cor_faixa);
    retangulo(x - 9.0f, y - 98.0f, 18.0f, 18.0f, C_PELE);
    retangulo(x - 10.0f, y - 94.0f, 20.0f, 4.0f, cor_faixa);

    if (golpe == 1) {
        float bx = (dir == 1) ? x : (x - 48.0f);
        retangulo(bx, y - 72.0f, 48.0f, 9.0f, C_PELE);
        gsKit_prim_triangle(gsGlobal, x + (dir * 46), y - 78, x + (dir * 60), y - 67, x + (dir * 46), y - 56, 1, C_GOLPE);
    } else {
        retangulo(x + (dir * 10.0f) - 4.0f, y - 74.0f, 8.0f, 18.0f, C_PELE);
        retangulo(x - (dir * 10.0f) - 4.0f, y - 70.0f, 8.0f, 16.0f, C_PELE);
    }
}

int main(void) {
    struct padButtonStatus buttons;
    u32 pad = 0, old_pad = 0, new_pad = 0;

    /* Inicializa sistema e controles do PS2 */
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, &padBuf);

    /* Inicializa placa de video (gsKit) a 60 FPS */
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsGlobal = gsKit_init_global();
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_init_screen(gsGlobal);
    gsKit_mode_switch(gsGlobal, GS_ONESHOT);

    float p1_x = 180.0f, p1_y = 370.0f, p1_vy = 0.0f;
    int p1_hp = 100, p1_golpe = 0, p1_timer = 0, p1_hit = 0;
    float p2_x = 460.0f, p2_y = 370.0f;
    int p2_hp = 100, p2_golpe = 0, p2_timer = 0, p2_cd = 30;
    float bola_x = 0.0f, bola_y = 0.0f, bola_vx = 0.0f;
    int bola_ativa = 0, bola_dono = 0, shake = 0;

    while (1) {
        if (padGetState(0, 0) == PAD_STATE_STABLE && padRead(0, 0, &buttons) != 0) {
            pad = 0xFFFF ^ buttons.btns;
            new_pad = pad & ~old_pad;
            old_pad = pad;
        }
        if (shake > 0) shake--;

        int p1_dir = (p1_x <= p2_x) ? 1 : -1;
        int p2_dir = -p1_dir;

        if (p1_timer > 0) {
            p1_timer--;
            if (p1_timer == 0) p1_golpe = 0;
        } else {
            if (pad & PAD_LEFT)  p1_x -= 4.2f;
            if (pad & PAD_RIGHT) p1_x += 4.2f;
            if ((new_pad & (PAD_UP | PAD_CROSS)) && p1_y >= 370.0f) p1_vy = -13.0f;
            if (new_pad & PAD_SQUARE) { p1_golpe = 1; p1_timer = 14; p1_hit = 1; }
            else if (new_pad & PAD_TRIANGLE) { p1_golpe = 2; p1_timer = 20; p1_hit = 1; }
            else if ((new_pad & PAD_CIRCLE) && !bola_ativa) {
                bola_ativa = 1; bola_dono = 1;
                bola_x = p1_x + (p1_dir * 35.0f); bola_y = p1_y - 62.0f; bola_vx = p1_dir * 7.5f;
                p1_golpe = 1; p1_timer = 16;
            }
        }

        p1_vy += 0.72f;
        p1_y += p1_vy;
        if (p1_y >= 370.0f) { p1_y = 370.0f; p1_vy = 0.0f; }

        if (p2_timer > 0) {
            p2_timer--;
            if (p2_timer == 0) p2_golpe = 0;
        } else {
            float dist = abs_f(p2_x - p1_x);
            if (dist > 62.0f) {
                p2_x += p2_dir * 3.0f;
                if (dist > 240.0f && !bola_ativa && p2_cd == 0) {
                    bola_ativa = 1; bola_dono = 2;
                    bola_x = p2_x + (p2_dir * 35.0f); bola_y = p2_y - 62.0f; bola_vx = p2_dir * 7.0f;
                    p2_golpe = 1; p2_timer = 16; p2_cd = 60;
                }
            } else if (p2_cd == 0) {
                p2_golpe = (p2_hp < 40) ? 2 : 1;
                p2_timer = 18; p2_cd = 42;
                if (abs_f(p1_y - p2_y) < 35.0f) {
                    int defendendo = (p1_dir == 1 && (pad & PAD_LEFT)) || (p1_dir == -1 && (pad & PAD_RIGHT));
                    p1_hp -= defendendo ? 2 : (p2_golpe == 2 ? 14 : 9);
                    p1_x -= p1_dir * 14.0f;
                    shake = 4;
                }
            }
        }
        if (p2_cd > 0) p2_cd--;

        if (p1_hit && p1_timer == 10) {
            p1_hit = 0;
            float alcance = (p1_golpe == 2) ? 68.0f : 62.0f;
            if (abs_f(p1_x - p2_x) <= alcance && abs_f(p1_y - p2_y) < 40.0f) {
                p2_hp -= (p1_golpe == 2) ? 15 : 10;
                p2_x += p1_dir * (p1_golpe == 2 ? 26.0f : 14.0f);
                shake = 5;
            }
        }

        if (bola_ativa) {
            bola_x += bola_vx;
            if (bola_x < 10.0f || bola_x > 630.0f) bola_ativa = 0;
            else if (bola_dono == 1 && abs_f(bola_x - p2_x) < 24.0f && abs_f(bola_y - (p2_y - 50.0f)) < 38.0f) {
                p2_hp -= 16; p2_x += p1_dir * 20.0f; bola_ativa = 0; shake = 6;
            } else if (bola_dono == 2 && abs_f(bola_x - p1_x) < 24.0f && abs_f(bola_y - (p1_y - 50.0f)) < 38.0f) {
                p1_hp -= 14; p1_x -= p1_dir * 20.0f; bola_ativa = 0; shake = 6;
            }
        }

        if (p1_x < 40.0f) p1_x = 40.0f; if (p1_x > 600.0f) p1_x = 600.0f;
        if (p2_x < 40.0f) p2_x = 40.0f; if (p2_x > 600.0f) p2_x = 600.0f;

        if (p1_hp <= 0 || p2_hp <= 0) {
            p1_hp = 100; p2_hp = 100; p1_x = 180.0f; p2_x = 460.0f; bola_ativa = 0;
        }

        /* Desenho direto no Graphics Synthesizer (60 FPS) */
        float sx = (shake > 0) ? ((shake % 2 == 0) ? 4.0f : -4.0f) : 0.0f;
        gsKit_clear(gsGlobal, C_CEU);

        gsKit_prim_triangle(gsGlobal, 320 + sx, 70, 265 + sx, 165, 375 + sx, 165, 1, C_SOL);
        gsKit_prim_triangle(gsGlobal, 140 + sx, 150, 10 + sx, 320, 290 + sx, 320, 1, C_MONTE);
        gsKit_prim_triangle(gsGlobal, 490 + sx, 130, 330 + sx, 320, 635 + sx, 320, 1, C_MONTE);

        retangulo(0.0f + sx, 320.0f, 640.0f, 160.0f, C_TATAME);
        retangulo(0.0f + sx, 320.0f, 640.0f, 6.0f, C_FAIXA_CH);

        desenhar_lutador(p1_x + sx, p1_y, p1_dir, p1_golpe, p1_y < 370.0f, C_KIMONO1, C_FAIXA1);
        desenhar_lutador(p2_x + sx, p2_y, p2_dir, p2_golpe, p2_y < 370.0f, C_KIMONO2, C_FAIXA2);

        if (bola_ativa) {
            float d = (bola_vx > 0) ? 1.0f : -1.0f;
            gsKit_prim_triangle(gsGlobal, bola_x + d * 18 + sx, bola_y,
                                bola_x - d * 12 + sx, bola_y - 12,
                                bola_x - d * 12 + sx, bola_y + 12, 1, C_ENERGIA);
        }

        retangulo(30.0f, 24.0f, 240.0f, 18.0f, C_FUNDO_HP);
        retangulo(30.0f + (240.0f - (p1_hp * 240.0f) / 100.0f), 24.0f, (p1_hp * 240.0f) / 100.0f, 18.0f, C_VIDA);
        retangulo(370.0f, 24.0f, 240.0f, 18.0f, C_FUNDO_HP);
        retangulo(370.0f, 24.0f, (p2_hp * 240.0f) / 100.0f, 18.0f, C_VIDA);
        gsKit_prim_triangle(gsGlobal, 320, 18, 304, 44, 336, 44, 1, C_FAIXA1);

        gsKit_queue_exec(gsGlobal);
        gsKit_sync_flip(gsGlobal);
    }
    return 0;
}
