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

/* ---- Intel Wi-Fi (iwlwifi ailesi) ---- */
#define PCI_DEV_IWL_3160_A       0x08B3
#define PCI_DEV_IWL_3160_B       0x08B4
#define PCI_DEV_IWL_7260         0x08B1
#define PCI_DEV_IWL_7260_B       0x08B2
#define PCI_DEV_IWL_7265_A       0x095A
#define PCI_DEV_IWL_7265_B       0x095B
#define PCI_DEV_IWL_3165_A       0x3165
#define PCI_DEV_IWL_3165_B       0x3166
#define PCI_DEV_IWL_8260         0x24F3
#define PCI_DEV_IWL_8265         0x24FD
#define PCI_DEV_IWL_9260         0x2526
#define PCI_DEV_IWL_9560_A       0x9DF0
#define PCI_DEV_IWL_9560_B       0xA370
#define PCI_DEV_IWL_AX200        0x2723
#define PCI_DEV_IWL_AX201_A      0x06F0
#define PCI_DEV_IWL_AX201_B      0xA0F0
#define PCI_DEV_IWL_AX201_C      0x02F0
#define PCI_DEV_IWL_AX210        0x2725
#define PCI_DEV_IWL_AX211        0x51F0
#define PCI_DEV_IWL_AX411        0x7AF0

/* ---- Realtek Wi-Fi ---- */
#define PCI_DEV_RTL8188EE        0x8179
#define PCI_DEV_RTL8192          0x8192
#define PCI_DEV_RTL8723BE        0xB723
#define PCI_DEV_RTL8723DE        0xD723
#define PCI_DEV_RTL8821CE        0xC821
#define PCI_DEV_RTL8822CE        0xC822

/* ---- Atheros / Qualcomm Wi-Fi ---- */
#define PCI_DEV_ATH_AR9485       0x0032
#define PCI_DEV_ATH_AR9462       0x0034
#define PCI_DEV_ATH_AR9565       0x0036
#define PCI_DEV_ATH_AR9580       0x003A
#define PCI_DEV_ATH_QCA9880      0x003C
#define PCI_DEV_ATH_QCA6174      0x003E
#define PCI_DEV_ATH_QCA9377      0x0042
#define PCI_DEV_ATH_QCA9984      0x0046

/* ---- Broadcom Wi-Fi ---- */
#define PCI_DEV_BCM4360          0x43A0
#define PCI_DEV_BCM4352          0x43B1
#define PCI_DEV_BCM4356          0x43D3
#define PCI_DEV_BCM43602_A       0x4365
#define PCI_DEV_BCM43602_B       0x43BA

/* ---- Ek Ethernet kartlari ---- */
#define PCI_DEV_RTL8168          0x8168
#define PCI_DEV_RTL8169          0x8169
#define PCI_DEV_RTL8125          0x8125
#define PCI_DEV_I225             0x15F3

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
#define NET_TYPE_RTL8169  4
#define NET_TYPE_UNKNOWN   -1

void pci_init(void);
void pci_scan_network_controllers(void);

#endif
