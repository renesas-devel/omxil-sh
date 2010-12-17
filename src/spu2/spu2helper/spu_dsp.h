/************************************************************************

 Copyright (C) 2009 Renesas Solutions Corp.

 This code is LGPL license

 Kuninori Morimoto <morimoto.kuninori@renesas.com>

************************************************************************/
#ifndef SPU_DSP_H
#define SPU_DSP_H

#include <pthread.h>
#include "uio.h"
#include "common.h"

/************************************************************************


                      struct


************************************************************************/
struct spu_dsp {
	u32		id;
	struct uio	uio;
	/* parameter "addr" of reg_read/write is offset from SPU0.
	 * So, we need offset from SPU0 here. */
	u32		offset;


	u32 pbankc; /* bankc initial value */
	u32 xbankc; /* bankc initial value */
};

int spudsp_init(void);
void spudsp_quit(void);
struct spu_dsp* spudsp_open(u32 id, void (*callback) (void *arg), void *arg);
void spudsp_close(struct spu_dsp *dsp);

u32 spudsp_read(u32 id, u32 addr);
void spudsp_write(u32 id, u32 addr, u32 data);

u32 spudsp_get_workarea_addr(struct spu_dsp *dsp);
u32 spudsp_get_workarea_size(struct spu_dsp *dsp);
u32 spudsp_get_workarea_io(struct spu_dsp *dsp);

#endif
