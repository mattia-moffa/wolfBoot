CROSS_COMPILE?=arm-none-eabi-
CC:=$(CROSS_COMPILE)gcc

WOLFBOOT_ROOT?=$(abspath ../..)
SHASTA_DIR?=$(WOLFBOOT_ROOT)/ShastaForWolfBoot
MSDK_DIR?=$(abspath $(WOLFBOOT_ROOT)/../card10-firmware-branch/lib/sdk)

CFLAGS:=-mcpu=cortex-m4 -mthumb -Os -g -ffunction-sections -fdata-sections \
  -I$(WOLFBOOT_ROOT) \
  -I$(SHASTA_DIR) \
  -I$(WOLFBOOT_ROOT)/hal/max32666_nand/compat \
  -I$(SHASTA_DIR)/CMSIS/Device/Maxim/MAX32665/Include \
  -I$(SHASTA_DIR)/MaximInc/MAX32665PeriphDriver/Include \
  -I$(MSDK_DIR)/Libraries/CMSIS/Include \
  -DZIO_STATIC=static -DTARGET=MAX32665 -DTARGET_REV=0x4131 \
  -DMAX32666_NAND -DMAX32666_NAND_BBT_CREATE \
  -Wall -Wno-unknown-pragmas -Wno-pointer-sign -Wno-unused-parameter

ZIO_SRCS:=ZioNandBadBlocks.c ZioOnfiMemory.c ZioHalSpi0.c ZioHalGpioUtil.c \
  ZioCrc.c ZioUtils.c ZioMath.c

SRCS:=main.c \
  $(WOLFBOOT_ROOT)/hal/max32666_nand.c \
  $(WOLFBOOT_ROOT)/hal/max32666_nand/zio_shim.c \
  $(addprefix $(SHASTA_DIR)/,$(ZIO_SRCS))

OBJS:=$(addprefix build/,$(notdir $(SRCS:.c=.o)))

vpath %.c . $(WOLFBOOT_ROOT)/hal $(WOLFBOOT_ROOT)/hal/max32666_nand $(SHASTA_DIR)

all: nand_flasher.elf

build/ZioUtils.o: CFLAGS+=-O0

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

nand_flasher.elf: $(OBJS) flasher.ld
	$(CC) -mcpu=cortex-m4 -mthumb -nostartfiles -Wl,--gc-sections -T flasher.ld -o $@ $(OBJS)

clean:
	rm -rf build nand_flasher.elf

.PHONY: all clean
