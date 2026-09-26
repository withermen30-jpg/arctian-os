#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "kernel.h"
#include "idt.h"
#include "pic.h"
#include "isr.h"
#include "amod.h"
#include "asi.h"
#include "media.h"
#include "storage.h"
#include "loader.h"
#include "printf.h"
#include "alloc.h"
#include "memory_map.h"
#include "serial.h"
#include "afs.h"
#include "blockdev.h"

asi_t *g_asi = 0;

static asi_console_t s_console;
static asi_mem_t     s_mem;
static asi_storage_t s_storage;
static asi_time_t    s_time;
static asi_t         s_asi;
static uint64_t      s_ticks;


static void console_clear(void)          { terminal_initialize(); }
static void console_putc(char c)         { terminal_putchar(c); }
static void console_puts(const char *s)  { terminal_writestring(s); }
static void *mem_alloc(size_t n)         { return kmalloc(n); }
static void  mem_free(void *p)           { kfree(p); }
static void  mem_set(void *d, int c, size_t n)          { memset(d, c, n); }
static void  mem_copy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
static int   st_read(uint32_t lba, uint32_t count, void *buf)        { return media_read(lba, count, buf); }
static int   st_write(uint32_t lba, uint32_t count, const void *buf) { return storage_write(lba, count, buf); }
static int   st_present(void)            { return 1; }
static uint64_t time_ticks(void)         { return s_ticks; }
static void time_sleep(uint32_t ms) {
    for (volatile uint64_t i = 0; i < (uint64_t)ms * 200000ULL; i++) asm volatile("pause");
    s_ticks += ms;
}

static void asi_setup(void) {
    s_console.clear = console_clear;
    s_console.putc = console_putc;
    s_console.puts = console_puts;
    s_console.printf = kprintf;

    s_mem.alloc = mem_alloc;
    s_mem.free = mem_free;
    s_mem.memset = mem_set;
    s_mem.memcpy = mem_copy;

    s_storage.read_sectors = st_read;
    s_storage.write_sectors = st_write;
    s_storage.present = st_present;

    s_time.ticks_ms = time_ticks;
    s_time.sleep_ms = time_sleep;

    s_asi.version = ASI_VERSION;
    s_asi.console = &s_console;
    s_asi.mem = &s_mem;
    s_asi.storage = &s_storage;
    s_asi.time = &s_time;
    s_asi.gfx = 0;
    s_asi.input = 0;
    s_asi.system = 0;
    s_asi.fs = afs_asi();
    s_asi.net = 0;
    g_asi = &s_asi;
}

typedef void (*mod_entry_t)(void *arg);

void kernel_main(void) {
    serial_init();
    serial_puts("[mk] start\n");
    terminal_initialize();
    show_arctian_header();
    kernel_print_info();

    pic_init();
    idt_init();
    asm volatile("sti");

    terminal_writestring("main_kernel: boot medyasi baslatiliyor...\n");
    serial_puts("[mk] media init\n");
    if (media_init() != 0) kernel_panic("boot medyasi bulunamadi");
    asi_setup();
    serial_puts("[mk] media ok\n");

    bool installed = media_installed() != 0;
    terminal_writestring(installed ? "Kurulu sistem tespit edildi.\n"
                                   : "Kurulum gerekli / canli ortam.\n");

#ifdef ARCTIAN_BLK_SELFTEST
    if (!installed) {
        int bn = blockdev_init();
        serial_printf("[blk] %d cihaz\n", bn);
        for (int i = 0; i < bn; i++) {
            blockdev_info_t info;
            blockdev_info(i, &info);
            uint8_t buf[512];
            int r = blockdev_read(i, 0, 1, buf);
            serial_printf("[blk] #%d %s kind=%d secs=%d rd=%d sig=%02X%02X\n",
                          i, info.name, info.kind, (int)info.sectors, r, buf[510], buf[511]);
        }
    }
#endif

    if (!installed) {
        module_entry_t inst;
        if (module_find_by_name("install", &inst) == 0) {
            terminal_writestring("install modulu baslatiliyor...\n");
            serial_puts("[mk] install module\n");
            if (module_load(&inst) == 0) {
                ((mod_entry_t)(uintptr_t)inst.load_addr)(&s_asi);
            }
            serial_puts("[mk] install returned\n");
        }
    }

    module_entry_t kp;
    serial_puts("[mk] finding kernel_plus\n");
    if (module_find_by_name("kernel_plus", &kp) != 0) {
        serial_puts("[mk] kernel_plus NOT FOUND\n");
        kernel_panic("kernel_plus modulu yok");
    }
    serial_puts("[mk] kernel_plus found, loading\n");
    if (module_load(&kp) != 0) {
        serial_puts("[mk] kernel_plus LOAD FAIL\n");
        kernel_panic("kernel_plus yuklenemedi");
    }
    serial_puts("[mk] kernel_plus loaded, jumping\n");

    terminal_writestring("kernel_plus baslatiliyor...\n");
    ((mod_entry_t)(uintptr_t)kp.load_addr)(&s_asi);

    kernel_panic("kernel_plus geri dondu");
    for (;;) asm volatile("hlt");
}
