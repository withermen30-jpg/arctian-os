#include "kernel.h"
#include "pci.h"
#include "mmio.h"

static void pci_map_bars(pci_bar_t *bars) {
    for (int i = 0; i < 6; i++) {
        if (bars[i].is_io || bars[i].address == 0) continue;
        mmio_map(bars[i].address, bars[i].size ? bars[i].size : 0x1000);
    }
}

int usb_controller_count = 0;
usb_controller_t usb_controllers[8];

int net_controller_count = 0;
net_controller_t net_controllers[8];

uint32_t pci_config_readl(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_writel(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_config_readw(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return (uint16_t)((inl(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_config_readb(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return (uint8_t)((inl(PCI_CONFIG_DATA) >> ((offset & 3) * 8)) & 0xFF);
}

void pci_config_writew(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t reg = inl(PCI_CONFIG_DATA);
    uint32_t shift = (offset & 2) * 8;
    reg &= ~(0xFFFF << shift);
    reg |= ((uint32_t)value << shift);
    outl(PCI_CONFIG_DATA, reg);
}

void pci_config_writeb(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t reg = inl(PCI_CONFIG_DATA);
    uint32_t shift = (offset & 3) * 8;
    reg &= ~(0xFF << shift);
    reg |= ((uint32_t)value << shift);
    outl(PCI_CONFIG_DATA, reg);
}

pci_bar_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_index) {
    pci_bar_t bar;
    bar.address = 0;
    bar.size = 0;
    bar.is_io = false;
    bar.is_64bit = false;
    bar.prefetchable = false;

    uint8_t offset = PCI_CONFIG_BAR0 + bar_index * 4;
    if (bar_index >= 6) return bar;

    uint32_t bar_raw = pci_config_readl(bus, slot, func, offset);
    bar.is_io = (bar_raw & 1) != 0;

    if (bar.is_io) {
        bar.address = (uint64_t)(bar_raw & 0xFFFFFFFC);
        pci_config_writel(bus, slot, func, offset, 0xFFFFFFFF);
        uint32_t size_raw = pci_config_readl(bus, slot, func, offset);
        pci_config_writel(bus, slot, func, offset, bar_raw);
        bar.size = (uint64_t)(~(size_raw & 0xFFFFFFFC) + 1);
    } else {
        bar.prefetchable = (bar_raw & 0x08) != 0;
        bar.is_64bit = ((bar_raw >> 1) & 0x03) == 0x02;
        uint32_t low = bar_raw & 0xFFFFFFF0;

        if (bar.is_64bit && bar_index < 5) {
            uint32_t high = pci_config_readl(bus, slot, func, offset + 4);
            bar.address = ((uint64_t)high << 32) | (uint64_t)low;
            pci_config_writel(bus, slot, func, offset, 0xFFFFFFFF);
            uint32_t size_low = pci_config_readl(bus, slot, func, offset);
            uint64_t size = (uint64_t)(~(size_low & 0xFFFFFFF0) + 1);
            uint32_t size_high = pci_config_readl(bus, slot, func, offset + 4);
            if (size_high != 0) {
                uint32_t saved_bar1 = pci_config_readl(bus, slot, func, offset + 4);
                pci_config_writel(bus, slot, func, offset + 4, 0xFFFFFFFF);
                size_high = pci_config_readl(bus, slot, func, offset + 4);
                pci_config_writel(bus, slot, func, offset + 4, saved_bar1);
                size |= (uint64_t)size_high << 32;
                size = ~size + 1;
            }
            bar.size = size;
            pci_config_writel(bus, slot, func, offset, bar_raw);
        } else {
            bar.address = (uint64_t)low;
            pci_config_writel(bus, slot, func, offset, 0xFFFFFFFF);
            uint32_t size_raw = pci_config_readl(bus, slot, func, offset);
            pci_config_writel(bus, slot, func, offset, bar_raw);
            bar.size = (uint64_t)(~(size_raw & 0xFFFFFFF0) + 1);
        }
    }
    return bar;
}

void pci_enable_bus_mastering(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t cmd = pci_config_readw(bus, slot, func, PCI_CONFIG_COMMAND);
    cmd |= PCI_CMD_BUS_MASTER | PCI_CMD_MEM_SPACE | PCI_CMD_IO_SPACE;
    pci_config_writew(bus, slot, func, PCI_CONFIG_COMMAND, cmd);
}

bool pci_device_iterate(void) {
    static int state = 0;
    static int bus = 0, slot = 0, func = 0;

    if (state == 0) {
        bus = 0; slot = 0; func = 0;
        state = 1;
    }

    for (; bus < 256; bus++) {
        for (; slot < 32; slot++) {
            uint16_t vendor = pci_config_readw(bus, slot, 0, 0);
            if (vendor == 0xFFFF) { func = 0; continue; }

            uint8_t header_type = pci_config_readb(bus, slot, 0, 0x0E);
            uint8_t max_func = (header_type & 0x80) ? 8 : 1;

            for (; func < max_func; func++) {
                uint16_t v = pci_config_readw(bus, slot, func, 0);
                if (v == 0xFFFF) continue;

                uint32_t id = pci_config_readl(bus, slot, func, 0x08);
                uint8_t cc = (id >> 24) & 0xFF;
                uint8_t sc = (id >> 16) & 0xFF;
                uint8_t pi = (id >> 8) & 0xFF;

                uint8_t irq = pci_config_readb(bus, slot, func, PCI_CONFIG_INTERRUPT);

                if (cc == PCI_CLASS_SERIAL && sc == PCI_SUBCLASS_USB && usb_controller_count < 8) {
                    usb_controller_t *uc = &usb_controllers[usb_controller_count];
                    uc->dev.bus = (uint8_t)bus;
                    uc->dev.slot = (uint8_t)slot;
                    uc->dev.func = (uint8_t)func;
                    uc->dev.vendor = v;
                    uc->dev.device = pci_config_readw(bus, slot, func, 0x02);
                    uc->dev.class_code = cc;
                    uc->dev.subclass = sc;
                    uc->dev.prog_if = pi;
                    uc->dev.rev_id = pci_config_readb(bus, slot, func, 0x08);
                    uc->dev.irq = irq;
                    uc->initialized = false;

                    switch (pi & 0x3F) {
                        case PCI_PROGIF_UHCI: uc->usb_type = 0; break;
                        case PCI_PROGIF_OHCI: uc->usb_type = 1; break;
                        case PCI_PROGIF_EHCI: uc->usb_type = 2; break;
                        case PCI_PROGIF_xHCI: uc->usb_type = 3; break;
                        default: uc->usb_type = 0xFF; break;
                    }

                    for (int b = 0; b < 6; b++)
                    uc->bar[b] = pci_read_bar((uint8_t)bus, (uint8_t)slot, (uint8_t)func, b);

                    usb_controller_count++;
                }

                func++;
                if (func >= max_func) { func = 0; break; }
                return true;
            }
            func = 0;
        }
        slot = 0;
    }
    state = 0;
    return false;
}

void pci_scan_usb_controllers(void) {
    usb_controller_count = 0;

    for (int b = 0; b < 256; b++) {
        for (int s = 0; s < 32; s++) {
            uint16_t vendor = pci_config_readw(b, s, 0, 0);
            if (vendor == 0xFFFF) continue;

            uint8_t header_type = pci_config_readb(b, s, 0, 0x0E);
            uint8_t max_func = (header_type & 0x80) ? 8 : 1;

            for (int f = 0; f < max_func; f++) {
                vendor = pci_config_readw(b, s, f, 0);
                if (vendor == 0xFFFF) continue;

                uint32_t rev_class = pci_config_readl(b, s, f, 0x08);
                uint8_t cc = (rev_class >> 24) & 0xFF;
                uint8_t sc = (rev_class >> 16) & 0xFF;
                uint8_t pi = (rev_class >> 8) & 0xFF;

                if (cc != PCI_CLASS_SERIAL || sc != PCI_SUBCLASS_USB)
                    continue;
                if (usb_controller_count >= 8) break;

                usb_controller_t *uc = &usb_controllers[usb_controller_count];
                uc->dev.bus = (uint8_t)b;
                uc->dev.slot = (uint8_t)s;
                uc->dev.func = (uint8_t)f;
                uc->dev.vendor = vendor;
                uc->dev.device = pci_config_readw(b, s, f, 0x02);
                uc->dev.class_code = cc;
                uc->dev.subclass = sc;
                uc->dev.prog_if = pi;
                uc->dev.rev_id = pci_config_readb(b, s, f, 0x08);
                uc->dev.irq = pci_config_readb(b, s, f, PCI_CONFIG_INTERRUPT);
                uc->initialized = false;

                switch (pi & 0x3F) {
                    case PCI_PROGIF_UHCI: uc->usb_type = 0; break;
                    case PCI_PROGIF_OHCI: uc->usb_type = 1; break;
                    case PCI_PROGIF_EHCI: uc->usb_type = 2; break;
                    case PCI_PROGIF_xHCI: uc->usb_type = 3; break;
                    default: uc->usb_type = 0xFF; break;
                }

                for (int ba = 0; ba < 6; ba++)
                    uc->bar[ba] = pci_read_bar((uint8_t)b, (uint8_t)s, (uint8_t)f, ba);
                pci_map_bars(uc->bar);

                usb_controller_count++;
            }
        }
    }
}

pci_device_t* pci_get_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
    for (int i = 0; i < usb_controller_count; i++) {
        if (usb_controllers[i].dev.class_code == class_code &&
            usb_controllers[i].dev.subclass == subclass &&
            usb_controllers[i].dev.prog_if == prog_if)
            return &usb_controllers[i].dev;
    }
    return 0;
}

void pci_scan_network_controllers(void) {
    net_controller_count = 0;

    for (int b = 0; b < 256; b++) {
        for (int s = 0; s < 32; s++) {
            uint16_t vendor = pci_config_readw(b, s, 0, 0);
            if (vendor == 0xFFFF) continue;

            uint8_t header_type = pci_config_readb(b, s, 0, 0x0E);
            uint8_t max_func = (header_type & 0x80) ? 8 : 1;

            for (int f = 0; f < max_func; f++) {
                vendor = pci_config_readw(b, s, f, 0);
                if (vendor == 0xFFFF) continue;

                uint32_t rev_class = pci_config_readl(b, s, f, 0x08);
                uint8_t cc = (rev_class >> 24) & 0xFF;
                uint8_t sc = (rev_class >> 16) & 0xFF;
                uint8_t pi = (rev_class >> 8) & 0xFF;
                uint16_t device = pci_config_readw(b, s, f, 0x02);

                if (cc != PCI_CLASS_NETWORK) continue;
                if (net_controller_count >= 8) break;

                net_controller_t *nc = &net_controllers[net_controller_count];
                nc->dev.bus = (uint8_t)b;
                nc->dev.slot = (uint8_t)s;
                nc->dev.func = (uint8_t)f;
                nc->dev.vendor = vendor;
                nc->dev.device = device;
                nc->dev.class_code = cc;
                nc->dev.subclass = sc;
                nc->dev.prog_if = pi;
                nc->dev.rev_id = pci_config_readb(b, s, f, 0x08);
                nc->dev.irq = pci_config_readb(b, s, f, PCI_CONFIG_INTERRUPT);
                nc->initialized = false;

                if (sc == PCI_SUBCLASS_ETHERNET) {
                    if (vendor == PCI_VENDOR_INTEL &&
                        (device == PCI_DEV_E1000_82540EM ||
                         device == PCI_DEV_E1000_82545EM ||
                         device == PCI_DEV_E1000_82574L ||
                         device == PCI_DEV_E1000_I217LM ||
                         device == PCI_DEV_E1000_I219LM)) {
                        nc->net_type = NET_TYPE_E1000;
                    } else if (vendor == PCI_VENDOR_REALTEK &&
                               device == PCI_DEV_RTL8139) {
                        nc->net_type = NET_TYPE_RTL8139;
                    } else if (vendor == PCI_VENDOR_AMD &&
                               (device == PCI_DEV_PCNET_II ||
                                device == PCI_DEV_PCNET_FAST_III)) {
                        nc->net_type = NET_TYPE_PCNET;
                    } else {
                        nc->net_type = NET_TYPE_UNKNOWN;
                    }
                } else {
                    bool known_wifi =
                        (vendor == PCI_VENDOR_INTEL &&
                         (device == PCI_DEV_IWL_7260 ||
                          device == PCI_DEV_IWL_8260)) ||
                        (vendor == PCI_VENDOR_ATHEROS &&
                         (device == PCI_DEV_ATH9K ||
                          device == PCI_DEV_ATH10K)) ||
                        (vendor == PCI_VENDOR_BROADCOM &&
                         device == PCI_DEV_BCM4360);

                    if (known_wifi || sc == PCI_SUBCLASS_NET_OTHER) {
                        nc->net_type = NET_TYPE_WIFI;
                    } else {
                        nc->net_type = NET_TYPE_UNKNOWN;
                    }
                }

                for (int ba = 0; ba < 6; ba++)
                    nc->bar[ba] = pci_read_bar((uint8_t)b, (uint8_t)s, (uint8_t)f, ba);
                pci_map_bars(nc->bar);

                net_controller_count++;
            }
        }
    }
}

void pci_init(void) {
    kernel_debug("PCI: USB denetleyicileri taranıyor...");
    pci_scan_usb_controllers();

    for (int i = 0; i < usb_controller_count; i++) {
        usb_controller_t *uc = &usb_controllers[i];
        const char *type_str;
        switch (uc->usb_type) {
            case 0: type_str = "UHCI"; break;
            case 1: type_str = "OHCI"; break;
            case 2: type_str = "EHCI"; break;
            case 3: type_str = "xHCI"; break;
            default: type_str = "Bilinmeyen"; break;
        }

        kernel_debug("PCI: USB #%d: %s %04X:%04X bus=%d slot=%d func=%d irq=%d",
                     i, type_str, uc->dev.vendor, uc->dev.device,
                     uc->dev.bus, uc->dev.slot, uc->dev.func, uc->dev.irq);

        pci_enable_bus_mastering(uc->dev.bus, uc->dev.slot, uc->dev.func);
    }

    if (usb_controller_count == 0)
        kernel_debug("PCI: USB denetleyici bulunamadi");

    kernel_debug("PCI: Ag kartlari taranıyor...");
    pci_scan_network_controllers();

    for (int i = 0; i < net_controller_count; i++) {
        net_controller_t *nc = &net_controllers[i];
        const char *type_str;
        switch (nc->net_type) {
            case NET_TYPE_E1000: type_str = "E1000"; break;
            case NET_TYPE_RTL8139: type_str = "RTL8139"; break;
            case NET_TYPE_PCNET: type_str = "PCnet"; break;
            case NET_TYPE_WIFI: type_str = "WiFi"; break;
            default: type_str = "Bilinmeyen"; break;
        }

        kernel_debug("PCI: NET #%d: %s %04X:%04X bus=%d slot=%d func=%d irq=%d",
                     i, type_str, nc->dev.vendor, nc->dev.device,
                     nc->dev.bus, nc->dev.slot, nc->dev.func, nc->dev.irq);

        pci_enable_bus_mastering(nc->dev.bus, nc->dev.slot, nc->dev.func);
    }

    if (net_controller_count == 0)
        kernel_debug("PCI: Ag karti bulunamadi");
}
