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
    u8 kbd_buf_raw __attribute__((aligned(64)));
    u8 ps2_data_0 __attribute__((aligned(64)));
    u8 ps2_data_1;
    u8 ps2_data_2;
    u8 ps2_data_3;
    u8 ps2_data_4;
    u8 ps2_data_5;
    u8 press_right;
    u8 press_left;
    u8 press_up;
    u8 press_down;
    u8 press_tri;
    u8 press_cir;
    u8 press_cro;
    u8 press_squ;
    u8 press_l1;
    u8 press_r1;
    u8 press_l2;
    u8 press_r2;
} kbd_device_t;

static kbd_device_t kbd_pads_0 __attribute__((aligned(64)));
static kbd_device_t kbd_pads_1 __attribute__((aligned(64)));

static kbd_device_t *get_pad(int idx)
{
    return (idx == 0) ? &kbd_pads_0 : &kbd_pads_1;
}

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

// Traduz os 8 bytes do teclado USB para os 18 bytes completos do DualShock 2 (Configuração KOF + Saída IGR)
static void traduzir_teclado(kbd_device_t *pad)
{
    u8 *kbd_buf = (u8 *)&pad->kbd_buf_raw;
    u16 botoes = 0xFFFF; // 0xFFFF = todos os botões soltos

    pad->press_right = 0;
    pad->press_left  = 0;
    pad->press_up    = 0;
    pad->press_down  = 0;
    pad->press_tri   = 0;
    pad->press_cir   = 0;
    pad->press_cro   = 0;
    pad->press_squ   = 0;
    pad->press_l1    = 0;
    pad->press_r1    = 0;
    pad->press_l2    = 0;
    pad->press_r2    = 0;

    for (int i = 2; i < 8; i++) {
        u8 tecla = *(kbd_buf + i);
        if (tecla == 0) continue;

        switch (tecla) {
            // Movimentação no D-Pad Digital (WASD)
            case 0x1A: botoes &= ~(1 << 4); pad->press_up    = 0xFF; break; // W -> Cima
            case 0x16: botoes &= ~(1 << 6); pad->press_down  = 0xFF; break; // S -> Baixo
            case 0x04: botoes &= ~(1 << 7); pad->press_left  = 0xFF; break; // A -> Esquerda
            case 0x07: botoes &= ~(1 << 5); pad->press_right = 0xFF; break; // D -> Direita

            // Os 4 Botões de Ataque do KOF
            case 0x0F: botoes &= ~(1 << 15); pad->press_squ = 0xFF; break; // L          -> Quadrado (A - Soco Fraco)
            case 0x33: botoes &= ~(1 << 14); pad->press_cro = 0xFF; break; // Ç          -> X (B - Chute Fraco)
            case 0x36: botoes &= ~(1 << 12); pad->press_tri = 0xFF; break; // < , (Esq.) -> Triângulo (C - Soco Forte)
            case 0x37: botoes &= ~(1 << 13); pad->press_cir = 0xFF; break; // > . (Dir.) -> Bola (D - Chute Forte)

            // Os 4 Gatilhos (L1, L2, R1, R2)
            case 0x0C: botoes &= ~(1 << 10); pad->press_l1 = 0xFF; break; // I -> L1
            case 0x12: botoes &= ~(1 << 8);  pad->press_l2 = 0xFF; break; // O -> L2
            case 0x13: botoes &= ~(1 << 11); pad->press_r1 = 0xFF; break; // P -> R1
            case 0x0E: botoes &= ~(1 << 9);  pad->press_r2 = 0xFF; break; // K -> R2

            // Start e Select
            case 0x28: botoes &= ~(1 << 3); break;  // Enter     -> START
            case 0x2A: botoes &= ~(1 << 0); break;  // Backspace -> SELECT

            // ATALHO DE SAÍDA DIRETA DO OPL (IGR: L1 + L2 + R1 + R2 + Start + Select nas teclas ESC ou Y)
            case 0x29: // ESC
            case 0x1C: // Y (Atalho extra direto para o OPL)
                botoes &= ~((1 << 10) | (1 << 8) | (1 << 11) | (1 << 9) | (1 << 3) | (1 << 0));
                pad->press_l1 = 0xFF;
                pad->press_l2 = 0xFF;
                pad->press_r1 = 0xFF;
                pad->press_r2 = 0xFF;
                break;
        }
    }

    pad->ps2_data_0 = botoes & 0xFF;
    pad->ps2_data_1 = (botoes >> 8) & 0xFF;
    pad->ps2_data_2 = 0x7F; // Analógico Direito X (Centro)
    pad->ps2_data_3 = 0x7F; // Analógico Direito Y (Centro)
    pad->ps2_data_4 = 0x7F; // Analógico Esquerdo X (Centro)
    pad->ps2_data_5 = 0x7F; // Analógico Esquerdo Y (Centro)
}

static void usb_read_cb(int resultCode, int bytes, void *arg)
{
    kbd_device_t *pad = (kbd_device_t *)arg;
    if (resultCode == USB_RC_OK && bytes >= 8) {
        traduzir_teclado(pad);
    }
    SignalSema(pad->sema);
}

static void kbd_poll(int pad_idx)
{
    kbd_device_t *pad = get_pad(pad_idx);
    if (!pad->enabled || pad->interruptEndp < 0) return;

    if (PollSema(pad->sema) == 0) {
        UsbInterruptTransfer(pad->interruptEndp, (u8 *)&pad->kbd_buf_raw, 8, usb_read_cb, pad);
    }
}

