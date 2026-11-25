#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <unistd.h>
#include <debug.h>
#include <libpad.h>

static char padBuf __attribute__((aligned(64)));

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

void tela_historia_inicio(void) {
    scr_clear();
    scr_setXY(18, 3);  scr_printf("=== A LENDA DA PEDRA DA ALMA ===");
    scr_setXY(33, 5);  scr_printf("    /\\    ");
    scr_setXY(33, 6);  scr_printf("   /  \\   ");
    scr_setXY(33, 7);  scr_printf("  ( ** )  ");
    scr_setXY(33, 8);  scr_printf("   \\  /   ");
    scr_setXY(33, 9);  scr_printf("    \\/    ");

    scr_setXY(8, 12); scr_printf("Ha seculos, a Pedra da Alma mantinha a paz no Vale de Pedra.");
    scr_setXY(8, 13); scr_printf("Mas o Lorde das Sombras a partiu em 3 FRAGMENTOS sagrados.");
    scr_setXY(8, 14); scr_printf("Monstros de rocha despertaram e tomaram o Desfiladeiro.");
    scr_setXY(8, 16); scr_printf("SUA MISSAO: Recupere os 3 Fragmentos, derrote o Lorde");
    scr_setXY(8, 17); scr_printf("das Sombras e restaure a luz ao mundo!");

    scr_setXY(22, 21); scr_printf("Pressione (X) para iniciar a jornada...");
    esperar_botao(PAD_CROSS);
    scr_clear();
}

void tela_historia_final(int nivel) {
    scr_clear();
    scr_setXY(18, 3);  scr_printf("=== A PEDRA DA ALMA FOI RESTAURADA! ===");
    scr_setXY(33, 5);  scr_printf("   \\|/    ");
    scr_setXY(33, 6);  scr_printf("  -(*)-   ");
    scr_setXY(33, 7);  scr_printf("   /|\\    ");

    scr_setXY(8, 10); scr_printf("Com um golpe final, o Lorde das Sombras se desfaz em poeira!");
    scr_setXY(8, 11); scr_printf("Os 3 Fragmentos se unem no ceu e devolvem a luz ao vale.");
    scr_setXY(8, 13); scr_printf("Voce salvou o Santuario no Nivel %d!", nivel);
    scr_setXY(8, 15); scr_printf("Obrigado por jogar este RPG em ASCII no PlayStation 2!");

    scr_setXY(20, 20); scr_printf("Pressione (X) para jogar novamente...");
    esperar_botao(PAD_CROSS);
    scr_clear();
}

