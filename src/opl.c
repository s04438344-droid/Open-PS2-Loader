#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <unistd.h>
#include <debug.h>
#include <libpad.h>

static struct {
    u64 a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p;
    u64 q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F;
} padBuf __attribute__((aligned(64)));

static struct {
    u64 a,b,c,d,e,f,g,h,i,j,k,l;
} falaMem;

int abs_val(int v) {
    return (v < 0) ? -v : v;
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

void tela_historia_inicio(void) {
    scr_clear();
    scr_setXY(14, 2);  scr_printf("=== DARK STONE SOULS: A PEDRA DA ALMA ===");
    scr_setXY(33, 4);  scr_printf("    ^^    ");
    scr_setXY(33, 5);  scr_printf("   /  |   ");
    scr_setXY(33, 6);  scr_printf("  ( ** )  ");
    scr_setXY(33, 7);  scr_printf("   |  /   ");
    scr_setXY(33, 8);  scr_printf("    vv    ");

    scr_setXY(5, 10); scr_printf("A Pedra da Alma foi partida em 7 FRAGMENTOS espalhados por 3 reinos.");
    scr_setXY(5, 11); scr_printf("3 Guardioes Colossais protegem os selos. Observe seus padroes:");
    scr_setXY(5, 12); scr_printf("use o ROLAMENTO (X) para atravessar golpes e controle seu VIGOR!");
    scr_setXY(5, 14); scr_printf("CONTROLES: Direcional (Move 3D) | (Quad) Atacar | (Bola) Relampago");
    scr_setXY(5, 15); scr_printf("(X) Rolar (Invencivel) | (R2) Frasco Cura | (L1/R1) Arma | (START) Pausa");

    scr_setXY(21, 21); scr_printf("Pressione (X) para acender sua jornada...");
    esperar_botao(PAD_CROSS);
    scr_clear();
}

void tela_historia_final(int nivel, int arma_nv, int mortes) {
    scr_clear();
    scr_setXY(18, 3);  scr_printf("=== A PEDRA DA ALMA FOI RESTAURADA! ===");
    scr_setXY(33, 5);  scr_printf("   .|.    ");
    scr_setXY(33, 6);  scr_printf("  -(*)-   ");
    scr_setXY(33, 7);  scr_printf("   '|'    ");

    scr_setXY(8, 10); scr_printf("Os 3 Guardioes cairam e o Lorde das Sombras virou cinzas!");
    scr_setXY(8, 11); scr_printf("Os 7 Fragmentos se uniram e dissiparam a escuridao do Vale.");
    scr_setXY(8, 13); scr_printf("Nivel Final: %d  |  Armas Forjadas: +%d  |  Quedas: %d", nivel, arma_nv, mortes);
    scr_setXY(8, 15); scr_printf("Voce conquistou Dark Stone Souls no PlayStation 2!");

    scr_setXY(20, 20); scr_printf("Pressione (X) para iniciar Nova Jornada+...");
    esperar_botao(PAD_CROSS);
    scr_clear();
}

const char* nome_arma(int id) {
    if (id == 0) return "ESPADA+ESCUDO";
    if (id == 1) return "MARTELO PESADO";
    if (id == 2) return "ARCO DE CACA";
    return "LAMINAS DUPLAS";
}

int main(void) {
    struct padButtonStatus buttons;
    u32 paddata = 0, old_pad = 0, new_pad = 0;
    char *fala = (char *)&falaMem;

    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, &padBuf);

    init_scr();
    scr_setCursor(0);
    scr_clear();

    while (1) {
        tela_historia_inicio();

        // Status e Evolucao Estilo Souls
        int nivel = 1, xp = 0, xp_prox = 40;
        int hp = 24, hp_max = 24;
        int mp = 12, mp_max = 12;
        int stam = 100, stam_max = 100;
        int atk_base = 2;

        // Inventario, Forja e Progresso (7 Fragmentos)
        int frascos = 4, frascos_max = 4;
        int minerios = 0;
        int arma_nv = 0;
        int fragmentos = 0;
        int mortes = 0;
        int jogo_zerado = 0;
        int reiniciar = 0;

        // 4 Armas
        int arma_atual = 0;

        // Posicao 2.5D e Rolamento
        // area: 0=Santuario, 1=Floresta Sombria, 2=Caverna Cristal, 3=Castelo Sombras
        int area = 0;
        int px = 12, py = 15;
        int roll_timer = 0, roll_dir = 1;
        int atk_timer = 0;
        int raio_timer = 0, raio_x = 0, raio_y = 14;

        // Projetil do Arco
        int flecha_ativa = 0, fx = 0, fy = 0;

        // Inimigo / Chefe e Mecanicas Taticas
        int ex = 58, ey = 15, ehp = 10, ehp_max = 10;
        int e_stun = 0;
        int tipo_chefe = 0; // 0=Comum, 1=Cavaleiro(Escudo), 2=Golem(Onda), 3=Lorde(Raios)
        int chefe_estado = 0; // Controla postura/ataque especial do chefe
        int chefe_timer = 0;
        int onda_ativa = 0, onda_x = 0, onda_y = 15;
        int raio_chefe_timer = 0, rc_x = 0, rc_y = 15;
        int frame = 0;

        sprintf(fala, "Santuario: Descanse na Fogueira ou use a Forja com (Triangulo).");

        while (!jogo_zerado && !reiniciar) {
            frame++;
            if (padGetState(0, 0) == PAD_STATE_STABLE) {
                if (padRead(0, 0, &buttons) != 0) {
                    paddata = 0xffff ^ buttons.btns;
                    new_pad = paddata & ~old_pad;
                    old_pad = paddata;
                }
            }

            // Regeneracao natural de Vigor (Stamina) quando nao esta rolando ou batendo
            if (roll_timer == 0 && atk_timer == 0 && stam < stam_max) {
                stam += 3;
                if (stam > stam_max) stam = stam_max;
            }

            // MENU DE PAUSA (Botao START)
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

                    if ((new_pad & PAD_UP) && cursor > 0) cursor--;
                    if ((new_pad & PAD_DOWN) && cursor < 3) cursor++;
                    if (new_pad & PAD_START) pausado = 0;

                    if (new_pad & PAD_CROSS) {
                        if (cursor == 0) pausado = 0;
                        else if (cursor == 1) arma_atual = (arma_atual + 1) % 4;
                        else if (cursor == 2) {
                            if (frascos > 0 && hp < hp_max) {
                                frascos--;
                                hp += 12 + (nivel * 2);
                                if (hp > hp_max) hp = hp_max;
                            }
                        } else if (cursor == 3) {
                            reiniciar = 1;
                            pausado = 0;
                        }
                    }

                    scr_setXY(15, 5);  scr_printf("+================================================+");
                    scr_setXY(15, 6);  scr_printf("|             MENU DE PAUSA (SOULS)              |");
                    scr_setXY(15, 7);  scr_printf("+------------------------------------------------+");
                    scr_setXY(15, 8);  scr_printf("| Nivel: %-2d  ATK: %-2d  Forja: +%-2d  Quedas: %-2d    |", nivel, atk_base, arma_nv, mortes);
                    scr_setXY(15, 9);  scr_printf("| HP: %-2d/%-2d  MP: %-2d/%-2d  Minerios: %-2d  Frag: %d/7  |", hp, hp_max, mp, mp_max, minerios, fragmentos);
                    scr_setXY(15, 10); scr_printf("+------------------------------------------------+");
                    scr_setXY(15, 11); scr_printf("| %s Continuar Jornada                          |", cursor == 0 ? "-->" : "   ");
                    scr_setXY(15, 12); scr_printf("| %s Arma: %-16s (X troca)          |", cursor == 1 ? "-->" : "   ", nome_arma(arma_atual));
                    scr_setXY(15, 13); scr_printf("| %s Beber Frasco de Cura (Restam: %d)          |", cursor == 2 ? "-->" : "   ", frascos);
                    scr_setXY(15, 14); scr_printf("| %s Reiniciar Aventura                         |", cursor == 3 ? "-->" : "   ");
                    scr_setXY(15, 15); scr_printf("+------------------------------------------------+");
                    scr_setXY(15, 16); scr_printf("| (Cima/Baixo) Mover | (X) Confirmar | START     |");
                    scr_setXY(15, 17); scr_printf("+================================================+");
                    usleep(40000);
                }
                scr_clear();
                continue;
            }

            // Trocar Arma (L1 ou R1)
            if ((new_pad & PAD_L1) || (new_pad & PAD_R1)) {
                arma_atual = (arma_atual + 1) % 4;
                if (arma_atual == 0) sprintf(fala, "Arma: ESPADA+ESCUDO +%d (Defende golpes e ataca rapido).", arma_nv);
                if (arma_atual == 1) sprintf(fala, "Arma: MARTELO PESADO +%d (Quebra postura e da dano alto).", arma_nv);
                if (arma_atual == 2) sprintf(fala, "Arma: ARCO DE CACA +%d (Atira flechas a distancia).", arma_nv);
                if (arma_atual == 3) sprintf(fala, "Arma: LAMINAS DUPLAS +%d (Golpe duplo com baixo gasto vigor).", arma_nv);
            }

            // Beber Frasco de Cura (Botao R2)
            if (new_pad & PAD_R2) {
                if (frascos > 0 && hp < hp_max) {
                    frascos--;
                    int cura = 12 + (nivel * 2);
                    hp += cura;
                    if (hp > hp_max) hp = hp_max;
                    sprintf(fala, "Frasco usado! +%d HP recuperado (Restam %d frascos).", cura, frascos);
                } else if (frascos == 0) {
                    sprintf(fala, "Frascos vazios! Volte a Fogueira no Santuario para encher.");
                } else {
                    sprintf(fala, "Sua vida ja esta cheia!");
                }
            }

            // ROLAMENTO ESTILO DARK SOULS (Botao X) - Invencivel durante o giro!
            if ((new_pad & PAD_CROSS) && roll_timer == 0) {
Use o código com cuidado.
if (stam >= 25) {
stam -= 25;
roll_timer = 6;
roll_dir = (paddata & PAD_LEFT) ? -1 : 1;
} else {
sprintf(fala, "Sem Vigor (Stamina) para rolar! Aguarde recuperar.");
}
}
// Movimentacao 2.5D ou Rolamento
if (roll_timer > 0) {
px += (roll_dir * 2);
if (px < 2) px = 2;
if (px > 71) px = 71;
} else {
if ((paddata & PAD_LEFT) && px > 2) px--;
if ((paddata & PAD_RIGHT) && px < 71) px++;
if ((paddata & PAD_UP) && py > 11) py--;
if ((paddata & PAD_DOWN) && py < 17) py++;
}
// Transicao entre as 4 Regioes
if (area == 0 && px >= 70) {
// Entra na Floresta Sombria (Fragmentos 0 e 1)
area = 1; px = 4; ex = 58; ey = 14;
e_stun = 0; flecha_ativa = 0; onda_ativa = 0; raio_chefe_timer = 0;
if (fragmentos == 1) {
tipo_chefe = 1; chefe_estado = 0; chefe_timer = 0;
ehp_max = 32; ehp = ehp_max;
sprintf(fala, "GUARDIAO 1: Cavaleiro de Ferro! Cuidado com o escudo frontal dele!");
} else {
tipo_chefe = 0;
ehp_max = 9 + (nivel * 3); ehp = ehp_max;
sprintf(fala, "Floresta Sombria: Derrote Cavaleiros Caidos (%d/7 Fragmentos).", fragmentos);
}
} else if (area == 1 && px <= 2) {
area = 0; px = 67; flecha_ativa = 0;
sprintf(fala, "Santuario: Fogueira (esq) ou Forja (dir) com (Triangulo).");
} else if (area == 1 && px >= 70) {
if (fragmentos >= 2) {
// Entra na Caverna de Cristal (Fragmentos 2 e 3)
area = 2; px = 4; ex = 58; ey = 14;
e_stun = 0; flecha_ativa = 0; onda_ativa = 0; raio_chefe_timer = 0;
if (fragmentos == 3) {
tipo_chefe = 2; chefe_estado = 0; chefe_timer = 0;
ehp_max = 48; ehp = ehp_max;
sprintf(fala, "GUARDIAO 2: Golem Colossal! Role (X) para atravessar as ondas dele!");
} else {
tipo_chefe = 0;
ehp_max = 14 + (nivel * 3); ehp = ehp_max;
sprintf(fala, "Caverna de Cristal: Inimigos mais fortes! (%d/7 Fragmentos).", fragmentos);
}
} else {
px = 69;
sprintf(fala, "Nevoa bloqueada! Derrote o Guardiao da Floresta (2/7 Frag).");
}
} else if (area == 2 && px <= 2) {
area = 1; px = 67; ex = 20; ey = 14; tipo_chefe = 0;
ehp_max = 9 + (nivel * 3); ehp = ehp_max;
sprintf(fala, "Voce voltou para a Floresta Sombria.");
} else if (area == 2 && px >= 70) {
if (fragmentos >= 4) {
// Entra no Castelo das Sombras (Fragmentos 4, 5 e 6)
area = 3; px = 4; ex = 58; ey = 14;
e_stun = 0; flecha_ativa = 0; onda_ativa = 0; raio_chefe_timer = 0;
if (fragmentos >= 6) {
tipo_chefe = 3; chefe_estado = 0; chefe_timer = 0;
ehp_max = 75; ehp = ehp_max;
sprintf(fala, "CHEFE FINAL: Lorde das Sombras! Desvie dos raios marcados no chao!");
} else {
tipo_chefe = 0;
ehp_max = 20 + (nivel * 3); ehp = ehp_max;
sprintf(fala, "Castelo das Sombras: A tempestade ruge! (%d/7 Fragmentos).", fragmentos);
}
} else {
px = 69;
sprintf(fala, "Portao selado! Derrote o Golem Colossal da Caverna (4/7 Frag).");
}
} else if (area == 3 && px <= 2) {
area = 2; px = 67; ex = 20; ey = 14; tipo_chefe = 0;
ehp_max = 14 + (nivel * 3); ehp = ehp_max;
sprintf(fala, "Voce recuou para a Caverna de Cristal.");
}
// Ataque com a Arma Equipada (Consome Vigor!)
if ((new_pad & PAD_SQUARE) && atk_timer == 0 && roll_timer == 0) {
int custo_stam = (arma_atual == 1) ? 35 : ((arma_atual == 3) ? 15 : 20);
if (stam >= custo_stam) {
stam -= custo_stam;
if (arma_atual == 0) atk_timer = 4;
if (arma_atual == 1) atk_timer = 8;
if (arma_atual == 2) {
atk_timer = 4;
if (!flecha_ativa) {
flecha_ativa = 1;
fx = px + 6;
fy = py;
}
}
if (arma_atual == 3) atk_timer = 3;
} else {
sprintf(fala, "Vigor esgotado! Recue um pouco para recuperar folego.");
}
}
// Magia: Relampago (Botao Circulo)
if ((new_pad & PAD_CIRCLE) && raio_timer == 0 && mp >= 3) {
mp -= 3;
raio_timer = 6;
raio_x = (area > 0) ? ex : (px + 10);
raio_y = (area > 0) ? ey : py;
if (area > 0) {
int dano_raio = atk_base + arma_nv + 4;
ehp -= dano_raio;
e_stun = 14;
chefe_estado = 0; // Quebra a defesa do Cavaleiro de Ferro!
sprintf(fala, "RELAMPAGO! Quebrou a guarda e causou %d de dano + Atordoamento!", dano_raio);
} else {
sprintf(fala, "Voce invocou um Relampago nos ceus do Santuario!");
}
}
// Interacoes no Santuario (Botao Triangulo)
if ((new_pad & PAD_TRIANGLE) && area == 0) {
if (px >= 18 && px <= 34 && py <= 15) {
hp = hp_max;
mp = mp_max;
stam = stam_max;
frascos = frascos_max;
sprintf(fala, "FOGUEIRA ACESA: Vida, Mana, Vigor e %d Frascos restaurados!", frascos);
} else if (px >= 42 && px <= 58 && py <= 15) {
int custo = 2 + arma_nv;
if (minerios >= custo) {
minerios -= custo;
arma_nv++;
sprintf(fala, "FORJA CONCLUIDA! Todas as suas armas agora sao +%d!", arma_nv);
} else {
sprintf(fala, "Forja: Precisa de %d Minerios para fundir +%d (Voce tem %d).", custo, arma_nv + 1, minerios);
}
} else {
sprintf(fala, "Aproxime-se da Fogueira ou da Forja ao fundo e aperte (Triangulo).");
}
}
// Movimento da Flecha do Arco
if (flecha_ativa) {
fx += 4;
if (fx > 74) flecha_ativa = 0;
}
// Movimento da Onda de Choque do Chefe 2 (Golem Colossal)
if (onda_ativa) {
onda_x -= 2;
if (onda_x < 2) onda_ativa = 0;
// Se acertar o jogador sem estar rolando (roll_timer == 0)
if (onda_ativa && roll_timer == 0 && abs_val(py - onda_y) <= 1 && abs_val(px - onda_x) <= 2) {
int dano_onda = 5;
hp -= dano_onda;
onda_ativa = 0;
sprintf(fala, "A onda sismica te atingiu (-%d HP)! Use (X) para rolar por cima dela!", dano_onda);
}
}
// Logica de Combate e Inteligencia Artificial dos 3 Chefes
if (area > 0) {
if (e_stun > 0) {
e_stun--;
} else {
// Comportamento especial de cada Chefe
if (tipo_chefe == 1) {
// CHEFE 1: Cavaleiro de Ferro (Alterna entre Escudo Levantado e Investida)
chefe_timer++;
if (chefe_timer < 35) {
chefe_estado = 1; // Escudo levantado (bloqueia ataques frontais!)
if (frame % 3 == 0 && ex > px + 6) ex--;
} else if (chefe_timer < 55) {
chefe_estado = 2; // Baixou escudo para atacar (hora de bater ou rolar!)
if (frame % 2 == 0 && ex > px + 4) ex--;
} else {
chefe_timer = 0;
}
} else if (tipo_chefe == 2) {
// CHEFE 2: Golem Colossal (Dispara ondas sismicas na estrada)
chefe_timer++;
if (chefe_timer == 30) {
chefe_estado = 1; // Preparando socao no chao
sprintf(fala, "O Golem ergueu os punhos! Prepare o rolamento (X)!");
} else if (chefe_timer == 42) {
chefe_estado = 0;
onda_ativa = 1;
onda_x = ex - 2;
onda_y = ey;
} else if (chefe_timer > 60) {
chefe_timer = 0;
}
if (frame % 3 == 0 && ex > px + 8) ex--;
} else if (tipo_chefe == 3) {
// CHEFE FINAL: Lorde das Sombras (Marca raios no chao + teleporte)
chefe_timer++;
if (chefe_timer == 25) {
raio_chefe_timer = 18;
rc_x = px;
rc_y = py;
sprintf(fala, "Selo negro aos seus pes! Role (X) para sair antes que o raio caia!");
} else if (chefe_timer > 55) {
chefe_timer = 0;
}
if (frame % 2 == 0) {
if (ex > px + 5) ex--;
else if (ex < px - 5) ex++;
}
} else {
// Inimigo comum
if (frame % 2 == 0) {
if (ex > px + 5) ex--;
else if (ex < px - 5) ex++;
}
}
// Alinhamento de profundidade Y
if (frame % 4 == 0) {
if (ey < py) ey++;
else if (ey > py) ey--;
}
}
// Executa dano do raio marcado pelo Lorde das Sombras
if (raio_chefe_timer > 0) {
raio_chefe_timer--;
if (raio_chefe_timer == 1) {
if (roll_timer == 0 && abs_val(px - rc_x) <= 2 && abs_val(py - rc_y) <= 1) {
hp -= 7;
sprintf(fala, "O raio negro te atingiu (-7 HP)! Role para fora da marca '()'!");
}
}
}
// Acerto do Jogador no Inimigo/Chefe
int acertou = 0;
int dano = 0;
int mesma_linha = (abs_val(py - ey) <= 1);
if (mesma_linha && ex >= px && ex <= px + 9) {
if (arma_atual == 0 && atk_timer == 3) {
acertou = 1; dano = atk_base + arma_nv; ex += 3;
} else if (arma_atual == 1 && atk_timer == 5) {
acertou = 1; dano = ((atk_base + arma_nv) * 2) + 2; ex += 7; e_stun = 6;
} else if (arma_atual == 3 && (atk_timer == 2 || atk_timer == 1)) {
acertou = 1; dano = atk_base + arma_nv; ex += 2;
}
}
if (flecha_ativa && abs_val(fy - ey) <= 1 && fx >= ex - 2 && fx <= ex + 5) {
acertou = 1;
dano = atk_base + arma_nv + 1;
flecha_ativa = 0;
}
if (acertou) {
// Se for o Cavaleiro de Ferro com escudo levantado e sem stun
if (tipo_chefe == 1 && chefe_estado == 1 && e_stun == 0 && arma_atual != 1) {
sprintf(fala, "ESCUDO DE FERRO BLOQUEOU! Use Martelo, Relampago ou espere ele baixar!");
} else {
if (tipo_chefe == 1 && chefe_estado == 1 && arma_atual == 1) {
e_stun = 10; // Martelo quebra a guarda do Cavaleiro!
sprintf(fala, "GUARDA QUEBRADA com o Martelo! Causou %d de dano!", dano);
} else {
sprintf(fala, "Acerto com %s +%d! Dano: %d (HP Alvo: %d/%d).",
nome_arma(arma_atual), arma_nv, dano, (ehp - dano) > 0 ? (ehp - dano) : 0, ehp_max);
}
ehp -= dano;
}
}
// Verifica Morte do Inimigo ou Chefe
if (ehp <= 0) {
if (tipo_chefe == 3) {
jogo_zerado = 1;
break;
}
int eh_guardiao = (tipo_chefe > 0);
xp += eh_guardiao ? 45 : 18;
minerios += eh_guardiao ? 2 : 1;
ex = 62;
ey = 14;
e_stun = 5;
onda_ativa = 0;
raio_chefe_timer = 0;
if (fragmentos < 7) fragmentos++;
if (xp >= xp_prox) {
nivel++;
xp -= xp_prox;
xp_prox += 20;
hp_max += 5;
mp_max += 3;
stam_max += 10;
atk_base += 1;
hp = hp_max;
mp = mp_max;
stam = stam_max;
}
// Define o proximo inimigo ou spawna o Guardiao da area!
if (area == 1 && fragmentos == 1) {
tipo_chefe = 1; chefe_estado = 0; chefe_timer = 0;
ehp_max = 32; ehp = ehp_max;
sprintf(fala, "GUARDIAO 1 SURGIU: Cavaleiro de Ferro! Quebre a guarda dele!");
} else if (area == 2 && fragmentos == 3) {
tipo_chefe = 2; chefe_estado = 0; chefe_timer = 0;
ehp_max = 48; ehp = ehp_max;
sprintf(fala, "GUARDIAO 2 SURGIU: Golem Colossal! Role (X) sobre as ondas!");
} else if (area == 3 && fragmentos == 6) {
tipo_chefe = 3; chefe_estado = 0; chefe_timer = 0;
ehp_max = 75; ehp = ehp_max;
sprintf(fala, "CHEFE FINAL: O Lorde das Sombras desceu ao trono!");
} else {
tipo_chefe = 0;
ehp_max = (area == 1 ? 9 : (area == 2 ? 14 : 20)) + (nivel * 3);
ehp = ehp_max;
if (fragmentos == 2) sprintf(fala, "Guardiao derrotado! A Caverna de Cristal a direita (-->) abriu!");
else if (fragmentos == 4) sprintf(fala, "Golem derrotado! O Castelo das Sombras a direita (-->) abriu!");
else sprintf(fala, "Inimigo derrotado! +1 Minerio e Fragmento (%d/7)!", fragmentos);
}
}
// Ataque corpo a corpo do Inimigo no Jogador (Invencivel durante roll_timer > 0!)
if (e_stun == 0 && roll_timer == 0 && abs_val(py - ey) <= 1 && ex >= px - 1 && ex <= px + 4) {
int dano_sofrido = (tipo_chefe > 0) ? (3 + area) : (1 + area);
if (arma_atual == 0) {
dano_sofrido = (dano_sofrido > 1) ? (dano_sofrido / 2) : 1;
sprintf(fala, "ESCUDO absorveu parte do golpe! Sofreu %d de dano.", dano_sofrido);
} else {
sprintf(fala, "Voce foi golpeado (-%d HP)! Use (X) para rolar antes do golpe!", dano_sofrido);
}
hp -= dano_sofrido;
ex += 6;
}
// Verifica Morte do Jogador (Estilo Dark Souls: volta a Fogueira)
if (hp <= 0) {
mortes++;
area = 0;
px = 12; py = 15;
hp = hp_max; mp = mp_max; stam = stam_max;
frascos = frascos_max;
tipo_chefe = 0; onda_ativa = 0; raio_chefe_timer = 0;
sprintf(fala, "VOCE MORREU... Despertou na Fogueira do Santuario (Quedas: %d).", mortes);
}
}
if (atk_timer > 0) atk_timer--;
if (raio_timer > 0) raio_timer--;
if (roll_timer > 0) roll_timer--;
// Limpa area do cenario
for (int y = 4; y <= 18; y++) {
scr_setXY(0, y);
scr_printf("                                                                              ");
}
// HUD Superior Estilo Souls
scr_setXY(2, 1);
scr_printf("LV:%d HP:%d/%d MP:%d/%d VIGOR:%d%% FRASCO:%d MIN:%d FRAG:%d/7",
nivel, hp, hp_max, mp, mp_max, (stam * 100) / stam_max, frascos, minerios, fragmentos);
scr_setXY(2, 2);
scr_printf("ARMA:(%s +%d) | (X)Rolar (Quad)Bater (Bola)Raio (R2)Cura (START)Pausa",
nome_arma(arma_atual), arma_nv);
// HORIZONTE E ESTRADA EM PERSPECTIVA 2.5D
scr_setXY(0, 8);
scr_printf("..--^^--....--^^--....--^^--....--^^--....--^^--....--^^--....--^^");
scr_setXY(0, 9);
scr_printf("   /      /         /         /         /         /         /         /       ");
scr_setXY(0, 10);
scr_printf("------------------------------------------------------------------------------");
// Cenarios das 4 Regioes
if (area == 0) {
scr_setXY(6, 5);   scr_printf("  ..          (  )            ..          (  )  ");
scr_setXY(6, 6);   scr_printf(" (    )        (    )          (    )        (    ) ");
// Fogueira Ancestral (Bonfire com espada cravada!)
scr_setXY(22, 10); scr_printf("    |    ");
scr_setXY(22, 11); scr_printf(frame % 4 < 2 ? "  ( | )  " : "  ) | (  ");
scr_setXY(22, 12); scr_printf(" (|) ");
scr_setXY(22, 13); scr_printf(" (FOGUEIRA)");
// Caldeirao de Forja
scr_setXY(45, 10); scr_printf(frame % 4 < 2 ? "  ( ) )  " : "  ( ( )  ");
scr_setXY(45, 11); scr_printf(" .-----. ");
scr_setXY(45, 12); scr_printf(" |FORJA| ");
scr_setXY(45, 13); scr_printf(" /-----/ ");
scr_setXY(66, 14); scr_printf("FLORESTA ->");
} else if (area == 1) {
// Floresta Sombria
scr_setXY(14, 10); scr_printf(" .@@@. "); scr_setXY(14, 11); scr_printf("@@@@@@@"); scr_setXY(15, 12); scr_printf(" | | ");
scr_setXY(48, 10); scr_printf(" .@@@. "); scr_setXY(48, 11); scr_printf("@@@@@@@"); scr_setXY(49, 12); scr_printf(" | | ");
scr_setXY(2, 14);  scr_printf("<- SANTUARIO");
scr_setXY(66, 14); scr_printf(fragmentos >= 2 ? "CAVERNA ->" : "(NEVOA)");
} else if (area == 2) {
// Caverna de Cristal
scr_setXY(2, 4);   scr_printf("VV  V   VVV    V     VV    VVV   V    VVV   V    VV  VVV");
scr_setXY(18, 10); scr_printf(" /++\ "); scr_setXY(18, 11); scr_printf("/++++\");
scr_setXY(50, 10); scr_printf(" /++\ "); scr_setXY(50, 11); scr_printf("/++++\");
scr_setXY(2, 14);  scr_printf("<- FLORESTA");
scr_setXY(66, 14); scr_printf(fragmentos >= 4 ? "CASTELO ->" : "(SELADO)");
} else {
// Castelo das Sombras com Chuva Animada
for (int cy = 4; cy <= 16; cy += 3) {
int offset = (frame + cy) % 8;
scr_setXY(4 + offset, cy); scr_printf("/    /    /    /    /    /    /    /");
}
scr_setXY(56, 5);  scr_printf(" |^|  |^| ");
scr_setXY(56, 6);  scr_printf(" | |__| | ");
scr_setXY(56, 7);  scr_printf(" |  __  | ");
scr_setXY(2, 14);  scr_printf("<- CAVERNA");
}
// Marca no chao do raio do Chefe Final
if (raio_chefe_timer > 0) {
scr_setXY(rc_x, rc_y);
scr_printf(raio_chefe_timer > 3 ? "( !! )" : "VVVVVV");
}
// Onda Sismica do Golem Colossal
if (onda_ativa) {
scr_setXY(onda_x, onda_y);
scr_printf("~~~");
}
// Ordenacao de Profundidade 2.5D
int jogador_atras = (py < ey);
for (int passo = 0; passo < 2; passo++) {
if ((passo == 0 && jogador_atras) || (passo == 1 && !jogador_atras)) {
// DESENHA JOGADOR (Com animacao de Rolamento estilo Dark Souls!)
if (roll_timer > 0) {
scr_setXY(px, py - 1); scr_printf(" .-. ");
scr_setXY(px, py);     scr_printf("( @ )");
} else {
scr_setXY(px, py - 2);
scr_printf(arma_nv > 0 ? " ^o^ " : "  o  ");
scr_setXY(px, py - 1);
if (arma_atual == 0) {
if (atk_timer > 0) scr_printf(arma_nv > 0 ? "(#)|===>>" : "(#)|===>");
else               scr_printf("(#)|- -  ");
} else if (arma_atual == 1) {
if (atk_timer > 4)      scr_printf(arma_nv > 0 ? " -o- (TTT)" : " -o- (T)");
else if (atk_timer > 0) scr_printf(arma_nv > 0 ? "  |--(====)" : "  |--(===)");
else                    scr_printf(" -|- (T)");
} else if (arma_atual == 2) {
if (atk_timer > 0) scr_printf(" -|--|)  ");
else               scr_printf(" -|- |)  ");
} else {
if (atk_timer > 0) scr_printf(" >>|->>  ");
else               scr_printf(" /|-|/   ");
}
scr_setXY(px, py);
if ((paddata & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN)) && (frame % 4 < 2)) {
scr_printf(" / | ");
} else {
scr_printf(" | | ");
}
}
} else if (area > 0) {
// DESENHA INIMIGO OU GUARDIAO / CHEFE
if (tipo_chefe == 1) {
// Cavaleiro de Ferro (Mostra escudo || quando em postura defensiva!)
scr_setXY(ex, ey - 3); scr_printf("  .===.  ");
scr_setXY(ex, ey - 2); scr_printf(e_stun > 0 ? " (x _ x) " : " (O _ O) ");
scr_setXY(ex, ey - 1); scr_printf(chefe_estado == 1 && e_stun == 0 ? "||-|###| " : " /-|###| ");
scr_setXY(ex, ey);     scr_printf("   / |   ");
} else if (tipo_chefe == 2) {
// Golem Colossal
scr_setXY(ex, ey - 3); scr_printf(chefe_estado == 1 ? " ^.---.^ " : "  .---.  ");
scr_setXY(ex, ey - 2); scr_printf(e_stun > 0 ? " (x _ x) " : " (O _ O) ");
scr_setXY(ex, ey - 1); scr_printf(" /|#####|");
scr_setXY(ex, ey);     scr_printf("  | | |  ");
} else if (tipo_chefe == 3) {
// Chefe Final: Lorde das Sombras
scr_setXY(ex, ey - 3); scr_printf("  ^^^^  ");
scr_setXY(ex, ey - 2); scr_printf(e_stun > 0 ? " v(x_x)v " : " ( >< ) ");
scr_setXY(ex, ey - 1); scr_printf(" /|###|| ");
scr_setXY(ex, ey);     scr_printf("  /   |  ");
} else {
// Inimigos Comuns da Area
if (e_stun > 0) {
scr_setXY(ex, ey - 2); scr_printf("  * @ *  ");
scr_setXY(ex, ey - 1); scr_printf(" (x _ x) ");
scr_setXY(ex, ey);     scr_printf(" /|###|| ");
} else {
scr_setXY(ex, ey - 2); scr_printf(area == 1 ? "  .---.  " : "  .<->.  ");
scr_setXY(ex, ey - 1); scr_printf(" (O _ O) ");
scr_setXY(ex, ey);     scr_printf(frame % 6 < 3 ? " /|###|| " : " ||###|/ ");
}
}
}
}
// Flecha voando
if (flecha_ativa) {
scr_setXY(fx, fy - 1);
scr_printf("--->");
}
// Relampago do Jogador
if (raio_timer > 0) {
for (int ry = 4; ry <= raio_y; ry++) {
scr_setXY(raio_x + (ry % 2 == 0 ? 1 : 0), ry);
scr_printf(ry % 2 == 0 ? "VV" : "AA");
}
}
// Borda inferior da estrada
scr_setXY(0, 18);
if (area == 0)      scr_printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
else if (area == 1) scr_printf(",.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.,.");
else if (area == 2) scr_printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^");
else                scr_printf("==============================================================================");
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