static UsbInterfaceDescriptor *find_kbd_interface(int devId, UsbConfigDescriptor *conf)
{
    UsbInterfaceDescriptor *intf = (UsbInterfaceDescriptor *)UsbGetDeviceStaticDescriptor(devId, conf, USB_DT_INTERFACE);
    while (intf != NULL) {
        if (intf->bInterfaceClass == USB_CLASS_HID &&
            intf->bInterfaceSubClass == USB_SUBCLASS_BOOT &&
            intf->bInterfaceProtocol == USB_PROTOCOL_KEYBOARD) {
            return intf;
        }
        intf = (UsbInterfaceDescriptor *)UsbGetDeviceStaticDescriptor(devId, intf, USB_DT_INTERFACE);
    }
    return NULL;
}

int usb_probe(int devId)
{
    UsbDeviceDescriptor *dev = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    if (!dev) return 0;

    UsbConfigDescriptor *conf = (UsbConfigDescriptor *)UsbGetDeviceStaticDescriptor(devId, dev, USB_DT_CONFIG);
    if (!conf) return 0;

    if (find_kbd_interface(devId, conf) != NULL) {
        return 1;
    }

    return 0;
}

int usb_connect(int devId)
{
    int pad_idx = -1;
    for (int i = 0; i < MAX_PADS; i++) {
        if (get_pad(i)->devId == -1) {
            pad_idx = i;
            break;
        }
    }
    if (pad_idx == -1) return -1;

    UsbDeviceDescriptor *dev = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    UsbConfigDescriptor *conf = (UsbConfigDescriptor *)UsbGetDeviceStaticDescriptor(devId, dev, USB_DT_CONFIG);
    UsbInterfaceDescriptor *intf = find_kbd_interface(devId, conf);
    if (!intf) return -1;

    UsbEndpointDescriptor *endp = (UsbEndpointDescriptor *)UsbGetDeviceStaticDescriptor(devId, intf, USB_DT_ENDPOINT);
    while (endp != NULL) {
        if ((endp->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN &&
            (endp->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT) {
            break;
        }
        endp = (UsbEndpointDescriptor *)UsbGetDeviceStaticDescriptor(devId, endp, USB_DT_ENDPOINT);
    }
    if (!endp) return -1;

    kbd_device_t *pad = get_pad(pad_idx);
    pad->devId = devId;
    pad->interfaceNumber = intf->bInterfaceNumber;
    pad->controlEndp = UsbOpenEndpoint(devId, NULL);
    pad->interruptEndp = UsbOpenEndpoint(devId, endp);

    pad->kbd_buf_raw = 0;
    traduzir_teclado(pad);

    UsbSetDeviceConfiguration(pad->controlEndp, conf->bConfigurationValue, NULL, NULL);
    UsbControlTransfer(pad->controlEndp, 0x21, 0x0B, 0x0000, pad->interfaceNumber, 0, NULL, NULL, NULL);

    pad->enabled = 1;
    SignalSema(pad->sema);

    return 0;
}

int usb_disconnect(int devId)
{
    for (int i = 0; i < MAX_PADS; i++) {
        kbd_device_t *pad = get_pad(i);
        if (pad->devId == devId) {
            pad->enabled = 0;
            pad->devId = -1;
            if (pad->interruptEndp >= 0) UsbCloseEndpoint(pad->interruptEndp);
            if (pad->controlEndp >= 0) UsbCloseEndpoint(pad->controlEndp);
            pad->interruptEndp = -1;
            pad->controlEndp = -1;
            break;
        }
    }
    return 0;
}

int ds34usb_init(u8 pads, u8 options)
{
    iop_sema_t sema;
    sema.attr = 0;
    sema.initial = 0;
    sema.max = 1;
    sema.option = 0;

    for (int i = 0; i < MAX_PADS; i++) {
        kbd_device_t *pad = get_pad(i);
        pad->devId = -1;
        pad->controlEndp = -1;
        pad->interruptEndp = -1;
        pad->enabled = 0;
        pad->sema = CreateSema(&sema);
        pad->kbd_buf_raw = 0;
        traduzir_teclado(pad);
    }

    if (UsbRegisterDriver(&kbd_driver) != USB_RC_OK) {
        return 0;
    }
    return 1;
}

int ds34usb_get_status(int port)
{
    if (port >= MAX_PADS) return DS34USB_STATE_DISCONNECTED;
    return get_pad(port)->enabled ? DS34USB_STATE_RUNNING : DS34USB_STATE_DISCONNECTED;
}

int ds34usb_get_model(int port)
{
    return MODEL_PS2;
}

int ds34usb_get_data(u8 *dst, int size, int port)
{
    if (port >= MAX_PADS || !get_pad(port)->enabled) return 0;

    kbd_poll(port);

    kbd_device_t *pad = get_pad(port);
    u8 *src = &pad->ps2_data_0;
    int copy_size = (size < 18) ? size : 18;
    for (int i = 0; i < copy_size; i++) {
        *(dst + i) = *(src + i);
    }
    return 1;
}

void ds34usb_set_rumble(u8 lrum, u8 rrum, int port) { }
void ds34usb_set_mode(int mode, int lock, int port) { }
void ds34usb_reset()
{
    for (int i = 0; i < MAX_PADS; i++) {
        kbd_device_t *pad = get_pad(i);
        if (pad->devId != -1) {
            usb_disconnect(pad->devId);
        }
    }
}
