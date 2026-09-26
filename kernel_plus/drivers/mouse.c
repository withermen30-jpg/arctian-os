
#include "kernel.h"
#include "isr.h"
#include "pic.h"
#include "mouse.h"

static int usb_mouse_dx = 0;
static int usb_mouse_dy = 0;
static uint8_t usb_mouse_buttons = 0;


#define PS2_DATA            0x60
#define PS2_STATUS          0x64
#define PS2_CMD             0x64

#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_PORT2   0xA7
#define PS2_CMD_ENABLE_PORT2    0xA8
#define PS2_CMD_TEST_PORT2      0xA9

#define MOUSE_CMD_RESET         0xFF
#define MOUSE_CMD_RESEND        0xFE
#define MOUSE_CMD_SET_DEFAULTS  0xF6
#define MOUSE_CMD_DISABLE       0xF5
#define MOUSE_CMD_ENABLE        0xF4
#define MOUSE_CMD_SET_SAMPLE    0xF3
#define MOUSE_CMD_SET_RATE      0xF3
#define MOUSE_CMD_GET_ID        0xF2
#define MOUSE_CMD_SET_REMOTE    0xF0
#define MOUSE_CMD_SET_WRAP      0xEE
#define MOUSE_CMD_SET_STREAM    0xEA
#define MOUSE_CMD_STATUS_REQ    0xE9
#define MOUSE_CMD_SET_RES       0xE8

#define MOUSE_ACK           0xFA
#define MOUSE_NACK          0xFE
#define MOUSE_ERROR         0xFC
#define MOUSE_SELFTEST_OK   0xAA

#define MOUSE_ID_STANDARD   0x00
#define MOUSE_ID_INTELLI    0x03
#define MOUSE_ID_INTELLI_5B 0x04


#define PACKET_3BYTE    3
#define PACKET_4BYTE    4


#define TIMEOUT_COUNT       100000
#define SHORT_DELAY         50


static mouse_state_t mouse_state;
static int mouse_packet_size = PACKET_3BYTE;
static int mouse_packet_cycle = 0;
static uint8_t mouse_packet[4];

int mouse_screen_width  = 1024;
int mouse_screen_height = 768;


static int ps2_wait_write(void) {
    int timeout = TIMEOUT_COUNT;
    while (timeout--) {
        if ((inb(PS2_STATUS) & 0x02) == 0)
            return 0;
    }
    return -1;
}

static int ps2_wait_read(void) {
    int timeout = TIMEOUT_COUNT;
    while (timeout--) {
        if (inb(PS2_STATUS) & 0x01)
            return 0;
    }
    return -1;
}

static int ps2_write_data(uint8_t data) {
    if (ps2_wait_write() != 0)
        return -1;
    outb(PS2_DATA, data);
    return 0;
}

static int ps2_write_cmd(uint8_t cmd) {
    if (ps2_wait_write() != 0)
        return -1;
    outb(PS2_CMD, cmd);
    return 0;
}

static int ps2_read_data(uint8_t *data) {
    if (ps2_wait_read() != 0)
        return -1;
    *data = inb(PS2_DATA);
    return 0;
}

static void ps2_flush_buffer(void) {
    int timeout = SHORT_DELAY;
    while (timeout--) {
        if (inb(PS2_STATUS) & 0x01) {
            inb(PS2_DATA);
        } else {
            break;
        }
    }
}


static int mouse_send_cmd(uint8_t cmd) {
    if (ps2_write_cmd(0xD4) != 0)
        return -1;
    if (ps2_write_data(cmd) != 0)
        return -1;
    uint8_t resp;
    int timeout = TIMEOUT_COUNT;
    while (timeout--) {
        if (ps2_read_data(&resp) == 0) {
            if (resp == MOUSE_ACK)
                return 0;
            if (resp == MOUSE_NACK)
                return -2;
            return -3;
        }
    }
    return -1;
}

