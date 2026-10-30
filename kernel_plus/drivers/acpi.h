#ifndef ARCTIAN_ACPI_H
#define ARCTIAN_ACPI_H

#include <stdint.h>
#include <stdbool.h>

/* ACPI tablo kesfi (RSDP -> RSDT/XSDT -> FADT/DSDT) ve guc yonetimi.
 * Gercek S5 kapatma FADT'nin PM1a/PM1b kontrol blogu ve DSDT'teki _S5
 * paketindeki SLP_TYP degerleriyle yapilir. Pil/AC durumu SMBus (SBS)
 * uzerinden en-iyi-caba okunur; yoksa AC varsayilir. */

typedef struct {
    int      present;       /* FADT bulundu mu */
    int      has_dsdt;
    uint16_t pm1a_cnt;      /* PM1a kontrol blogu G/IO portu */
    uint16_t pm1b_cnt;
    uint16_t pm1a_evt;
    uint16_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  pm1_cnt_len;
    uint8_t  sci_int;
    uint8_t  slp_typ_s5a;
    uint8_t  slp_typ_s5b;
    uint8_t  slp_typ_s3a;
    uint8_t  slp_typ_s3b;
    uint8_t  century;
    uint32_t flags;
    char     oem_id[7];
} acpi_info_t;

extern acpi_info_t g_acpi;

void acpi_init(void);
int  acpi_present(void);
void acpi_shutdown(void);        /* S5: gercekten gucu keser (ACPI varsa) */
int  acpi_enter_sleep(void);     /* S3 dener; basarisizsa 0 doner */

/* Guc / pil durumu */
typedef struct {
    int source;     /* 0 = AC, 1 = pil */
    int present;    /* pil takili mi */
    int percent;    /* 0..100, -1 = bilinmiyor */
    int charging;   /* 1 = sarj oluyor */
    int minutes;    /* kalan sure (dk), -1 = bilinmiyor */
    int smbus;      /* SMBus denetleyicisi bulundu mu */
} acpi_power_t;

void acpi_power_poll(acpi_power_t *out);

int  acpi_selftest(void);

#endif /* ARCTIAN_ACPI_H */
