/************************************************************************

 Copyright (C) 2009 Renesas Solutions Corp.

 This code is LGPL license

 Kuninori Morimoto <morimoto.kuninori@renesas.com>

************************************************************************/
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "spu_dsp.h"

#define CPUINFO "/proc/cpuinfo"
#define CPUTYPE "cpu type"
#define MAXBUF 1024

static struct spu_dsp spu_dsp[SPU_DSP_MAX];

#define DSP_write(b, c)  uio_write32(&spu_dsp[0].uio.mmio, c, b)
#define DSP_read(b)      uio_read32(&spu_dsp[0].uio.mmio, b)

/* dsp-common */
#define PBANKC0		(0x000FFC00)
#define PBANKC1		(0x000FFC04)
#define PBANKC2		(0x000FFC08)
#define XBANKC0		(0x000FFC10)
#define XBANKC1		(0x000FFC14)
#define XBANKC2		(0x000FFC18)
#define XBANKS0		(0x000FFC1C)
#define XBANKS1		(0x000FFC20)
#define SPUSRST		(0x000FFC24)

#define DSPRST		(0x000FFD00)
#define DSPCORERST	(0x000FFD04)

struct dsp_def {
	u32 total_pbank_num;
	u32 total_xbank_num;

	u32 pbank_num;
	u32 xbank_num;
};

typedef enum {
	SHNONE = 0,
	SH7724,
} cpu_id;

struct cpu_type {
	cpu_id id;
	const char *name;
};

#define CPU(a) {a, #a}
static const struct cpu_type sh_type[] = {
	CPU(SHNONE),
	CPU(SH7724),
};

/************************************************************************


                      static function


************************************************************************/
static struct spu_dsp* spudsp_get(u32 id)
{
	if (id > SPU_DSP_MAX) {
		perr("ID size error\n");
		return NULL;
	}

	return spu_dsp + id;
}

static cpu_id check_cpu(void)
{
	FILE	*cpuf = fopen(CPUINFO , "r");
	char	 buf[MAXBUF];
	char	*pbuf;
	cpu_id	 ret = SHNONE;
	int	 i;

	while (fgets(buf, MAXBUF, cpuf)) {
		if (0 != strncmp(buf , CPUTYPE , strlen(CPUTYPE)))
			continue;

		/* this time buf is..
		 * type1        : value1
		 * type2        : value2...
		 */
		pbuf = strstr(buf, ":");
		if (!pbuf) {
			perr("is this real sh?");
			goto error;
		}

		pbuf++;  /* : */
		pbuf++;  /* space */

		pbuf[strlen(pbuf) - 1] = '\0';

		for (i=0; i<ARRAY_SIZE(sh_type); i++) {
			if (0 == strcmp(pbuf , sh_type[i].name)) {
				ret = sh_type[i].id;
				break;
			}
		}
		break;
	}

error:
	fclose(cpuf);

	return ret;
}

static int get_dsp_def(struct dsp_def *def)
{
	if (!def) {
		perr("no dsp_def\n");
		return -1;
	}

	switch (check_cpu()) {
	case SH7724:
		def->total_pbank_num	= 5;
		def->total_xbank_num	= 7;
		def->pbank_num		= 1;
		def->xbank_num		= 1;
		break;
	default:
		perr("Un-supported CPU\n");
		def->total_pbank_num	= 5;
		def->total_xbank_num	= 7;
		def->pbank_num		= 4;//1;
		def->xbank_num		= 6;//1;
		return 0;
		return -1;
	}

	return 0;
}

static int soft_reset(void)
{
	u32 data;
	int i;

	DSP_write(SPUSRST, 0x00000000);
	for (i=0; i<1024; i++) {
		data = DSP_read(SPUSRST);
		if (!(data & 0x00000001))
			return 0;
		usleep(10);
	}

	perr("soft reset timeout\n");
	return -1;
}

static int dsp_reset(struct spu_dsp *dsp, u32 psetting, u32 xsetting)
{
	u32 pbank, xbank;

	if (!dsp) {
		perr("dsp select error\n");
		return -1;
	}

	/* initialize PBANK / XBANK register
	 * It use SPU_write(0), because P/X BANK is on common area */
	switch (dsp->id) {
	case 0:
		pbank  = PBANKC0;
		xbank  = XBANKC0;
		break;
	case 1:
		pbank  = PBANKC1;
		xbank  = XBANKC1;
		break;
	default:
		perr("unknown dsp\n");
		return -1;
	}

	uio_write32(&dsp->uio.mmio, 0x00000001, dsp->offset + DSPRST);
	uio_write32(&dsp->uio.mmio, 0x00000001, dsp->offset + DSPCORERST);

	DSP_write(pbank, dsp->pbankc | psetting);
	DSP_write(xbank, dsp->xbankc | xsetting);

	return 0;
}

