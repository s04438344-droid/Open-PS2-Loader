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

#define X360_INTERFACE_CLASS    0xFF
#define X360_INTERFACE_SUBCLASS 0x5D
#define X360_INTERFACE_PROTOCOL 0x01

#define MAX_PADS 2

typedef struct _x360_device {
    int devId;
    int controlEndp;
    int interruptInEndp;
    int interruptOutEndp;
    int interfaceNumber;
    int sema;
    u8 enabled;
    u64 x360_buf_0 __attribute__((aligned(64)));
    u64 x360_buf_1;
    u64 x360_buf_2;
    u64 x360_buf_3;
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
} x360_device_t;

static x360_device_t x360_pads_0 __attribute__((aligned(64)));
static x360_device_t x360_pads_1 __attribute__((aligned(64)));
static u32 led_cmd_raw __attribute__((aligned(64))) = 0x00060301;

static x360_device_t *get_pad(int idx)
{
    return (idx == 0) ? &x360_pads_0 : &x360_pads_1;
}

int usb_probe(int devId);
int usb_connect(int devId);
int usb_disconnect(int devId);

static UsbDriver x360_driver = {
    NULL, NULL,
    "x360usb",
    usb_probe,
    usb_connect,
    usb_disconnect
};

static u8 converter_eixo(s16 val, int inverter_y)
{
    int v = val;
    if (inverter_y) {
        v = -v - 1;
    }
    u8 res = (u8)((v + 32768) >> 8);
    if (res >= 0x78 && res <= 0x86) {
        return 0x7F;
    }
    return res;
}