static int mouse_expect_response(uint8_t expected) {
    uint8_t data;
    int timeout = TIMEOUT_COUNT;
    while (timeout--) {
        if (ps2_read_data(&data) == 0) {
            if (data == expected)
                return 0;
        }
    }
    return -1;
}


static bool mouse_detect_intellimouse(void) {
    mouse_send_cmd(MOUSE_CMD_SET_SAMPLE);
    mouse_send_cmd(200);
    mouse_send_cmd(MOUSE_CMD_SET_SAMPLE);
    mouse_send_cmd(100);
    mouse_send_cmd(MOUSE_CMD_SET_SAMPLE);
    mouse_send_cmd(80);

    if (mouse_send_cmd(MOUSE_CMD_GET_ID) != 0)
        return false;

    uint8_t id;
    if (ps2_read_data(&id) != 0)
        return false;

    return (id == MOUSE_ID_INTELLI || id == MOUSE_ID_INTELLI_5B);
}


void mouse_usb_move(int dx, int dy, uint8_t buttons) {
    usb_mouse_dx += dx;
    usb_mouse_dy += dy;
    usb_mouse_buttons = buttons;
}

static void mouse_process_usb(void) {
    if (usb_mouse_dx == 0 && usb_mouse_dy == 0 &&
        usb_mouse_buttons == mouse_state.buttons[0] + (mouse_state.buttons[1] << 1) + (mouse_state.buttons[2] << 2))
        return;

    mouse_state.dx = usb_mouse_dx;
    mouse_state.dy = usb_mouse_dy;

    mouse_state.x += usb_mouse_dx;
    mouse_state.y -= usb_mouse_dy;

    if (mouse_state.x < 0) mouse_state.x = 0;
    if (mouse_state.x >= mouse_screen_width) mouse_state.x = mouse_screen_width - 1;
    if (mouse_state.y < 0) mouse_state.y = 0;
    if (mouse_state.y >= mouse_screen_height) mouse_state.y = mouse_screen_height - 1;

    mouse_state.buttons[MOUSE_BUTTON_LEFT]   = (usb_mouse_buttons & 0x01) != 0;
    mouse_state.buttons[MOUSE_BUTTON_RIGHT]  = (usb_mouse_buttons & 0x02) != 0;
    mouse_state.buttons[MOUSE_BUTTON_MIDDLE] = (usb_mouse_buttons & 0x04) != 0;

    usb_mouse_dx = 0;
    usb_mouse_dy = 0;
}


static void mouse_decode_packet(void) {
    uint8_t status_byte = mouse_packet[0];
    int dx, dy;

    dx = (int)(int8_t)mouse_packet[1];
    dy = (int)(int8_t)mouse_packet[2];

    if (status_byte & 0x40) dx = (dx > 0) ? 127 : -128;
    if (status_byte & 0x80) dy = (dy > 0) ? 127 : -128;

    mouse_state.dx = dx;
    mouse_state.dy = dy;

    mouse_state.x += dx;
    mouse_state.y -= dy;

    if (mouse_state.x < 0) mouse_state.x = 0;
    if (mouse_state.x >= mouse_screen_width)
        mouse_state.x = mouse_screen_width - 1;
    if (mouse_state.y < 0) mouse_state.y = 0;
    if (mouse_state.y >= mouse_screen_height)
        mouse_state.y = mouse_screen_height - 1;

    mouse_state.buttons[MOUSE_BUTTON_LEFT]   = (status_byte & 0x01) != 0;
    mouse_state.buttons[MOUSE_BUTTON_RIGHT]  = (status_byte & 0x02) != 0;
    mouse_state.buttons[MOUSE_BUTTON_MIDDLE] = (status_byte & 0x04) != 0;

    if (mouse_packet_size == PACKET_4BYTE) {
        mouse_state.scroll = (int)(int8_t)mouse_packet[3];
    } else {
        mouse_state.scroll = 0;
    }
}

