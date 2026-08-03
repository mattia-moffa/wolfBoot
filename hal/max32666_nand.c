/* wolfBoot external flash driver built on ZioNandBadBlocks.
 *
 * Linear ext_flash addresses map onto the whole chip:
 *   addr = ((plane * 2048 + block) * 64 + page) * 4096 + offset
 * The erase granularity is one NAND block (64 pages * 4096 = 0x40000), hence
 * WOLFBOOT_SECTOR_SIZE = 0x40000.
 *
 * Pages can only be programmed once per erase: writes are combined in a
 * one-page buffer, then flushed when a page is complete, on a page switch, or
 * on ext_flash_lock().
 */

#include <stdint.h>
#include <string.h>

#include "ZioHalSpi0.h"
#include "ZioNandBadBlocks.h"
#include "ZioOnfiMemory.h"
#include "ZioResult.h"
#include "ZioUtils.h"

#define NAND_PAGE_SIZE      ZIO_ONFI_FULL_PAGE_SIZE
#define NAND_PAGE_MASK      ((uintptr_t)(NAND_PAGE_SIZE - 1u))
#define NAND_BLOCK_SIZE     (NAND_PAGE_SIZE * ZIO_NAND_PAGES_PER_BLOCK)

static union { uint32_t align; uint8_t b[NAND_PAGE_SIZE]; } mWr;
static uint32_t mWrPageAddr;
static int      mWrDirty;

static union { uint32_t align; uint8_t b[NAND_PAGE_SIZE]; } mRd;
static uint32_t mRdPageAddr;
static int      mRdValid;

static int      mNandOk;

static sZioOnfiMemoryAddress_T nand_addr(uint32_t address)
{
    sZioOnfiMemoryAddress_T addr;
    uint32_t blockLinear = address / NAND_BLOCK_SIZE;

    addr.plane = (uint8_t)(blockLinear / ZIO_NAND_BLOCKS_PER_PLANE);
    addr.block = (uint16_t)(blockLinear % ZIO_NAND_BLOCKS_PER_PLANE);
    addr.page  = (uint8_t)((address / NAND_PAGE_SIZE) % ZIO_NAND_PAGES_PER_BLOCK);
    return addr;
}

static int nand_flush(void)
{
    eZioResult_T result;

    if (!mWrDirty)
        return 0;
    result = ZioNandBadBlocks_PageWrite(nand_addr(mWrPageAddr),
                                        ZIO_WRITE_BOTH_LOGICAL_PAGES,
                                        eZioBool_FALSE, mWr.b);
    mWrDirty = 0;
    if (mRdValid && mRdPageAddr == mWrPageAddr)
        mRdValid = 0;
    return (result == eZioResult_SUCCESS) ? 0 : -1;
}

static int nand_link_check(void)
{
    ZioUint8_T id[ZIO_NAND_CHIP_ID_SIZE];
    ZioUint8_T cmd[2];
    ZioUint8_T lock[1];

    if (ZioOnfiMemory_ReadChipId(id) != eZioResult_SUCCESS ||
            id[0] != 0x2Cu || id[1] != 0x47u)
        return -1;
    cmd[0] = 0x0Fu; /* GET_FEATURE */
    cmd[1] = 0xA0u; /* block lock register */
    lock[0] = 0xFFu;
    if (ZioHalSpi0_SingleWireTransact(cmd, 2u, 1u, lock) !=
            eZioResult_SUCCESS || lock[0] != 0x00u)
        return -1;
    return 0;
}

