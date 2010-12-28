/************************************************************************

 Copyright (C) 2009 Renesas Solutions Corp.

 This code is LGPL license

 Kuninori Morimoto <morimoto.kuninori@renesas.com>

************************************************************************/
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include "uiomux/uiomux.h"
#include "uiohelper.h"

#include "uio.h"
/************************************************************************


                        struct / define


************************************************************************/

#define MAXNAMELEN 256
#if 1
#define perr(a...) printf("uio: " a)
#else
#define perr(a...)
#endif

/************************************************************************

                      local function

                      interrupt

************************************************************************/
static void interrupt_handler(void *arg)
{
	struct uio *uio;

	uio = arg;
	uio->dev.callback (uio->dev.arg);
}

/************************************************************************

                      global function

                      uio open / close

************************************************************************/
void uio_deinit(struct uio *uio)
{
	if (uio->dev.up) {
		UIO_close(uio->dev.up);
		uio->dev.up = NULL;
	}
}

int uio_init(struct uio *uio, const char *name)
{
	ulong paddr_reg, paddr_pmem;
	size_t size_reg, size_pmem;
	void *vaddr_reg;

	if (strcmp(name, "SPU2DSP0") != 0) {
		perr ("Only SPU2DSP0 is supported.\n");
		return -1;
	}
	uio->dev.up = UIO_open(UIOMUX_SH_SPU, &paddr_reg, NULL, &vaddr_reg,
			       NULL, &size_reg, &size_pmem, interrupt_handler,
			       uio);
	if (!uio->dev.up) {
		perr("device open error\n");
		return -1;
	}
	uio->mem.iomem = UIO_pmem_alloc(uio->dev.up, size_pmem, 1, &paddr_pmem);
	if (!uio->mem.iomem) {
		perr("memory setup error\n");
		return -1;
	}
	uio->mem.addr = paddr_pmem;
	uio->mem.size = size_pmem;
	uio->mmio.iomem = vaddr_reg;
	uio->mmio.addr = paddr_reg;
	uio->mmio.size = size_reg;
	return 0;
}

/************************************************************************

                      global function

                        read / write

************************************************************************/
u32 uio_read32(struct uio_map *ump, u32 ofs)
{
	volatile u32 *reg = (volatile u32 *)ump->iomem;

	return reg[ofs / 4];
}

u16 uio_read16(struct uio_map *ump, u32 ofs)
{
	volatile u16 *reg = (volatile u16 *)ump->iomem;

	return reg[ofs / 2];
}

u8 uio_read8(struct uio_map *ump, u32 ofs)
{
	volatile u8 *reg = (volatile u8 *)ump->iomem;

	return reg[ofs];
}

void uio_write32(struct uio_map *ump, u32 val, u32 ofs)
{
	volatile u32 *reg = (volatile u32 *)ump->iomem;

	reg[ofs / 4] = val;
}

void uio_write16(struct uio_map *ump, u16 val, u32 ofs)
{
	volatile u16 *reg = (volatile u16 *)ump->iomem;

	reg[ofs / 2] = val;
}

void uio_write8(struct uio_map *ump, u8 val, u32 ofs)
{
	volatile u8 *reg = (volatile u8 *)ump->iomem;

	reg[ofs] = val;
}

/************************************************************************


                      global function

                       interrupt

************************************************************************/
void uio_set_interrupt_callback(struct uio *uio, void (*callback) (void *arg),
				void *arg)
{
	UIO_interrupt_disable(uio->dev.up);
	if (callback) {
		uio->dev.callback = callback;
		uio->dev.arg = arg;
		UIO_interrupt_enable(uio->dev.up);
	}
}
