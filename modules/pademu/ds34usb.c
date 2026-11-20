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

#define MAX_PADS 2

typedef struct _hid_device {
    int devId;
    int controlEndp;
    int interruptInEndp;
    int maxPacketSize;
    int interfaceNumber;
    u8 enabled;
    u64 hid_buf_0 __attribute__((aligned(64)));
    u64 hid_buf_1;
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
} hid_device_t;

static hid_device_t hid_pads_0 __attribute__((aligned(64)));
static hid_device_t hid_pads_1 __attribute__((aligned(64)));

static hid_device_t *get_pad(int idx)
{
    return (idx == 0) ? &hid_pads_0 : &hid_pads_1;
}

int usb_probe(int devId);
int usb_connect(int devId);
int usb_disconnect(int devId);
static void usb_read_cb(int resultCode, int bytes, void *arg);

static UsbDriver hid_driver = {
    NULL, NULL,
    "stickusb",
    usb_probe,
    usb_connect,
    usb_disconnect
};

static u8 limpar_drift(u8 val)
{
    if (val >= 0x76 && val <= 0x89) {
        return 0x7F;
    }
    return val;
}

// Traduz os 8 bytes padrão HID (Game Stick 2.4GHz) para o DualShock 2
static void traduzir_gamestick(hid_device_t *pad, int bytes)
{
    u8 *buf = (u8 *)&pad->hid_buf_0;
    if (bytes < 7) return;

    // Alguns receptores de 2 controles mandam o ID do controle (0x01 ou 0x02) no Byte 0
    // Se bytes >= 8 e o Byte 0 for 1 ou 2 enquanto Byte 1..4 parecem eixos (0x7F/0x80), desloca 1 byte
    if (bytes >= 8 && (*(buf + 0) == 0x01 || *(buf + 0) == 0x02) &&
        (*(buf + 1) >= 0x70 && *(buf + 1) <= 0x90)) {
        buf = buf + 1;
    }

    u16 botoes = 0xFFFF;

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

    u8 lx   = *(buf + 0);
    u8 ly   = *(buf + 1);
    u8 rx   = *(buf + 2);
    u8 ry   = *(buf + 3);
    u8 b5   = *(buf + 5); // D-Pad (nibble baixo) + Botões de face (nibble alto)
    u8 b6   = *(buf + 6); // L1, R1, L2, R2, Select, Start, L3, R3

    // --- D-Pad (Hat Switch nos 4 bits baixos do Byte 5) ---
    u8 hat = b5 & 0x0F;
    if (hat == 0 || hat == 1 || hat == 7) { botoes &= ~(1 << 4); pad->press_up    = 0xFF; } // Cima
    if (hat == 3 || hat == 4 || hat == 5) { botoes &= ~(1 << 6); pad->press_down  = 0xFF; } // Baixo
    if (hat == 5 || hat == 6 || hat == 7) { botoes &= ~(1 << 7); pad->press_left  = 0xFF; } // Esquerda
    if (hat == 1 || hat == 2 || hat == 3) { botoes &= ~(1 << 5); pad->press_right = 0xFF; } // Direita

    // --- Botões de Face (4 bits altos do Byte 5) ---
    if (b5 & 0x10) { botoes &= ~(1 << 12); pad->press_tri = 0xFF; } // Triângulo (1 / Y)
    if (b5 & 0x20) { botoes &= ~(1 << 13); pad->press_cir = 0xFF; } // Bola (2 / B)
    if (b5 & 0x40) { botoes &= ~(1 << 14); pad->press_cro = 0xFF; } // X (3 / A)
    if (b5 & 0x80) { botoes &= ~(1 << 15); pad->press_squ = 0xFF; } // Quadrado (4 / X)

    // --- Botões de Ombro e Centrais (Byte 6) ---
    if (b6 & 0x01) { botoes &= ~(1 << 10); pad->press_l1 = 0xFF; }  // L1
    if (b6 & 0x02) { botoes &= ~(1 << 11); pad->press_r1 = 0xFF; }  // R1
    if (b6 & 0x04) { botoes &= ~(1 << 8);  pad->press_l2 = 0xFF; }  // L2
    if (b6 & 0x08) { botoes &= ~(1 << 9);  pad->press_r2 = 0xFF; }  // R2
    if (b6 & 0x10) { botoes &= ~(1 << 0); }                         // Select
    if (b6 & 0x20) { botoes &= ~(1 << 3); }                         // Start
    if (b6 & 0x40) { botoes &= ~(1 << 1); }                         // L3
    if (b6 & 0x80) { botoes &= ~(1 << 2); }                         // R3

    // --- Atalho IGR no PS2: Start + Select + L1 + R1 juntos ---
    if ((b6 & 0x33) == 0x33) {
        botoes &= ~((1 << 10) | (1 << 8) | (1 << 11) | (1 << 9) | (1 << 3) | (1 << 0));
        pad->press_l1 = 0xFF;
        pad->press_l2 = 0xFF;
        pad->press_r1 = 0xFF;
        pad->press_r2 = 0xFF;
    }

    pad->ps2_data_0 = botoes & 0xFF;
    pad->ps2_data_1 = (botoes >> 8) & 0xFF;
    pad->ps2_data_2 = limpar_drift(rx); // Analógico Direito X
    pad->ps2_data_3 = limpar_drift(ry); // Analógico Direito Y
    pad->ps2_data_4 = limpar_drift(lx); // Analógico Esquerdo X
    pad->ps2_data_5 = limpar_drift(ly); // Analógico Esquerdo Y
}

