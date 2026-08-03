/* CUSTOM_PARTITION_TRAILER implementation for MAX32666 + NAND.
 *
 * The default wolfBoot trailer (FLAGS at partition end, NVM_FLASH_WRITEONCE)
 * would need 2 sectors of flags plus a sector-sized RAM cache. However in this
 * configuration WOLFBOOT_SECTOR_SIZE is very large, so instead, we keep all
 * partition flags in a dedicated internal flash region of two 8 KB
 * pages used as a ping-pong pair: each update writes the whole image to the
 * other page, then erases the stale page. */

#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "image.h"
#include "wolfboot/wolfboot.h"

#ifndef WOLFBOOT_TRAILER_ADDRESS
#define WOLFBOOT_TRAILER_ADDRESS 0x10048000UL
#endif
#define TRAILER_PAGE_SIZE   0x2000UL
#define TRAILER_PAGE(n)     (WOLFBOOT_TRAILER_ADDRESS + (n) * TRAILER_PAGE_SIZE)
#define TRAILER_VALID_MAGIC 0x4C525457UL /* WTRL */

#define TRAILER_SECTOR_FLAGS_MAX 64u

struct trailer_data {
    uint32_t boot_magic;
    uint8_t  boot_state;
    uint8_t  pad0[3];
    uint32_t update_magic;
    uint8_t  update_state;
    uint8_t  sector_flags[TRAILER_SECTOR_FLAGS_MAX];
    uint8_t  pad1[3];
};

struct trailer_footer {
    uint32_t generation;
    uint32_t valid_magic;
};

#define FOOTER_OFFSET (TRAILER_PAGE_SIZE - sizeof(struct trailer_footer))

static uint8_t trailer_cache[TRAILER_PAGE_SIZE] XALIGNED(16);
static const uint32_t trailer_erased = FLASH_WORD_ERASED;

static int RAMFUNCTION trailer_page_valid(int n)
{
    const struct trailer_footer *f =
        (const struct trailer_footer *)(TRAILER_PAGE(n) + FOOTER_OFFSET);
    return (f->valid_magic == TRAILER_VALID_MAGIC);
}

/* Returns the currently valid page (0/1), or -1 if none */
static int RAMFUNCTION trailer_select(void)
{
    const struct trailer_footer *f0 =
        (const struct trailer_footer *)(TRAILER_PAGE(0) + FOOTER_OFFSET);
    const struct trailer_footer *f1 =
        (const struct trailer_footer *)(TRAILER_PAGE(1) + FOOTER_OFFSET);
    int v0 = trailer_page_valid(0);
    int v1 = trailer_page_valid(1);

    if (v0 && v1)
        return (f0->generation + 1u == f1->generation) ? 1 : 0;
    if (v0)
        return 0;
    if (v1)
        return 1;
    return -1;
}

static uint8_t* RAMFUNCTION trailer_field(uint8_t *base, uint8_t part,
        uint32_t at)
{
    struct trailer_data *d = (struct trailer_data *)base;

    if (part == PART_BOOT) {
        if (at == 0)
            return (uint8_t *)&d->boot_magic;
        if (at == 1)
            return &d->boot_state;
    } else if (part == PART_UPDATE) {
        if (at == 0)
            return (uint8_t *)&d->update_magic;
        if (at == 1)
            return &d->update_state;
        if ((at - 2) < TRAILER_SECTOR_FLAGS_MAX)
            return &d->sector_flags[at - 2];
    }
    return NULL;
}

uint8_t* RAMFUNCTION get_trailer_at(uint8_t part, uint32_t at)
{
    int sel = trailer_select();
    uint8_t *field;

    if (sel < 0)
        return (uint8_t *)&trailer_erased;
    field = trailer_field((uint8_t *)TRAILER_PAGE(sel), part, at);
    if (field == NULL)
        return (uint8_t *)&trailer_erased;
    return field;
}

static void RAMFUNCTION trailer_commit(int sel)
{
    struct trailer_footer *f =
        (struct trailer_footer *)(trailer_cache + FOOTER_OFFSET);
    int dst = (sel < 0) ? 0 : !sel;

    f->generation = (sel < 0) ? 0 :
        ((const struct trailer_footer *)(TRAILER_PAGE(sel) +
            FOOTER_OFFSET))->generation + 1u;
    f->valid_magic = TRAILER_VALID_MAGIC;

    hal_flash_erase(TRAILER_PAGE(dst), TRAILER_PAGE_SIZE);
    hal_flash_write(TRAILER_PAGE(dst), trailer_cache, TRAILER_PAGE_SIZE);
    if (sel >= 0)
        hal_flash_erase(TRAILER_PAGE(sel), TRAILER_PAGE_SIZE);
}

static void RAMFUNCTION trailer_load(int sel)
{
    if (sel < 0)
        memset(trailer_cache, FLASH_BYTE_ERASED, TRAILER_PAGE_SIZE);
    else
        memcpy(trailer_cache, (const void *)TRAILER_PAGE(sel),
               TRAILER_PAGE_SIZE);
}

void RAMFUNCTION set_trailer_at(uint8_t part, uint32_t at, uint8_t val)
{
    int sel = trailer_select();
    uint8_t *field;

    trailer_load(sel);
    field = trailer_field(trailer_cache, part, at);
    if (field == NULL)
        return;
    if (sel >= 0 && *field == val)
        return;
    *field = val;
    trailer_commit(sel);
}

void RAMFUNCTION set_partition_magic(uint8_t part)
{
    static const uint32_t magic_trail = WOLFBOOT_MAGIC_TRAIL;
    int sel = trailer_select();
    uint8_t *field;

    trailer_load(sel);
    field = trailer_field(trailer_cache, part, 0);
    if (field == NULL)
        return;
    if (sel >= 0 && memcmp(field, &magic_trail, sizeof(uint32_t)) == 0)
        return;
    memcpy(field, &magic_trail, sizeof(uint32_t));
    trailer_commit(sel);
}