static void traduzir_x360(x360_device_t *pad)
{
    u8 *buf = (u8 *)&pad->x360_buf_0;

    if (*(buf + 0) != 0x00 || *(buf + 1) != 0x14) {
        return;
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

    u8 btn_low  = *(buf + 2);
    u8 btn_high = *(buf + 3);
    u8 lt       = *(buf + 4);
    u8 rt       = *(buf + 5);

    if (btn_low & 0x01) { botoes &= ~(1 << 4); pad->press_up    = 0xFF; }
    if (btn_low & 0x02) { botoes &= ~(1 << 6); pad->press_down  = 0xFF; }
    if (btn_low & 0x04) { botoes &= ~(1 << 7); pad->press_left  = 0xFF; }
    if (btn_low & 0x08) { botoes &= ~(1 << 5); pad->press_right = 0xFF; }
    if (btn_low & 0x10) { botoes &= ~(1 << 3); }
    if (btn_low & 0x20) { botoes &= ~(1 << 0); }
    if (btn_low & 0x40) { botoes &= ~(1 << 1); }
    if (btn_low & 0x80) { botoes &= ~(1 << 2); }

    if (btn_high & 0x01) { botoes &= ~(1 << 10); pad->press_l1  = 0xFF; }
    if (btn_high & 0x02) { botoes &= ~(1 << 11); pad->press_r1  = 0xFF; }
    if (btn_high & 0x10) { botoes &= ~(1 << 14); pad->press_cro = 0xFF; }
    if (btn_high & 0x20) { botoes &= ~(1 << 13); pad->press_cir = 0xFF; }
    if (btn_high & 0x40) { botoes &= ~(1 << 15); pad->press_squ = 0xFF; }
    if (btn_high & 0x80) { botoes &= ~(1 << 12); pad->press_tri = 0xFF; }

    if (lt > 30) {
        botoes &= ~(1 << 8);
        pad->press_l2 = lt;
    }
    if (rt > 30) {
        botoes &= ~(1 << 9);
        pad->press_r2 = rt;
    }

    if (btn_high & 0x04) {
        botoes &= ~((1 << 10) | (1 << 8) | (1 << 11) | (1 << 9) | (1 << 3) | (1 << 0));
        pad->press_l1 = 0xFF;
        pad->press_l2 = 0xFF;
        pad->press_r1 = 0xFF;
        pad->press_r2 = 0xFF;
    }

    s16 lx = (s16)(*(buf + 6)  | (*(buf + 7)  << 8));
    s16 ly = (s16)(*(buf + 8)  | (*(buf + 9)  << 8));
    s16 rx = (s16)(*(buf + 10) | (*(buf + 11) << 8));
    s16 ry = (s16)(*(buf + 12) | (*(buf + 13) << 8));

    pad->ps2_data_0 = botoes & 0xFF;
    pad->ps2_data_1 = (botoes >> 8) & 0xFF;
    pad->ps2_data_2 = converter_eixo(rx, 0);
    pad->ps2_data_3 = converter_eixo(ry, 1);
    pad->ps2_data_4 = converter_eixo(lx, 0);
    pad->ps2_data_5 = converter_eixo(ly, 1);
}

static void resetar_estado_pad(x360_device_t *pad)
{
    pad->x360_buf_0 = 0;
    pad->x360_buf_1 = 0;
    pad->x360_buf_2 = 0;
    pad->x360_buf_3 = 0;
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

static void usb_read_cb(int resultCode, int bytes, void *arg)
{
    x360_device_t *pad = (x360_device_t *)arg;
    if (resultCode == USB_RC_OK && bytes >= 14) {
        traduzir_x360(pad);
    }
    SignalSema(pad->sema);
}

static void x360_poll(int pad_idx)
{
    x360_device_t *pad = get_pad(pad_idx);
    if (!pad->enabled || pad->interruptInEndp < 0) return;

    if (PollSema(pad->sema) == 0) {
        UsbInterruptTransfer(pad->interruptInEndp, (u8 *)&pad->x360_buf_0, 20, usb_read_cb, pad);
    }
}

static UsbInterfaceDescriptor *find_x360_interface(int devId, UsbConfigDescriptor *conf)
{
    UsbInterfaceDescriptor *intf = (UsbInterfaceDescriptor *)UsbGetDeviceStaticDescriptor(devId, conf, USB_DT_INTERFACE);
    while (intf != NULL) {
        if (intf->bInterfaceClass == X360_INTERFACE_CLASS &&
            intf->bInterfaceSubClass == X360_INTERFACE_SUBCLASS &&
            intf->bInterfaceProtocol == X360_INTERFACE_PROTOCOL) {
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

    if (find_x360_interface(devId, conf) != NULL) {
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
    UsbInterfaceDescriptor *intf = find_x360_interface(devId, conf);
    if (!intf) return -1;

    UsbEndpointDescriptor *endpIn = NULL;
    UsbEndpointDescriptor *endpOut = NULL;

    UsbEndpointDescriptor *endp = (UsbEndpointDescriptor *)UsbGetDeviceStaticDescriptor(devId, intf, USB_DT_ENDPOINT);
    while (endp != NULL) {
        if ((endp->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT) {
            if ((endp->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN && !endpIn) {
                endpIn = endp;
            } else if ((endp->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT && !endpOut) {
                endpOut = endp;
            }
        }
        endp = (UsbEndpointDescriptor *)UsbGetDeviceStaticDescriptor(devId, endp, USB_DT_ENDPOINT);
    }
    if (!endpIn) return -1;

    x360_device_t *pad = get_pad(pad_idx);
    pad->devId = devId;
    pad->interfaceNumber = intf->bInterfaceNumber;
    pad->controlEndp = UsbOpenEndpoint(devId, NULL);
    pad->interruptInEndp = UsbOpenEndpoint(devId, endpIn);
    pad->interruptOutEndp = endpOut ? UsbOpenEndpoint(devId, endpOut) : -1;

    resetar_estado_pad(pad);

    UsbSetDeviceConfiguration(pad->controlEndp, conf->bConfigurationValue, NULL, NULL);

    if (pad->interruptOutEndp >= 0) {
        u8 *led_ptr = (u8 *)&led_cmd_raw;
        *(led_ptr + 0) = 0x01;
        *(led_ptr + 1) = 0x03;
        *(led_ptr + 2) = (pad_idx == 0) ? 0x06 : 0x07;
        UsbInterruptTransfer(pad->interruptOutEndp, led_ptr, 3, NULL, NULL);
    }

    pad->enabled = 1;
    SignalSema(pad->sema);

    return 0;
}

int usb_disconnect(int devId)
{
    for (int i = 0; i < MAX_PADS; i++) {
        x360_device_t *pad = get_pad(i);
        if (pad->devId == devId) {
            pad->enabled = 0;
            pad->devId = -1;
            if (pad->interruptInEndp >= 0) UsbCloseEndpoint(pad->interruptInEndp);
            if (pad->interruptOutEndp >= 0) UsbCloseEndpoint(pad->interruptOutEndp);
            if (pad->controlEndp >= 0) UsbCloseEndpoint(pad->controlEndp);
            pad->interruptInEndp = -1;
            pad->interruptOutEndp = -1;
            pad->controlEndp = -1;
            resetar_estado_pad(pad);
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
        x360_device_t *pad = get_pad(i);
        pad->devId = -1;
        pad->controlEndp = -1;
        pad->interruptInEndp = -1;
        pad->interruptOutEndp = -1;
        pad->enabled = 0;
        pad->sema = CreateSema(&sema);
        resetar_estado_pad(pad);
    }

    if (UsbRegisterDriver(&x360_driver) != USB_RC_OK) {
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

    x360_poll(port);

x360_device_t *pad = get_pad(port);
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
x360_device_t *pad = get_pad(i);
if (pad->devId != -1) {
usb_disconnect(pad->devId);
}
}
}
