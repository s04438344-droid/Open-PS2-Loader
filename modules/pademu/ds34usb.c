#include "types.h"
#include "loadcore.h"
#include "stdio.h"
#include "sifrpc.h"
#include "sysclib.h"
#include "usbd.h"
#include "usbd_macro.h"
#include "thbase.h"
#include "thsemap.h"
#include "ds34usb.h"

#define USB_CLASS_HID         0x03
#define USB_SUBCLASS_BOOT     0x01
#define USB_PROTOCOL_KEYBOARD 0x01

#define MAX_PADS 2

typedef struct _kbd_device {
    int devId;
    int controlEndp;
    int interruptEndp;
    int interfaceNumber;
    int sema;
    u8 enabled;
    u8 kbd_buf[8];
    u8 ps2_data[6];
} kbd_device_t;

static kbd_device_t kbd_pads[MAX_PADS];

int usb_probe(int devId);
int usb_connect(int devId);
int usb_disconnect(int devId);

static UsbDriver kbd_driver = {
    NULL, NULL,
    "kbdusb",
    usb_probe,
    usb_connect,
    usb_disconnect
};

// Traduz os 8 bytes do teclado USB para os 6 bytes do DualShock 2 (Configuração KOF)
static void traduzir_teclado(u8 *kbd_buf, u8 *ps2_data)
{
    u16 botoes = 0xFFFF; // 0xFFFF = todos os botões soltos
    u8 lx = 0x7F, ly = 0x7F, rx = 0x7F, ry = 0x7F; // Analógicos centralizados

    for (int i = 2; i < 8; i++) {
        u8 tecla = *(kbd_buf + i);
        if (tecla == 0) continue;

        switch (tecla) {
            // Movimentação no D-Pad Digital (WASD)
            case 0x1A: botoes &= ~(1 << 4); break; // W -> Cima
            case 0x16: botoes &= ~(1 << 6); break; // S -> Baixo
            case 0x04: botoes &= ~(1 << 7); break; // A -> Esquerda
            case 0x07: botoes &= ~(1 << 5); break; // D -> Direita

            // Os 4 Botões de Ataque do KOF
            case 0x0F: botoes &= ~(1 << 15); break; // L             -> Quadrado (Soco Fraco)
            case 0x33: botoes &= ~(1 << 14); break; // Ç             -> X (Chute Fraco)
            case 0x50: botoes &= ~(1 << 12); break; // Seta Esquerda -> Triângulo (Soco Forte)
            case 0x4F: botoes &= ~(1 << 13); break; // Seta Direita  -> Bola (Chute Forte)

            // Atalhos Extras
            case 0x2C: botoes &= ~(1 << 11); break; // Espaço -> R1
            case 0x0E: botoes &= ~(1 << 10); break; // K      -> L1

            // Start e Select
            case 0x28: botoes &= ~(1 << 3); break;  // Enter     -> START
            case 0x2A: botoes &= ~(1 << 0); break;  // Backspace -> SELECT
        }
    }

    *(ps2_data + 0) = botoes & 0xFF;
    *(ps2_data + 1) = (botoes >> 8) & 0xFF;
    *(ps2_data + 2) = rx;
    *(ps2_data + 3) = ry;
    *(ps2_data + 4) = lx;
    *(ps2_data + 5) = ly;
}

static void usb_read_cb(int resultCode, int bytes, void *arg)
{
    kbd_device_t *pad = (kbd_device_t *)arg;
    if (resultCode == USB_RC_OK && bytes >= 8) {
        traduzir_teclado(pad->kbd_buf, pad->ps2_data);
    }
    SignalSema(pad->sema);
}

static void kbd_poll(int pad_idx)
{
    kbd_device_t *pad = &kbd_pads[pad_idx];
    if (!pad->enabled || pad->interruptEndp < 0) return;

    if (PollSema(pad->sema) == 0) {
        UsbInterruptTransfer(pad->interruptEndp, pad->kbd_buf, 8, usb_read_cb, pad);
    }
}

int usb_probe(int devId)
{
    UsbDeviceDescriptor *dev = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    if (!dev) return 0;

    UsbConfigDescriptor *conf = (UsbConfigDescriptor *)UsbGetDeviceStaticDescriptor(devId, dev, USB_DT_CONFIG);
    if (!conf) return 0;

    UsbInterfaceDescriptor *intf = (UsbInterfaceDescriptor *)UsbGetDeviceStaticDescriptor(devId, conf, USB_DT_INTERFACE);
    if (!intf) return 0;

    if (intf->bInterfaceClass == USB_CLASS_HID &&
        intf->bInterfaceSubClass == USB_SUBCLASS_BOOT &&
        intf->bInterfaceProtocol == USB_PROTOCOL_KEYBOARD) {
        return 1;
    }

    return 0;
}