int main(void) {
    struct padButtonStatus buttons;
    u32 paddata = 0, old_pad = 0, new_pad = 0;

    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, padBuf);

    init_scr();
    scr_setCursor(0);
    scr_clear();

    while (1) {
        tela_historia_inicio();

        // Status e Evolucao do Jogador
        int nivel = 1, xp = 0, xp_prox = 30;
        int hp = 15, hp_max = 15;
        int mp = 9, mp_max = 9;
        int atk_base = 2;

        // Progresso da Historia
        int fragmentos = 0;
        int chefe_ativo = 0;
        int jogo_zerado = 0;

        // Equipamento: 0 = Espada e Escudo, 1 = Martelo Pesado
        int arma_atual = 0;

        // Posicao, Fisica e Combate
        int area = 0;
        int px = 14, py = 17;
        int vy = 0, no_chao = 1;
        int atk_timer = 0;
        int raio_timer = 0, raio_x = 0;

        // Inimigo
        int ex = 60, ehp = 8, ehp_max = 8;
        int e_stun = 0;
        int frame = 0;

        char fala[76];
        sprintf(fala, "Fale com a Estatua (Triangulo) ou siga a direita (->) para lutar.");

        while (!jogo_zerado) {
            frame++;
            if (padGetState(0, 0) == PAD_STATE_STABLE) {
                if (padRead(0, 0, &buttons) != 0) {
                    paddata = 0xffff ^ buttons.btns;
                    new_pad = paddata & ~old_pad;
                    old_pad = paddata;
                }
            }

            // Trocar Arma (L1 ou R1)
            if ((new_pad & PAD_L1) || (new_pad & PAD_R1)) {
                arma_atual = !arma_atual;
                if (arma_atual == 0) {
                    sprintf(fala, "Equipado: ESPADA E ESCUDO (Ataque rapido e bloqueio).");
                } else {
                    sprintf(fala, "Equipado: MARTELO PESADO (Dano alto e empurrao).");
                }
            }

            // Andar Manualmente no Direcional
            if ((paddata & PAD_LEFT) && px > 2) px--;
            if ((paddata & PAD_RIGHT) && px < 71) px++;

            // Troca de Area
            if (area == 0 && px >= 70) {
                area = 1;
                px = 5;
                ex = 60;
                e_stun = 0;
                if (fragmentos >= 3) {
                    chefe_ativo = 1;
                    ehp_max = 35;
                    ehp = ehp_max;
                    sprintf(fala, "CHEFE FINAL: O Lorde das Sombras surgiu! Prepare-se!");
                } else {
                    chefe_ativo = 0;
                    ehp_max = 6 + (nivel * 3);
                    ehp = ehp_max;
                    sprintf(fala, "Desfiladeiro Sombrio! Derrote Golens pelos Fragmentos (%d/3).", fragmentos);
                }
            } else if (area == 1 && px <= 2) {
                area = 0;
                px = 67;
                sprintf(fala, "Voce voltou ao Santuario. Fale com a Estatua (Triangulo).");
            }

            // Pulo (Botao X)
            if ((new_pad & PAD_CROSS) && no_chao) {
                vy = -3;
                no_chao = 0;
            }

            // Ataque com Arma (Botao Quadrado)
            if ((new_pad & PAD_SQUARE) && atk_timer == 0) {
                atk_timer = (arma_atual == 0) ? 4 : 8;
            }

            // Magia: Relampago (Botao Circulo)
            if ((new_pad & PAD_CIRCLE) && raio_timer == 0 && mp >= 3) {
                mp -= 3;
                raio_timer = 6;
                raio_x = (area == 1) ? ex : (px + 10);
                if (area == 1) {
                    int dano_raio = atk_base + 3;
                    ehp -= dano_raio;
                    e_stun = 12;
                    sprintf(fala, "RELAMPAGO! Caiu do ceu e causou %d de dano + Atordoamento!", dano_raio);
                } else {
                    sprintf(fala, "Voce invocou um Relampago nos ceus do Santuario!");
                }
            }

            // Interacao com a Estatua / Historia no Santuario (Botao Triangulo)
            if ((new_pad & PAD_TRIANGLE) && area == 0 && px >= 26 && px <= 40) {
                hp = hp_max;
                mp = mp_max;
                if (fragmentos == 0) {
                    sprintf(fala, "Estatua: Traga os 3 Fragmentos dos Golens do Desfiladeiro!");
                } else if (fragmentos < 3) {
                    sprintf(fala, "Estatua: Voce ja tem %d/3 Fragmentos! Continue assim!", fragmentos);
                } else {
                    sprintf(fala, "Estatua: Voce tem os 3 Fragmentos! Enfrente o Lorde agora!");
                }
            }

            // Fisica da Gravidade
            py += vy;
            if (py >= 17) {
                py = 17;
                vy = 0;
                no_chao = 1;
            } else {
                vy++;
            }

            // Logica de Combate na Fase
            if (area == 1) {
                if (e_stun > 0) {
                    e_stun--;
                } else if (frame % 2 == 0) {
                    if (ex > px + 4) ex--;
                    else if (ex < px - 4) ex++;
                }

                int acertou = 0;
                int dano = 0;

                if (arma_atual == 0 && atk_timer == 3 && ex >= px && ex <= px + 8 && py >= 15) {
                    acertou = 1;
                    dano = atk_base;
                    ex += 4;
                } else if (arma_atual == 1 && atk_timer == 5 && ex >= px && ex <= px + 9 && py >= 15) {
                    acertou = 1;
                    dano = (atk_base * 2) + 1;
                    ex += 9;
                    e_stun = 6;
                }

                if (acertou) {
                    ehp -= dano;
                    sprintf(fala, "Golpe de %s! Causou %d de dano (HP Inimigo: %d/%d).",
                            arma_atual == 0 ? "Espada" : "Martelo", dano, ehp > 0 ? ehp : 0, ehp_max);
                }

                // Verifica Morte do Inimigo ou Chefe
                if (ehp <= 0) {
                    if (chefe_ativo) {
                        jogo_zerado = 1;
                        break;
                    }

                    int ganho_xp = 15;
                    xp += ganho_xp;
                    ex = 65;
                    e_stun = 5;

                    if (fragmentos < 3) {
                        fragmentos++;
                    }

                    if (xp >= xp_prox) {
                        nivel++;
                        xp -= xp_prox;
                        xp_prox += 15;
                        hp_max += 4;
                        mp_max += 3;
                        atk_base += 1;
                        hp = hp_max;
                        mp = mp_max;
                    }

                    if (fragmentos == 3) {
                        chefe_ativo = 1;
                        ehp_max = 35;
                        ehp = ehp_max;
                        sprintf(fala, "3/3 Fragmentos! O LORDE DAS SOMBRAS apareceu na sua frente!");
                    } else {
                        ehp_max = 6 + (nivel * 3);
                        ehp = ehp_max;
                        sprintf(fala, "Golem derrotado! Voce obteve um FRAGMENTO (%d/3)!", fragmentos);
                    }
                }

                // Ataque do Inimigo ou Chefe no Jogador
                if (e_stun == 0 && ex >= px - 1 && ex <= px + 3 && py >= 16) {
                    int dano_sofrido = chefe_ativo ? 4 : (2 + (nivel / 2));
                    if (arma_atual == 0) {
dano_sofrido = (dano_sofrido > 1) ? (dano_sofrido / 2) : 1;
sprintf(fala, "Seu ESCUDO bloqueou parte do golpe! Sofreu %d de dano.", dano_sofrido);
} else {
sprintf(fala, "Voce foi atingido! Sofreu %d de dano (Sem escudo!).", dano_sofrido);
}
hp -= dano_sofrido;
ex += 6;
if (hp <= 0) {
area = 0;
px = 14;
hp = hp_max;
mp = mp_max;
sprintf(fala, "Voce caiu em batalha e despertou no Santuario!");
}
}
}
if (atk_timer > 0) atk_timer--;
if (raio_timer > 0) raio_timer--;
// Limpa area do cenario
for (int y = 4; y <= 18; y++) {
scr_setXY(0, y);
scr_printf("                                                                              ");
}
// HUD Superior
scr_setXY(2, 1);
scr_printf("LV:%d  HP:%d/%d  MP:%d/%d  FRAG:%d/3  ARMA:(%s)",
nivel, hp, hp_max, mp, mp_max, fragmentos,
arma_atual == 0 ? "ESPADA+ESCUDO" : "MARTELO");
scr_setXY(2, 2);
scr_printf("(Quad) Atacar  (Bola) Relampago(-3MP)  (L1/R1) Arma  (X) Pulo  (Tri) Falar");
// Fundo Parallax ASCII
scr_setXY(4, 5);  scr_printf("   ..      .       /\         /\      .       ..   ");
scr_setXY(4, 6);  scr_printf("  (    )            /  \  /\   /  \            (    )  ");
scr_setXY(4, 7);  scr_printf("   '--'            /    \/  \ /    \            '--'   ");
// Desenha Cenario e NPCs
if (area == 0) {
scr_setXY(30, 14); scr_printf("  /^\  ");
scr_setXY(30, 15); scr_printf(" ( o_o ) ");
scr_setXY(30, 16); scr_printf(" /|===|\ ");
scr_setXY(30, 17); scr_printf(" (ESTATUA)");
scr_setXY(64, 16); scr_printf("DESFILADEIRO ->");
} else {
scr_setXY(2, 16);  scr_printf("<- SANTUARIO");
if (chefe_ativo) {
// Desenho do Chefe Final: Lorde das Sombras
if (e_stun > 0) {
scr_setXY(ex, 14); scr_printf("  * @ *  ");
scr_setXY(ex, 15); scr_printf(" \(x_x)/ ");
scr_setXY(ex, 16); scr_printf("  |###|  ");
scr_setXY(ex, 17); scr_printf("  /   \  ");
} else {
scr_setXY(ex, 14); scr_printf("  /\/\  ");
scr_setXY(ex, 15); scr_printf(" ( >< ) ");
scr_setXY(ex, 16); scr_printf(" /|###|\ ");
scr_setXY(ex, 17); scr_printf("  /   \  ");
}
} else {
// Desenho do Golem de Pedra
if (e_stun > 0) {
scr_setXY(ex, 15); scr_printf("  * @ *  ");
scr_setXY(ex, 16); scr_printf(" (x _ x) ");
scr_setXY(ex, 17); scr_printf(" /|###|\ ");
} else if (frame % 6 < 3) {
scr_setXY(ex, 15); scr_printf("  .---.  ");
scr_setXY(ex, 16); scr_printf(" (O _ O) ");
scr_setXY(ex, 17); scr_printf(" /|###|\ ");
} else {
scr_setXY(ex, 15); scr_printf("  .---.  ");
scr_setXY(ex, 16); scr_printf(" (O _ O) ");
scr_setXY(ex, 17); scr_printf(" \|###|/ ");
}
}
}
// Desenha Relampago caindo do ceu
if (raio_timer > 0) {
for (int ry = 4; ry <= 16; ry++) {
scr_setXY(raio_x + (ry % 2 == 0 ? 1 : 0), ry);
scr_printf(ry % 2 == 0 ? "VV" : "AA");
}
}
// Desenha Jogador e Equipamento
scr_setXY(px, py - 2);
scr_printf("  o  ");
scr_setXY(px, py - 1);
if (arma_atual == 0) {
if (atk_timer > 0) scr_printf("(#)|===>");
else               scr_printf("(#)|- - ");
} else {
if (atk_timer > 4)      scr_printf(" -o- (T)");
else if (atk_timer > 0) scr_printf("  |--(===)");
else                    scr_printf(" -|- (T)");
}
scr_setXY(px, py);
if ((paddata & (PAD_LEFT | PAD_RIGHT)) && (frame % 4 < 2)) {
scr_printf(" / \ ");
} else {
scr_printf(" | | ");
}
// Chao texturizado e Caixa de Dialogo
scr_setXY(0, 18);
scr_printf("==============================================================================");
scr_setXY(2, 20);
scr_printf("+----------------------------------------------------------------------------+");
scr_setXY(2, 21);
scr_printf("| %-74s |", fala);
scr_setXY(2, 22);
scr_printf("+----------------------------------------------------------------------------+");
usleep(45000);
}
tela_historia_final(nivel);
}
return 0;
}
