#include "kernel.h"
#include "usb.h"
#include "blockdev.h"
#include "serial.h"

#define MSD_MAX_DEVS      4
#define MSD_BLOCK_SIZE    512
#define MSD_BOUNCE_SIZE   4096

#define MSD_BOT_CBW_SIG   0x43425355u
#define MSD_BOT_CSW_SIG   0x53425355u

#define SCSI_INQUIRY          0x12
#define SCSI_TEST_UNIT_READY  0x00
#define SCSI_REQUEST_SENSE    0x03
#define SCSI_READ_CAPACITY10  0x25
#define SCSI_READ10           0x28
#define SCSI_WRITE10          0x2A

typedef struct {
    usb_device_t *dev;
    int           ep_in;
    int           ep_out;
    uint32_t      tag;
    uint32_t      block_size;
    uint64_t      sectors;
    char          vendor[12];
    char          product[20];
} msd_dev_t;

static msd_dev_t s_msd[MSD_MAX_DEVS];
static int       s_msd_count = 0;
static uint8_t   s_bounce[MSD_BOUNCE_SIZE] __attribute__((aligned(16)));

static void msd_name_copy(char *dst, int dstsz, const uint8_t *src, int srclen) {
    int p = 0;
    for (int i = 0; i < srclen && p < dstsz - 1; i++) {
        char c = (char)src[i];
        if (c < 0x20 || c > 0x7E) c = ' ';
        dst[p++] = c;
    }
    while (p > 0 && dst[p - 1] == ' ') p--;
    dst[p] = 0;
}

static int msd_bot(msd_dev_t *m, const uint8_t *cdb, int cdb_len,
                   void *data, uint32_t data_len, int dir_in) {
    if (data_len > MSD_BOUNCE_SIZE) return -1;

    uint8_t cbw[31];
    memset(cbw, 0, sizeof(cbw));
    uint32_t tag = ++m->tag;
    cbw[0] = (uint8_t)(MSD_BOT_CBW_SIG);
    cbw[1] = (uint8_t)(MSD_BOT_CBW_SIG >> 8);
    cbw[2] = (uint8_t)(MSD_BOT_CBW_SIG >> 16);
    cbw[3] = (uint8_t)(MSD_BOT_CBW_SIG >> 24);
    cbw[4] = (uint8_t)(tag);
    cbw[5] = (uint8_t)(tag >> 8);
    cbw[6] = (uint8_t)(tag >> 16);
    cbw[7] = (uint8_t)(tag >> 24);
    cbw[8]  = (uint8_t)(data_len);
    cbw[9]  = (uint8_t)(data_len >> 8);
    cbw[10] = (uint8_t)(data_len >> 16);
    cbw[11] = (uint8_t)(data_len >> 24);
    cbw[12] = dir_in ? 0x80 : 0x00;
    cbw[13] = 0;
    cbw[14] = (uint8_t)cdb_len;
    for (int i = 0; i < cdb_len && i < 16; i++) cbw[15 + i] = cdb[i];

    if (usb_bulk_transfer(m->dev, m->ep_out, cbw, 31, USB_DIR_OUT) != 0)
        return -1;

    if (data_len > 0) {
        if (!dir_in && data)
            memcpy(s_bounce, data, data_len);
        int ep = dir_in ? m->ep_in : m->ep_out;
        if (usb_bulk_transfer(m->dev, ep, s_bounce, (int)data_len,
                              dir_in ? USB_DIR_IN : USB_DIR_OUT) != 0)
            return -2;
        if (dir_in && data)
            memcpy(data, s_bounce, data_len);
    }

    uint8_t csw[13];
    if (usb_bulk_transfer(m->dev, m->ep_in, csw, 13, USB_DIR_IN) != 0)
        return -3;

    uint32_t sig = (uint32_t)csw[0] | ((uint32_t)csw[1] << 8) |
                   ((uint32_t)csw[2] << 16) | ((uint32_t)csw[3] << 24);
    if (sig != MSD_BOT_CSW_SIG) return -4;
    uint32_t rtag = (uint32_t)csw[4] | ((uint32_t)csw[5] << 8) |
                    ((uint32_t)csw[6] << 16) | ((uint32_t)csw[7] << 24);
    if (rtag != tag) return -5;
    if (csw[12] != 0) return -6;
    return 0;
}

static void cdb_lba(uint8_t *cdb, uint64_t lba) {
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)(lba);
}

static int msd_read(void *ctx, uint64_t lba, uint32_t count, void *buf) {
    msd_dev_t *m = (msd_dev_t *)ctx;
    if (!m || !buf) return -1;
    uint8_t *p = (uint8_t *)buf;

    while (count > 0) {
        uint32_t n = count > 8 ? 8 : count;
        uint8_t cdb[10];
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSI_READ10;
        cdb_lba(cdb, lba);
        cdb[7] = (uint8_t)(n >> 8);
        cdb[8] = (uint8_t)(n);
        if (msd_bot(m, cdb, 10, p, n * MSD_BLOCK_SIZE, 1) != 0)
            return -1;
        p += n * MSD_BLOCK_SIZE;
        lba += n;
        count -= n;
    }
    return 0;
}

