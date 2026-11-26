#include "include/opl.h"
#include "include/renderman.h"
#include "include/fntsys.h"
#include "include/pad.h"
#include <stdio.h>
#include <stdlib.h>
// Variaveis e funcoes exigidas pelo renderman.c e pad.c
int gVMode = 0;
int gXOff = 0;
int gYOff = 0;
int gOverscan = 0;
int gWideScreen = 0;
int KeyPressedOnce = 0;
int DisableCron = 0;
int gXSensitivity = 1;
int gYSensitivity = 1;



int main(int argc, char *argv) {
    // Inicializa apenas Video, Fontes e Controles do PS2 (sem interface do OPL)
    rmInit();
    fntInit();
    padInit(0);

    char *txt = (char *)malloc(128);
    u64 cBlk = GS_SETREG_RGBA(0x00,0x00,0x00,0x80), cW = GS_SETREG_RGBA(0xFF,0xFF,0xFF,0x80);
    u64 cG = GS_SETREG_RGBA(0x40,0xFF,0x40,0x80), cR = GS_SETREG_RGBA(0xFF,0x35,0x35,0x80);
    u64 cY = GS_SETREG_RGBA(0xFF,0xEE,0x40,0x80), cO = GS_SETREG_RGBA(0xFF,0x99,0x20,0x80);
    u64 cC = GS_SETREG_RGBA(0x40,0xEE,0xFF,0x80), cD = GS_SETREG_RGBA(0x55,0x55,0x55,0x80);

    int mode = 0, frm = 0, wave = 0, hx = 140, hy = 250, hDir = 1;
    int hp = 100, mHp = 100, stm = 100, souls = 0, wpnLv = 0, atkT = 0, rolT = 0;
    int eAlv = 0, eBoss = 0, ex = 480, ey = 250, eHp = 40, eMHp = 40, eT = 0;

    while (1) {
        readPads(); frm++;
        if (atkT == 0 && rolT == 0 && stm < 100) stm += 2;
        if (atkT > 0) atkT--;

        if (mode == 0) {
            if (wave == 0 && getKeyPressed(KEY_CROSS)) {
                if (hx < 200) { hp = mHp; stm = 100; }
                else if (hx < 320 && souls >= 50) { souls -= 50; wpnLv++; }
            }

            if (rolT > 0) { rolT--; hx += hDir * 7; }
            else if (atkT == 0) {
                if (getKey(KEY_UP) && hy > 195) hy -= 4;
                if (getKey(KEY_DOWN) && hy < 315) hy += 4;
                if (getKey(KEY_LEFT) && hx > 30) { hx -= 4; hDir = -1; }
                if (getKey(KEY_RIGHT) && hx < 560) { hx += 4; hDir = 1; }
                if (getKeyPressed(KEY_CIRCLE) && stm >= 20) { stm -= 20; rolT = 8; }

                if (getKeyPressed(KEY_SQUARE) && stm >= 15) {
                    stm -= 15; atkT = 9;
                    int dmg = 15 + (wpnLv * 8);
                    if (eAlv && abs(ey - hy) < 22 && ((hDir == 1 && ex >= hx && ex - hx < 65) || (hDir == -1 && ex <= hx && hx - ex < 65))) {
                        eHp -= dmg; ex += hDir * 12;
                    }
                }
            }

            if (!eAlv && hx >= 545) {
                hx = 50; wave++;
                if (wave > 2) wave = 0;
                else {
                    eAlv = 1; ex = 480; ey = 250; eT = 0;
                    eBoss = (wave == 2);
                    eMHp = eBoss ? 150 : 45; eHp = eMHp;
                }
            }

            if (eAlv) {
                if (eHp <= 0) { eAlv = 0; souls += eBoss ? 150 : 50; }
                else {
                    if (abs(hy - ey) > 5) ey += (hy > ey) ? 2 : -2;
                    if (abs(hx - ex) > 48) ex += (hx > ex) ? 2 : -2;
                    else if (++eT >= 35) {
                        eT = 0;
                        if (rolT == 0) hp -= eBoss ? 22 : 10;
                    }
                }
            }
            if (hp <= 0) { souls = 0; eAlv = 0; mode = 1; }
        } else if (getKeyPressed(KEY_CROSS)) {
            wave = 0; hx = 140; hp = mHp; stm = 100; mode = 0;
        }

        // Limpa a tela inteira de preto a cada quadro (sem rastro e sem fundo)
        rmStartFrame();
        rmDrawRect(0, 0, 1280, 1024, cBlk);

        sprintf(txt, "HP:%d/%d  STM:%d  ALMAS:%d  ESPADA:+%d  |  Tela %d/2", hp, mHp, stm, souls, wpnLv, wave);
        fntRenderString(0, 25, 25, 0, 0, 0, txt, cG);
        fntRenderString(0, 20, 205, 0, 0, 0, "------------------------------------------------------------------", cD);
        fntRenderString(0, 20, 360, 0, 0, 0, "==================================================================", cW);

        if (wave == 0) {
            fntRenderString(0, 130, 225, 0, 0, 0, (frm%16<8) ? " ^*^ " : " *^* ", cO);
            fntRenderString(0, 110, 195, 0, 0, 0, "Fogueira(X)", cO);
            fntRenderString(0, 255, 225, 0, 0, 0, " /|\\T", cC);
            fntRenderString(0, 230, 195, 0, 0, 0, "Upar Arma 50(X)", cC);
        }
        if (!eAlv && mode == 0 && frm%30 < 15) fntRenderString(0, 520, 175, 0, 0, 0, "SIGA ==>", cY);

        for (int p = 0; p < 2; p++) {
            int dH = (p == 0) ? (hy <= ey) : (hy > ey);
            if (dH) {
                fntRenderString(0, hx, hy, 0, 0, 0, "  o  ", cW);
                fntRenderString(0, hx, hy+18, 0, 0, 0, atkT ? (hDir>0 ? " /|==>" : "<==|\\ ") : " /|\\ ", cW);
                fntRenderString(0, hx, hy+36, 0, 0, 0, (frm%16<8) ? " / > " : " < \\ ", cW);
            } else if (eAlv) {
                fntRenderString(0, ex, ey, 0, 0, 0, eBoss ? " \\O/ " : "  o  ", cR);
                fntRenderString(0, ex, ey+18, 0, 0, 0, eBoss ? "<=#=>" : "-/|\\ ", cR);
                fntRenderString(0, ex, ey+36, 0, 0, 0, " / \\ ", cR);
            }
        }

        if (mode == 1) fntRenderString(0, 220, 180, 0, 0, 0, "VOCE MORREU - Aperte (X)", cR);
        fntRenderString(0, 25, 415, 0, 0, 0, "Setas:Mover | Quad:Atacar | Bolinha:Rolar | X:Interagir", cD);
        rmEndFrame();
    }
    return 0;
}
