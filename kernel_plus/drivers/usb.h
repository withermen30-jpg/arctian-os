#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <stdbool.h>
#include "pci.h"

#define USB_MAX_DEVICES     32
#define USB_MAX_ENDPOINTS   16
#define USB_PACKET_SIZE     512

#define USB_DIR_OUT         0
#define USB_DIR_IN          1

#define USB_REQTYPE_STANDARD    0
#define USB_REQTYPE_CLASS       1
#define USB_REQTYPE_VENDOR      2

#define USB_RECIP_DEVICE        0
#define USB_RECIP_INTERFACE     1
#define USB_RECIP_ENDPOINT      2
#define USB_RECIP_OTHER         3

#define USB_REQ_GET_STATUS          0
#define USB_REQ_CLEAR_FEATURE       1
#define USB_REQ_SET_FEATURE         3
#define USB_REQ_SET_ADDRESS         5
#define USB_REQ_GET_DESCRIPTOR      6
#define USB_REQ_SET_DESCRIPTOR      7
#define USB_REQ_GET_CONFIGURATION   8
#define USB_REQ_SET_CONFIGURATION   9
#define USB_REQ_GET_INTERFACE       10
#define USB_REQ_SET_INTERFACE       11
#define USB_REQ_SYNCH_FRAME         12

#define USB_DESC_DEVICE         1
#define USB_DESC_CONFIG         2
#define USB_DESC_STRING         3
#define USB_DESC_INTERFACE      4
#define USB_DESC_ENDPOINT       5
#define USB_DESC_HID            0x21
#define USB_DESC_REPORT         0x22

#define USB_CLASS_HID           3
#define USB_SUBCLASS_BOOT       1
#define USB_PROTOCOL_KEYBOARD   1
#define USB_PROTOCOL_MOUSE      2

#define HID_REQ_SET_PROTOCOL    0x0B
#define HID_REQ_SET_IDLE        0x0A
#define HID_REQ_GET_REPORT      0x01

#define HID_PROTOCOL_BOOT       0
#define HID_PROTOCOL_REPORT     1

#define USB_DEVSTATE_DETACHED   0
#define USB_DEVSTATE_ADDRESSED  1
#define USB_DEVSTATE_CONFIGURED 2

#define USB_SPEED_LOW           1
#define USB_SPEED_FULL          2
#define USB_SPEED_HIGH          3
#define USB_SPEED_SUPER         4

#define USB_EP_TYPE_CONTROL     0
#define USB_EP_TYPE_ISOCHRONOUS 1
#define USB_EP_TYPE_BULK        2
#define USB_EP_TYPE_INTERRUPT   3

#define USB_LINK_OK             0
#define USB_LINK_NOT_USED       1
#define USB_LINK_PSKELETAL      2
#define USB_LINK_REMOVE         3
#define USB_LINK_STALL          4

#define EHCI_PORTSC_CCS         (1 << 0)
#define EHCI_PORTSC_CSC         (1 << 1)
#define EHCI_PORTSC_PE          (1 << 2)
#define EHCI_PORTSC_PEC         (1 << 3)
#define EHCI_PORTSC_OCA         (1 << 4)
#define EHCI_PORTSC_OCC         (1 << 5)
#define EHCI_PORTSC_RESET       (1 << 8)
#define EHCI_PORTSC_PP          (1 << 12)
#define EHCI_PORTSC_WOC         (1 << 13)
#define EHCI_PORTSC_WDE         (1 << 14)
#define EHCI_PORTSC_WOE         (1 << 15)
#define EHCI_PORTSC_LINE_MASK   (0x03 << 10)
#define EHCI_PORTSC_LINE_K      (0x01 << 10)
#define EHCI_PORTSC_LINE_J      (0x02 << 10)
#define EHCI_PORTSC_LINE_UNDEF  (0x00 << 10)

#define EHCI_PORTSC_LSTATUS_SHIFT 10
#define EHCI_PORTSC_LSTATUS_MASK  (3 << 10)
#define EHCI_PORTSC_PP_SHIFT      12

#define EHCI_CMD_ASYNC_ENABLE   (1 << 5)
#define EHCI_CMD_PERIODIC_ENABLE (1 << 4)
#define EHCI_CMD_HC_RESET       (1 << 1)
#define EHCI_CMD_RUN_STOP       (1 << 0)

#define EHCI_STS_HCHALTED       (1 << 12)

#define EHCI_FLAG_64BIT         (1 << 0)
#define EHCI_FLAG_ASYNC_SCHED   (1 << 1)
#define EHCI_FLAG_PERIODIC_SCHED (1 << 2)

#define XHCI_CMD_RUN_STOP       (1 << 0)
#define XHCI_CMD_HC_RESET       (1 << 1)
#define XHCI_CMD_INT_ENABLE     (1 << 2)
#define XHCI_STS_HCHALTED       (1 << 0)

#pragma pack(push, 1)
typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} usb_device_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} usb_config_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} usb_interface_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} usb_endpoint_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdHID;
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    uint8_t bReportDescriptorType;
    uint16_t wReportDescriptorLength;
} usb_hid_descriptor_t;

typedef struct {
    uint8_t address;
    uint8_t speed;
    uint8_t state;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t protocol;
    uint16_t max_packet_size;
    uint16_t vendor_id;
    uint16_t product_id;
    usb_endpoint_descriptor_t endpoints[USB_MAX_ENDPOINTS];
    int endpoint_count;
    bool is_keyboard;
    bool is_mouse;
    int xhci_slot_id;
    int controller_port;
} usb_device_t;
#pragma pack(pop)

extern usb_device_t usb_devices[USB_MAX_DEVICES];
extern int usb_device_count;

void usb_init(void);
void usb_poll_all(void);
int usb_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup, uint8_t *data, int dir);

int ehci_init(usb_controller_t *ctrl);
int ehci_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup, uint8_t *data, int dir);
int ehci_poll_keyboard(usb_device_t *dev, uint8_t *buf);
int ehci_poll_mouse(usb_device_t *dev, uint8_t *buf);

int xhci_init(usb_controller_t *ctrl);
int xhci_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup, uint8_t *data, int dir);
int xhci_poll_keyboard(usb_device_t *dev, uint8_t *buf);
int xhci_poll_mouse(usb_device_t *dev, uint8_t *buf);

int uhci_init(usb_controller_t *ctrl);
int uhci_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup, uint8_t *data, int dir);

#endif
