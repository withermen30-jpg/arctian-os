#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

#define PCI_VENDOR_INTEL    0x8086
#define PCI_VENDOR_REALTEK  0x10EC
#define PCI_VENDOR_NEC      0x1033
#define PCI_VENDOR_VIA      0x1106
#define PCI_VENDOR_AMD      0x1022
#define PCI_VENDOR_NVIDIA   0x10DE
#define PCI_VENDOR_FRESCO   0x1B73
#define PCI_VENDOR_ASMEDIA  0x1B21
#define PCI_VENDOR_ETRON    0x1B6F
#define PCI_VENDOR_RENESAS  0x1912
#define PCI_VENDOR_TI       0x104C
#define PCI_VENDOR_ATHEROS  0x168C
#define PCI_VENDOR_BROADCOM 0x14E4

#define PCI_CLASS_SERIAL         0x0C
#define PCI_SUBCLASS_USB         0x03
#define PCI_PROGIF_UHCI         0x00
#define PCI_PROGIF_OHCI         0x10
#define PCI_PROGIF_EHCI         0x20
#define PCI_PROGIF_xHCI         0x30
#define PCI_PROGIF_EHCI_64BIT   0x40

#define PCI_CLASS_NETWORK        0x02
#define PCI_SUBCLASS_ETHERNET    0x00

#define PCI_DEV_E1000_82540EM    0x100E
#define PCI_DEV_E1000_82545EM    0x100F
#define PCI_DEV_E1000_82574L     0x10D3
#define PCI_DEV_E1000_I217LM     0x153A
#define PCI_DEV_E1000_I219LM     0x156F
#define PCI_DEV_RTL8139          0x8139
#define PCI_DEV_PCNET_II         0x2000
#define PCI_DEV_PCNET_FAST_III   0x2001

#define PCI_SUBCLASS_NET_OTHER   0x80
#define PCI_DEV_IWL_7260         0x08B1
#define PCI_DEV_IWL_8260         0x24F3
#define PCI_DEV_ATH9K            0x0036
#define PCI_DEV_ATH10K           0x003C
#define PCI_DEV_BCM4360          0x43A0

#define PCI_BAR_TYPE_IO          0x01
#define PCI_BAR_TYPE_MEMORY     0x00
#define PCI_BAR_MEM_64BIT       0x04

#define PCI_CONFIG_COMMAND       0x04
#define PCI_CONFIG_STATUS        0x06
#define PCI_CONFIG_BAR0          0x10
#define PCI_CONFIG_BAR1          0x14
#define PCI_CONFIG_BAR2          0x18
#define PCI_CONFIG_BAR3          0x1C
#define PCI_CONFIG_BAR4         0x20
#define PCI_CONFIG_BAR5         0x24
#define PCI_CONFIG_CAP_PTR       0x34
#define PCI_CONFIG_INTERRUPT     0x3C

#define PCI_CMD_IO_SPACE        0x0001
#define PCI_CMD_MEM_SPACE       0x0002
#define PCI_CMD_BUS_MASTER      0x0004
#define PCI_CMD_INT_DISABLE     0x0400

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t rev_id;
    uint8_t irq;
} pci_device_t;

typedef struct {
    uint64_t address;
    uint64_t size;
    bool is_io;
    bool is_64bit;
    bool prefetchable;
} pci_bar_t;

uint32_t pci_config_readl(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_writel(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
uint16_t pci_config_readw(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t pci_config_readb(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_writew(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
void pci_config_writeb(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value);

bool pci_device_iterate(void);
pci_device_t* pci_get_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if);
void pci_scan_usb_controllers(void);
pci_bar_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_index);
void pci_enable_bus_mastering(uint8_t bus, uint8_t slot, uint8_t func);

typedef struct {
    pci_device_t dev;
    pci_bar_t bar[6];
    uint8_t usb_type;
    bool initialized;
} usb_controller_t;

extern int usb_controller_count;
extern usb_controller_t usb_controllers[8];

typedef struct {
    pci_device_t dev;
    pci_bar_t bar[6];
    int net_type;
    bool initialized;
} net_controller_t;

extern int net_controller_count;
extern net_controller_t net_controllers[8];

#define NET_TYPE_E1000    0
#define NET_TYPE_RTL8139  1
#define NET_TYPE_WIFI      2
#define NET_TYPE_PCNET    3
#define NET_TYPE_UNKNOWN   -1

void pci_init(void);
void pci_scan_network_controllers(void);

#endif