static int dsp_init(struct spu_dsp *dsp, struct dsp_def *def)
{
	u32 pused;
	u32 xused;
	u32 pmask;
	u32 xmask;
	u32 psetting;
	u32 xsetting;

	if (!dsp) {
		perr("dsp get error\n");
		return -1;
	}

	if (0 > dsp_reset(dsp, 0, 0)) {
		perr("dsp reset failed\n");
		return -1;
	}

	/*=====================================
	 *
	 * This code is based on "SPU_InitDSP" on spu_init.c
	 *
	 *=====================================*/

	/* get initial value of BANK register */
	pmask = (u32)~((1 << def->total_pbank_num) - 1);
	xmask = (u32)~((1 << def->total_xbank_num) - 1);

	/* check bank setting conflict
	 * between dsp0 / dsp1 */
	if (0 == dsp->id) {
		dsp->pbankc = DSP_read(PBANKC0) & pmask;
		dsp->xbankc = DSP_read(XBANKC0) & xmask;

		pused = uio_read32(&dsp->uio.mmio, PBANKC1) & ~pmask;
		xused = uio_read32(&dsp->uio.mmio, XBANKC1) & ~pmask;

		psetting = (u32)((1 << def->pbank_num) - 1) << (def->total_pbank_num - def->pbank_num);
		xsetting = (u32)((1 << def->xbank_num) - 1) << (def->total_xbank_num - def->xbank_num);
	} else {
		dsp->pbankc = DSP_read(PBANKC1) & pmask;
		dsp->xbankc = DSP_read(XBANKC1) & xmask;

		pused = uio_read32(&dsp->uio.mmio, PBANKC0) & ~pmask;
		xused = uio_read32(&dsp->uio.mmio, XBANKC0) & ~pmask;

		psetting = (u32)(1 << def->pbank_num) - 1;
		xsetting = (u32)(1 << def->xbank_num) - 1;
	}

	/* bank conflict occurred */
	if ((pused & psetting) ||
	    (xused & xsetting)) {
		perr("bank conflict occurred\n");
		return -1;
	}

	return dsp_reset(dsp, psetting, xsetting);
}

static void linux_io_quit(void)
{
	u32 i;

	for (i=0 ; i<SPU_DSP_MAX ; i++)
		uio_deinit(&spudsp_get(i)->uio);
}

static int linux_io_init(void)
{
	u32 i;
	char buf[1024];
	struct spu_dsp *dsp;

	for (i=0 ; i<SPU_DSP_MAX ; i++) {
		dsp = spudsp_get(i);

		memset(buf, 0, sizeof(buf));
		sprintf(buf, "SPU2DSP%lu", dsp->id);
		if (0 > uio_init(&dsp->uio, buf)) {
			perr("%s init error\n", buf);
			goto linux_io_init_err;
		}
	}

	return 0;

linux_io_init_err:

	linux_io_quit();
	return -1;
}

/************************************************************************


                      global function


************************************************************************/
u32 spudsp_read(u32 id, u32 addr)
{
	struct spu_dsp *dsp = spudsp_get(id);
	if (!dsp) {
		perr("dsp get error\n");
		return 0;
	}

	return uio_read32(&dsp->uio.mmio,
			  addr - dsp->offset);
}

void spudsp_write(u32 id, u32 addr, u32 data)
{
	struct spu_dsp *dsp = spudsp_get(id);
	if (!dsp) {
		perr("dsp get error\n");
		return;
	}

	uio_write32(&dsp->uio.mmio, data,
		    addr - dsp->offset);
}

int spudsp_init(void)
{
	struct dsp_def def;
	struct spu_dsp *dsp;
	u32 i, base;

	if (0 > get_dsp_def(&def)) {
		perr("can't specific SH\n");
		return -1;
	}

	for (i=0; i<SPU_DSP_MAX; i++) {
		dsp = spudsp_get(i);
		dsp->id = i;
	}

	/*
	 * linux io interface is needed to access
	 * to spu2 register
	 */
	if (0 > linux_io_init())
		return -1;

	/*=====================================
	 *
	 * This code is based on "SPU_Init" on spu_init.c
	 *
	 *=====================================*/

	/* SPU - Software reset */
	if (0 > soft_reset()) {
		perr("dsp soft reset failed\n");
		return -1;
	}

	DSP_write(SPUSRST, 0x00000001);

	base = spu_dsp[0].uio.mmio.addr;
	for (i=0; i<SPU_DSP_MAX; i++) {
		dsp = spudsp_get(i);
		dsp->offset = dsp->uio.mmio.addr - base;
		dsp_init(dsp, &def);
	}

	return 0;
}

void spudsp_quit(void)
{
	linux_io_quit();
}

struct spu_dsp* spudsp_open(u32 id, void (*callback) (void *arg), void *arg)
{
	struct spu_dsp *dsp;

	dsp = spudsp_get(id);
	if (!dsp) {
		perr("dsp0 get error\n");
		return NULL;
	}

	/* allow interrupt */
	uio_set_interrupt_callback(&dsp->uio, callback, arg);

	return dsp;
}

void spudsp_close(struct spu_dsp *dsp)
{
	if (!dsp)
		return;

	uio_set_interrupt_callback(&dsp->uio, NULL, NULL);
}

u32 spudsp_get_workarea_addr(struct spu_dsp *dsp)
{
	if (!dsp)
		return 0;

	return dsp->uio.mem.addr;
}

u32 spudsp_get_workarea_size(struct spu_dsp *dsp)
{
	if (!dsp)
		return 0;

	return dsp->uio.mem.size;
}

u32 spudsp_get_workarea_io(struct spu_dsp *dsp)
{
	if (!dsp)
		return 0;

	return (u32)dsp->uio.mem.iomem;
}

u32
read_ieventc (void)
{
	return DSP_read (0x000FFD20);
}
