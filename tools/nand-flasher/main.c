/* GDB-driven SPI NAND flasher stub for MAX32666.
 *
 * Protocol: the host sets fl_cmd/fl_buf, resumes, and the stub stops at
 * flasher_ready() after each command with the result in fl_cmd.status.
 * Used by flash_nand_max32666.sh via flash_gdb.py
 */
#include <stdint.h>
#include "hal/max32666.h"
#include "max32665.h"
#include "gpio_regs.h"
#include "ZioHalSpi0.h"
#include "ZioNandBadBlocks.h"
#include "ZioOnfiMemory.h"
#include "ZioResult.h"

extern int max32666_nand_init(void);
extern int ext_flash_read(uintptr_t address, uint8_t *data, int len);
extern int ext_flash_write(uintptr_t address, const uint8_t *data, int len);
extern int ext_flash_erase(uintptr_t address, int len);
extern void ext_flash_lock(void);

#define NAND_BLOCK_SIZE 0x40000u

enum {
    CMD_IDLE = 0,
    CMD_INIT,
    CMD_PROGRAM,   /* erase block at addr, write len bytes from fl_buf */
    CMD_READ,      /* read len bytes at addr into fl_buf */
    CMD_FINALIZE,  /* flush page buffer + pending bad block table */
    CMD_CHIPID,    /* read the 2-byte NAND ID into fl_buf */
    CMD_FACTORY_RESET /* physical chip erase + fresh empty bad block table */
};

volatile struct {
    uint32_t cmd;
    uint32_t addr;
    uint32_t len;
    int32_t  status;
} fl_cmd;

uint8_t fl_buf[NAND_BLOCK_SIZE] __attribute__((aligned(4)));

extern unsigned int _sbss, _ebss, _estack;

void flasher_ready(void);
void isr_reset(void);
void isr_fault(void);

static void clock_init(void)
{
    GCR_CLKCN |= GCR_CLKCN_HIRC96M_EN;
    while (!(GCR_CLKCN & GCR_CLKCN_HIRC96M_RDY)) {}
    GCR_CLKCN = (GCR_CLKCN & ~(GCR_CLKCN_CLKSEL_MASK | GCR_CLKCN_PSC_MASK)) |
                GCR_CLKCN_CLKSEL_HIRC96 |
                GCR_CLKCN_HIRC96M_EN;
    while (!(GCR_CLKCN & GCR_CLKCN_CKRDY)) {}
}

/* Mirror the parts of the customer SystemInit the Zio stack relies on:
 * GPIO clocks on, all pins VDDIOH with 25K pullups */
static void board_init(void)
{
    GCR_PERCKCN0 &= ~(GCR_PERCKCN0_GPIO0D | GCR_PERCKCN0_GPIO1D);
    MXC_GPIO0->vssel |= 0xFFFFFFFFu;
    MXC_GPIO0->ps |= 0xFFFFFFFFu;
    MXC_GPIO0->pad_cfg1 |= 0xFFFFFFFFu;
    MXC_GPIO0->pad_cfg2 &= ~0xFFFFFFFFu;
    MXC_GPIO1->vssel |= 0xFFFFFFFFu;
    MXC_GPIO1->ps |= 0xFFFFFFFFu;
    MXC_GPIO1->pad_cfg1 |= 0xFFFFFFFFu;
    MXC_GPIO1->pad_cfg2 &= ~0xFFFFFFFFu;
}

__attribute__((noinline)) void flasher_ready(void)
{
    __asm__ volatile("nop");
}

static int do_program(void)
{
    if (fl_cmd.addr & (NAND_BLOCK_SIZE - 1u))
        return -2;
    if (fl_cmd.len > NAND_BLOCK_SIZE)
        return -3;
    if (ext_flash_erase(fl_cmd.addr, (int)NAND_BLOCK_SIZE) != 0)
        return -4;
    if (ext_flash_write(fl_cmd.addr, fl_buf, (int)fl_cmd.len) != (int)fl_cmd.len)
        return -5;
    return 0;
}

__attribute__((noreturn)) void flasher_main(void)
{
    unsigned int *p;

    for (p = &_sbss; p < &_ebss; p++)
        *p = 0;
    clock_init();
    board_init();
    for (;;) {
        flasher_ready();
        switch (fl_cmd.cmd) {
        case CMD_IDLE:
            fl_cmd.status = 0;
            break;
        case CMD_INIT:
            fl_cmd.status = max32666_nand_init();
            if (fl_cmd.status != 0) {
                /* >0: ZioResult.h error from the ONFI layer;
                 * <=-1000: -(1000 + BBT init ZioResult.h error) */
                eZioResult_T r = ZioOnfiMemory_Init();
                if (r != eZioResult_SUCCESS)
                    fl_cmd.status = (int32_t)r;
                else
                    fl_cmd.status = -1000 -
                        (int32_t)ZioNandBadBlocks_Init();
            }
            break;
        case CMD_PROGRAM:
            fl_cmd.status = do_program();
            break;
        case CMD_READ:
            fl_cmd.status = (fl_cmd.len <= NAND_BLOCK_SIZE &&
                    ext_flash_read(fl_cmd.addr, fl_buf,
                        (int)fl_cmd.len) == (int)fl_cmd.len) ? 0 : -1;
            break;
        case CMD_FINALIZE:
            ext_flash_lock();
            fl_cmd.status = 0;
            break;
        case CMD_CHIPID:
            ZioHalSpi0_Init();
            fl_cmd.status = (ZioOnfiMemory_ReadChipId(fl_buf) ==
                    eZioResult_SUCCESS) ? 0 : -1;
            break;
        case CMD_FACTORY_RESET:
            if (ZioOnfiMemory_Init() != eZioResult_SUCCESS)
                fl_cmd.status = -1;
            else if (ZioNandBadBlocks_ChipErase(eZioBool_TRUE) !=
                    eZioResult_SUCCESS)
                fl_cmd.status = -2;
            else if (ZioNandBadBlocks_TableWrite() != eZioResult_SUCCESS)
                fl_cmd.status = -3;
            else
                fl_cmd.status = max32666_nand_init();
            break;
        default:
            fl_cmd.status = -1;
            break;
        }
        fl_cmd.cmd = CMD_IDLE;
    }
}

void isr_fault(void)
{
    for (;;)
        __asm__ volatile("bkpt 1");
}

__attribute__((naked, noreturn)) void isr_reset(void)
{
    __asm__ volatile(
        "ldr r0, =_estack\n\t"
        "msr msp, r0\n\t"
        "ldr r0, =vectors\n\t"
        "ldr r1, =0xE000ED08\n\t"   /* SCB->VTOR */
        "str r0, [r1]\n\t"
        "b flasher_main\n\t");
}

__attribute__((section(".isr_vector"), used))
void (* const vectors[16])(void) = {
    (void (*)(void))&_estack,
    isr_reset,
    isr_fault, isr_fault, isr_fault, isr_fault, isr_fault, isr_fault,
    isr_fault, isr_fault, isr_fault, isr_fault, isr_fault, isr_fault,
    isr_fault, isr_fault,
};
