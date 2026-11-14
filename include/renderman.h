#ifndef __RENDERMAN_H
#define __RENDERMAN_H

// Definições de resolução, alinhamento e estruturas básicas (GSCLUT, rm_quad_t)
#define GS_MODE_DTV_576P 0x53
#define DIM_UNDEF -1

#define ALIGN_TOP     (0 << 0)
#define ALIGN_BOTTOM  (1 << 0)
#define ALIGN_VCENTER (2 << 0)
#define ALIGN_LEFT    (0 << 2)
#define ALIGN_RIGHT   (1 << 2)
#define ALIGN_HCENTER (2 << 2)
#define ALIGN_NONE    (ALIGN_TOP | ALIGN_LEFT)
#define ALIGN_CENTER  (ALIGN_VCENTER | ALIGN_HCENTER)

#define SCALING_NONE  0
#define SCALING_RATIO 1

typedef struct {
    u8 PSM;
    u8 ClutPSM;
    u32 *Clut;
    u32 VramClut;
} GSCLUT;

typedef struct {
    float x, y;
    float u, v;
} rm_tx_coord_t;

typedef struct {
    rm_tx_coord_t ul;
    rm_tx_coord_t br;
    u64 color;
    GSTEXTURE *txt;
} rm_quad_t;

extern const u64 gColWhite;
extern const u64 gColBlack;
extern const u64 gColDarker;
extern const u64 gColFocus;
extern const u64 gDefaultCol;
extern const u64 gDefaultAlpha;

enum rm_aratio {
    RM_ARATIO_4_3 = 0,
    RM_ARATIO_16_9,
};

// Funções de inicialização, desenho e controle de tela (incluindo rmDrawPlasma)
void rmInit();
int rmSetMode(int force);
void rmEnd(void);
void rmGetScreenExtentsNative(int *w, int *h);
void rmGetScreenExtents(int *w, int *h);
void rmInvalidateTexture(GSTEXTURE *txt);
void rmUnloadTexture(GSTEXTURE *txt);
void rmDrawQuad(rm_quad_t *q);
void rmDrawPixmap(GSTEXTURE *txt, int x, int y, short aligned, int w, int h, short scaled, u64 color);
void rmDrawOverlayPixmap(GSTEXTURE *overlay, int x, int y, short aligned, int w, int h, short scaled, u64 color,
                         GSTEXTURE *inlay, int ulx, int uly, int urx, int ury, int blx, int bly, int brx, int bry);
void rmDrawRect(int x, int y, int w, int h, u64 color);
void rmDrawLine(int x1, int y1, int x2, int y2, u64 color);
void rmDrawPlasma(void);
void rmStartFrame(void);
void rmEndFrame(void);
void rmSetDisplayOffset(int x, int y);
void rmSetOverscan(int overscan);
void rmSetAspectRatio(enum rm_aratio dar);
int rmWideScale(int x);
float rmGetPAR();
int rmGetInterlacedFrameMode();
int rmScaleX(int x);
int rmScaleY(int y);
int rmUnScaleX(int x);
int rmUnScaleY(int y);
unsigned char rmGetHsync(void);

#endif