int usb_connect(int devId)
{
    int pad_idx = -1;
    for (int i = 0; i < MAX_PADS; i++) {
        if (kbd_pads[i].devId == -1) {
            pad_idx = i;
            break;
        }
    }
    if (pad_idx == -1) return -1;

    UsbDeviceDescriptor *dev = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    UsbConfigDescriptor *conf = (UsbConfigDescriptor *)UsbGetDeviceStaticDescriptor(devId, dev, USB_DT_CONFIG);
    UsbInterfaceDescriptor *intf = (UsbInterfaceDescriptor *)UsbGetDeviceStaticDescriptor(devId, conf, USB_DT_INTERFACE);
    UsbEndpointDescriptor *endp = (UsbEndpointDescriptor *)UsbGetDeviceStaticDescriptor(devId, intf, USB_DT_ENDPOINT);

    if (!endp) return -1;

    kbd_device_t *pad = &kbd_pads[pad_idx];
    pad->devId = devId;
    pad->interfaceNumber = intf->bInterfaceNumber;
    pad->controlEndp = UsbOpenEndpoint(devId, NULL);
    pad->interruptEndp = UsbOpenEndpoint(devId, endp);

    // Zera estado inicial do controle (tudo solto)
    memset(pad->kbd_buf, 0, 8);
    traduzir_teclado(pad->kbd_buf, pad->ps2_data);

    // Ativa a configuração USB e força o Boot Protocol (8 bytes)
    UsbSetDeviceConfiguration(pad->controlEndp, conf->bConfigurationValue, NULL, NULL);
    UsbControlTransfer(pad->controlEndp, 0x21, 0x0B, 0x0000, pad->interfaceNumber, 0, NULL, NULL, NULL);

    pad->enabled = 1;
    SignalSema(pad->sema);

    return 0;
}

int usb_disconnect(int devId)
{
    for (int i = 0; i < MAX_PADS; i++) {
        if (kbd_pads[i].devId == devId) {
            kbd_pads[i].enabled = 0;
            kbd_pads[i].devId = -1;
            if (kbd_pads[i].interruptEndp >= 0) UsbCloseEndpoint(kbd_pads[i].interruptEndp);
            if (kbd_pads[i].controlEndp >= 0) UsbCloseEndpoint(kbd_pads[i].controlEndp);
            kbd_pads[i].interruptEndp = -1;
            kbd_pads[i].controlEndp = -1;
            break;
        }
    }
    return 0;
}

// --- FUNÇÕES EXIGIDAS PELO NÚCLEO DO PADEMU DO OPL ---

int ds34usb_init(u8 pads, u8 options)
{
    iop_sema_t sema;
    sema.attr = 0;
    sema.initial = 0;
    sema.max = 1;
    sema.option = 0;

    for (int i = 0; i < MAX_PADS; i++) {
        kbd_pads[i].devId = -1;
        kbd_pads[i].controlEndp = -1;
        kbd_pads[i].interruptEndp = -1;
        kbd_pads[i].enabled = 0;
        kbd_pads[i].sema = CreateSema(&sema);
        memset(kbd_pads[i].kbd_buf, 0, 8);
        traduzir_teclado(kbd_pads[i].kbd_buf, kbd_pads[i].ps2_data);
    }

    if (UsbRegisterDriver(&kbd_driver) != USB_RC_OK) {
        return 0;
    }
    return 1;
}

int ds34usb_get_status(int port)
{
    if (port >= MAX_PADS) return 0;
    return kbd_pads[port].enabled ? 1 : 0;
}

int ds34usb_get_data(u8 *dst, int size, int port)
{
    if (port >= MAX_PADS || !kbd_pads[port].enabled) return 0;

    kbd_poll(port);

    int copy_size = (size < 6) ? size : 6;
    memcpy(dst, kbd_pads[port].ps2_data, copy_size);
    return copy_size;
}

void ds34usb_set_rumble(u8 lrum, u8 rrum, int port) { /* Teclado não tem vibração */ }
void ds34usb_set_mode(int mode, int lock, int port) { /* Modo fixo digital/analógico */ }
void ds34usb_reset(void)
{
    for (int i = 0; i < MAX_PADS; i++) {
        if (kbd_pads[i].devId != -1) {
            usb_disconnect(kbd_pads[i].devId);
        }
    }
}