static int msd_write(void *ctx, uint64_t lba, uint32_t count, const void *buf) {
    msd_dev_t *m = (msd_dev_t *)ctx;
    if (!m || !buf) return -1;
    const uint8_t *p = (const uint8_t *)buf;

    while (count > 0) {
        uint32_t n = count > 8 ? 8 : count;
        uint8_t cdb[10];
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSI_WRITE10;
        cdb_lba(cdb, lba);
        cdb[7] = (uint8_t)(n >> 8);
        cdb[8] = (uint8_t)(n);
        if (msd_bot(m, cdb, 10, (void *)p, n * MSD_BLOCK_SIZE, 0) != 0)
            return -1;
        p += n * MSD_BLOCK_SIZE;
        lba += n;
        count -= n;
    }
    return 0;
}

static int msd_flush(void *ctx) {
    (void)ctx;
    return 0;
}

static int msd_register(msd_dev_t *m) {
    bdev_ops_t ops;
    memset(&ops, 0, sizeof(ops));
    int p = 0;
    const char *pfx = "USB ";
    for (int i = 0; pfx[i] && p < 23; i++) ops.name[p++] = pfx[i];
    for (int i = 0; m->product[i] && p < 23; i++) ops.name[p++] = m->product[i];
    ops.name[p] = 0;
    if (p == 0) {
        const char *def = "USB Depolama";
        for (int i = 0; def[i] && i < 23; i++) ops.name[i] = def[i];
    }
    ops.read = msd_read;
    ops.write = msd_write;
    ops.flush = msd_flush;
    ops.ctx = m;
    ops.sectors = m->sectors;
    ops.kind = BDEV_KIND_USB;
    ops.removable = 1;
    return blockdev_register(&ops);
}

void usb_msd_scan(void) {
    s_msd_count = 0;

    if (usb_device_count <= 0) return;

    for (int i = 0; i < usb_device_count && s_msd_count < MSD_MAX_DEVS; i++) {
        usb_device_t *d = &usb_devices[i];
        if (d->class_code != 0x08 || d->subclass != 0x06 || d->protocol != 0x50)
            continue;

        int ep_in = -1, ep_out = -1;
        for (int e = 0; e < d->endpoint_count; e++) {
            uint8_t ea = d->endpoints[e].bEndpointAddress;
            uint8_t attr = d->endpoints[e].bmAttributes & 0x03;
            if (attr != 0x02) continue;
            if (ea & 0x80) ep_in = ea;
            else ep_out = ea;
        }
        if (ep_in < 0 || ep_out < 0) {
            serial_printf("[msd] bulk uclar yok (in=%d out=%d)\n", ep_in, ep_out);
            continue;
        }

        msd_dev_t *m = &s_msd[s_msd_count];
        memset(m, 0, sizeof(*m));
        m->dev = d;
        m->ep_in = ep_in;
        m->ep_out = ep_out;
        m->block_size = MSD_BLOCK_SIZE;

        uint8_t cdb[10];
        uint8_t inq[36];
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSI_INQUIRY;
        cdb[4] = 36;
        if (msd_bot(m, cdb, 6, inq, 36, 1) != 0) {
            serial_printf("[msd] INQUIRY hata\n");
            continue;
        }
        msd_name_copy(m->vendor, sizeof(m->vendor), inq + 8, 8);
        msd_name_copy(m->product, sizeof(m->product), inq + 16, 16);

        uint8_t cap[8];
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSI_READ_CAPACITY10;
        if (msd_bot(m, cdb, 10, cap, 8, 1) != 0) {
            serial_printf("[msd] READ_CAPACITY hata\n");
            continue;
        }
        uint32_t last_lba = ((uint32_t)cap[0] << 24) | ((uint32_t)cap[1] << 16) |
                            ((uint32_t)cap[2] << 8) | (uint32_t)cap[3];
        uint32_t bs = ((uint32_t)cap[4] << 24) | ((uint32_t)cap[5] << 16) |
                      ((uint32_t)cap[6] << 8) | (uint32_t)cap[7];
        if (bs == 0) bs = MSD_BLOCK_SIZE;
        m->block_size = bs;
        m->sectors = ((uint64_t)last_lba + 1) * (bs / MSD_BLOCK_SIZE);

        if (msd_register(m) != 0) {
            serial_printf("[msd] blockdev kayit hata\n");
            continue;
        }

        serial_printf("[msd] kayit: '%s' '%s' sec=%d bs=%d\n",
                      m->vendor, m->product, (int)m->sectors, (int)m->block_size);
        s_msd_count++;
    }
}