void mouse_irq_handler(void) {
    mouse_process_usb();

    uint8_t status = inb(PS2_STATUS);

    if (!(status & 0x01))
        return;

    uint8_t data = inb(PS2_DATA);

    switch (mouse_packet_cycle) {
        case 0:
            if (!(data & 0x08)) {
                return;
            }
            mouse_packet[0] = data;
            mouse_packet_cycle = 1;
            break;

        case 1:
            mouse_packet[1] = data;
            mouse_packet_cycle = 2;
            break;

        case 2:
            mouse_packet[2] = data;

            if (mouse_packet_size == PACKET_4BYTE) {
                mouse_packet_cycle = 3;
            } else {
                mouse_packet_cycle = 0;
                mouse_decode_packet();
            }
            break;

        case 3:
            mouse_packet[3] = data;
            mouse_packet_cycle = 0;
            mouse_decode_packet();
            break;

        default:
            mouse_packet_cycle = 0;
            break;
    }
}


void mouse_init(void) {
    kernel_debug("PS/2 mouse baslatiliyor...");

    mouse_state.x = mouse_screen_width / 2;
    mouse_state.y = mouse_screen_height / 2;
    mouse_state.dx = 0;
    mouse_state.dy = 0;
    mouse_state.scroll = 0;
    mouse_state.buttons[0] = false;
    mouse_state.buttons[1] = false;
    mouse_state.buttons[2] = false;
    mouse_state.present = false;
    mouse_state.scroll_wheel = false;
    mouse_packet_cycle = 0;
    mouse_packet_size = PACKET_3BYTE;

    ps2_flush_buffer();

    ps2_write_cmd(PS2_CMD_ENABLE_PORT2);
    ps2_flush_buffer();

    ps2_write_cmd(PS2_CMD_READ_CONFIG);
    uint8_t config;
    if (ps2_read_data(&config) != 0) {
        kernel_warning("PS/2 mouse: config okunamadi!");
        return;
    }

    config |= 0x02;
    config &= ~0x20;

    ps2_write_cmd(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);

    int ret;
    ret = mouse_send_cmd(MOUSE_CMD_RESET);

    if (ret != 0) {
        kernel_warning("PS/2 mouse: reset basarisiz!");
        return;
    }

    uint8_t self_test_result;
    uint8_t device_id;

    if (ps2_read_data(&self_test_result) != 0 ||
        ps2_read_data(&device_id) != 0) {
        kernel_warning("PS/2 mouse: reset yaniti okunamadi!");
        return;
    }

    if (self_test_result != MOUSE_SELFTEST_OK) {
        kernel_warning("PS/2 mouse: self-test basarisiz!");
        return;
    }

    kernel_debug("PS/2 mouse: self-test OK, device ID = 0x00");

    mouse_send_cmd(MOUSE_CMD_SET_DEFAULTS);

    if (mouse_detect_intellimouse()) {
        mouse_packet_size = PACKET_4BYTE;
        mouse_state.scroll_wheel = true;
        kernel_debug("PS/2 mouse: IntelliMouse scroll wheel detected");
    } else {
        mouse_packet_size = PACKET_3BYTE;
        kernel_debug("PS/2 mouse: standard 3-button mode");
    }

    mouse_send_cmd(MOUSE_CMD_SET_SAMPLE);
    mouse_send_cmd(100);

    mouse_send_cmd(MOUSE_CMD_SET_RES);
    mouse_send_cmd(2);

    if (mouse_send_cmd(MOUSE_CMD_ENABLE) != 0) {
        kernel_warning("PS/2 mouse: enable basarisiz!");
        return;
    }

    ps2_flush_buffer();

    pic_set_mask(2, false);
    pic_set_mask(IRQ_MOUSE, false);
    isr_install_handler(IRQ_MOUSE, mouse_irq_handler);

    mouse_state.present = true;
    kernel_debug("PS/2 mouse basariyla baslatildi (IRQ12)");
}


mouse_state_t* mouse_get_state(void) {
    return &mouse_state;
}

void mouse_set_screen_size(int width, int height) {
    mouse_screen_width = width;
    mouse_screen_height = height;
}