static void resetar_estado_pad(hid_device_t *pad)
{
    pad->hid_buf_0 = 0;
    pad->hid_buf_1 = 0;
    pad->ps2_data_0 = 0xFF;
    pad->ps2_data_1 = 0xFF;
    pad->ps2_data_2 = 0x7F;
    pad->ps2_data_3 = 0x7F;
    pad->ps2_data_4 = 0x7F;
    pad->ps2_data_5 = 0x7F;
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
}

static void agendar_proxima_leitura(hid_device_t *pad)
{
    if (pad->enabled && pad->interruptInEndp >= 0) {
        int len = (pad->maxPacketSize > 0 && pad->maxPacketSize <= 16) ? pad->maxPacketSize : 8;
        UsbInterruptTransfer(pad->interruptInEndp, (u8 *)&pad->hid_buf_0, len, usb_read_cb, pad);
    }
}

static void usb_read_cb(int resultCode, int bytes, void *arg)
{
    hid_device_t *pad = (hid_device_t *)arg;
    if (!pad || !pad->enabled) return;

    if (resultCode == USB_RC_OK && bytes > 0) {
        traduzir_gamestick(pad, bytes);
    }
    agendar_proxima_leitura(pad);
}

static UsbInterfaceDescriptor *find_hid_gamepad(int devId, UsbConfigDescriptor *conf)
{
    UsbInterfaceDescriptor *intf = (UsbInterfaceDescriptor *)UsbGetDeviceStaticDescriptor(devId, conf, USB_DT_INTERFACE);
    while (intf != NULL) {
        // Classe HID (0x03), ignorando Teclado/Mouse Boot (SubClass 0x01)
        if (intf->bInterfaceClass == 0x03 && intf->bInterfaceSubClass != 0x01) {
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

    if (find_hid_gamepad(devId, conf) != NULL) {
        return 1;
    }

    return 0;
}

static void usb_config_cb(int resultCode, int bytes, void *arg)
{
    hid_device_t *pad = (hid_device_t *)arg;
    if (!pad || !pad->enabled) return;

    // SET_IDLE (0x0A) para o receptor HID enviar pacotes continuamente
    UsbControlTransfer(pad->controlEndp, 0x21, 0x0A, 0x0000, pad->interfaceNumber, 0, NULL, NULL, NULL);

    agendar_proxima_leitura(pad);
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
    UsbInterfaceDescriptor *intf = find_hid_gamepad(devId, conf);
    if (!intf) return -1;

    UsbEndpointDescriptor *endpIn = NULL;
    UsbEndpointDescriptor *endp = (UsbEndpointDescriptor *)UsbGetDeviceStaticDescriptor(devId, intf, USB_DT_ENDPOINT);
    while (endp != NULL) {
        if ((endp->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN &&
            (endp->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT) {
            endpIn = endp;
            break;
        }
        endp = (UsbEndpointDescriptor *)UsbGetDeviceStaticDescriptor(devId, endp, USB_DT_ENDPOINT);
    }
    if (!endpIn) return -1;

    hid_device_t *pad = get_pad(pad_idx);
    pad->devId = devId;
    pad->interfaceNumber = intf->bInterfaceNumber;
    pad->maxPacketSize = endpIn->wMaxPacketSize;
    pad->controlEndp = UsbOpenEndpoint(devId, NULL);
    pad->interruptInEndp = UsbOpenEndpoint(devId, endpIn);

    resetar_estado_pad(pad);
    pad->enabled = 1;

    UsbSetDeviceConfiguration(pad->controlEndp, conf->bConfigurationValue, usb_config_cb, pad);

    return 0;
}

int usb_disconnect(int devId)
{
    for (int i = 0; i < MAX_PADS; i++) {
        hid_device_t *pad = get_pad(i);
        if (pad->devId == devId) {
            pad->enabled = 0;
            pad->devId = -1;
            if (pad->interruptInEndp >= 0) UsbCloseEndpoint(pad->interruptInEndp);
            if (pad->controlEndp >= 0) UsbCloseEndpoint(pad->controlEndp);
            pad->interruptInEndp = -1;
            pad->controlEndp = -1;
            resetar_estado_pad(pad);
            break;
        }
    }
    return 0;
}

int ds34usb_init(u8 pads, u8 options)
{
    for (int i = 0; i < MAX_PADS; i++) {
        hid_device_t *pad = get_pad(i);
        pad->devId = -1;
        pad->controlEndp = -1;
        pad->interruptInEndp = -1;
        pad->maxPacketSize = 8;
        pad->enabled = 0;
        resetar_estado_pad(pad);
    }

    if (UsbRegisterDriver(&hid_driver) != USB_RC_OK) {
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