static int nand_init_once(void)
{
    eZioResult_T result;

    mWrDirty = 0;
    mRdValid = 0;
    mNandOk = 0;

    ZioHalSpi0_Init();
    /* The NAND keeps volatile state (block lock, command framing) across MCU
     * resets: send RESET (FFh) so init always starts from defaults. */
    {
        ZioUint8_T cmd[2];
        ZioUint8_T status[1];
        int i;

        cmd[0] = 0xFFu;
        (void)ZioHalSpi0_SingleWireTransact(cmd, 1u, 0u, status);
        ZioUtils_SwDelayMs(5u);
        cmd[0] = 0x0Fu; /* GET_FEATURE */
        cmd[1] = 0xC0u; /* status register */
        for (i = 0; i < 100; i++) {
            status[0] = 0xFFu;
            if ((ZioHalSpi0_SingleWireTransact(cmd, 2u, 1u, status) ==
                    eZioResult_SUCCESS) && ((status[0] & 0x01u) == 0u))
                break;
            ZioUtils_SwDelayMs(1u);
        }
    }
    result = ZioOnfiMemory_Init();
    if (result != eZioResult_SUCCESS)
        return -1;
    result = ZioNandBadBlocks_Init();
#ifdef MAX32666_NAND_BBT_CREATE
    if (result != eZioResult_SUCCESS) {
        /* Virgin part: build the table from the factory bad block marks */
        result = ZioNandBadBlocks_FirstTableCreate();
        if (result == eZioResult_SUCCESS)
            result = ZioNandBadBlocks_TableWrite();
        if (result == eZioResult_SUCCESS)
            result = ZioNandBadBlocks_Init();
    }
#endif
    if (result != eZioResult_SUCCESS)
        return -1;
    mNandOk = 1;
    return 0;
}

int max32666_nand_init(void)
{
    int attempt;

    for (attempt = 0; attempt < 3; attempt++) {
        if (nand_init_once() == 0 && nand_link_check() == 0)
            return 0;
        mNandOk = 0;
    }
    return -1;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    int done = 0;

    if (!mNandOk)
        return -1;
    while (done < len) {
        uint32_t pageAddr = (uint32_t)((address + done) & ~NAND_PAGE_MASK);
        uint32_t off = (uint32_t)((address + done) & NAND_PAGE_MASK);
        int chunk = (int)(NAND_PAGE_SIZE - off);

        if (chunk > (len - done))
            chunk = len - done;
        if (mWrDirty && pageAddr == mWrPageAddr) {
            memcpy(data + done, mWr.b + off, chunk);
        } else {
            if (!mRdValid || mRdPageAddr != pageAddr) {
                if (ZioNandBadBlocks_PageRead(nand_addr(pageAddr), mRd.b)
                        != eZioResult_SUCCESS)
                    return -1;
                mRdPageAddr = pageAddr;
                mRdValid = 1;
            }
            memcpy(data + done, mRd.b + off, chunk);
        }
        done += chunk;
    }
    return len;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    int done = 0;

    if (!mNandOk)
        return -1;
    while (done < len) {
        uint32_t pageAddr = (uint32_t)((address + done) & ~NAND_PAGE_MASK);
        uint32_t off = (uint32_t)((address + done) & NAND_PAGE_MASK);
        int chunk = (int)(NAND_PAGE_SIZE - off);

        if (chunk > (len - done))
            chunk = len - done;
        if (mWrDirty && mWrPageAddr != pageAddr) {
            if (nand_flush() != 0)
                return -1;
        }
        if (!mWrDirty) {
            mWrPageAddr = pageAddr;
            memset(mWr.b, 0xFF, NAND_PAGE_SIZE);
            mWrDirty = 1;
        }
        memcpy(mWr.b + off, data + done, chunk);
        done += chunk;
        if (off + (uint32_t)chunk == NAND_PAGE_SIZE) {
            if (nand_flush() != 0)
                return -1;
        }
    }
    return len;
}

int ext_flash_erase(uintptr_t address, int len)
{
    uintptr_t end = address + (uintptr_t)len;

    if (!mNandOk)
        return -1;
    if (mWrDirty && mWrPageAddr >= address && mWrPageAddr < end)
        mWrDirty = 0;
    else if (nand_flush() != 0)
        return -1;
    address &= ~((uintptr_t)NAND_BLOCK_SIZE - 1u);
    while (address < end) {
        if (ZioNandBadBlocks_BlockErase(nand_addr((uint32_t)address))
                != eZioResult_SUCCESS)
            return -1;
        if (mRdValid && (mRdPageAddr & ~((uint32_t)NAND_BLOCK_SIZE - 1u))
                == (uint32_t)address)
            mRdValid = 0;
        address += NAND_BLOCK_SIZE;
    }
    return 0;
}

void ext_flash_lock(void)
{
    if (!mNandOk)
        return;
    (void)nand_flush();
    if (ZioNandBadBlocks_TableWritePending() == eZioBool_TRUE)
        (void)ZioNandBadBlocks_TableWrite();
}

void ext_flash_unlock(void)
{
}
