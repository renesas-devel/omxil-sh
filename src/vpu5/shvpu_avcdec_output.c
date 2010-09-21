/**
   src/vpu/shvpu_avcdec_output.c

   This component implements H.264 / MPEG-4 AVC video decoder.
   The H.264 / MPEG-4 AVC Video decoder is implemented on the
   Renesas's VPU5HG middleware library.

   Copyright (C) 2010 IGEL Co., Ltd
   Copyright (C) 2010 Renesas Solutions Corp.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation; either version 2.1 of
   the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
   02110-1301 USA

*/

#include <stdio.h>
#include <stdlib.h>
#include "mcvdec.h"
#include "shvpu_avcdec.h"
#include "shvpu_avcdec_omx.h"

long
mcvdec_uf_get_frame_memory(MCVDEC_CONTEXT_T *context,
			   long xpic_size,
			   long ypic_size,
			   long requrired_fmem_cnt,
			   long nsampling,
			   long *fmem_cnt,
			   long fmem_x_size[],
			   long fmem_size[],
			   MCVDEC_FMEM_INFO_T *fmem[])
{
	MCVDEC_FMEM_INFO_T *_fmem;
	size_t fmemsize;
	long fmem_x;
	int i;
	void *ypic_vaddr, *cpic_vaddr;
	unsigned int ypic_paddr, cpic_paddr;
        shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
                (shvpu_avcdec_PrivateType *)context->user_info;

	loge("%s(%d, %d, %d, %d) invoked.\n",
	       __FUNCTION__, xpic_size, ypic_size,
	       requrired_fmem_cnt, nsampling);
	fmem_x = (xpic_size + 15) / 16 * 16;
	fmemsize = fmem_x * ((ypic_size + 15) / 16 * 16);

/*	if (_fmem != NULL)
		free(_fmem);*/
	_fmem = shvpu_avcdec_Private->avCodec->fmem = *fmem =
		(MCVDEC_FMEM_INFO_T *)
		malloc(sizeof(MCVDEC_FMEM_INFO_T) * requrired_fmem_cnt);
	shvpu_avcdec_Private->avCodec->fmem_size = requrired_fmem_cnt;
	if (*fmem == NULL)
		return MCVDEC_FMEM_SKIP_BY_USER;

	for (i=0; i<requrired_fmem_cnt; i++) {
		ypic_vaddr = pmem_alloc(fmemsize, 32, &ypic_paddr);
		if (ypic_vaddr == NULL)
			break;
		cpic_vaddr = pmem_alloc(fmemsize / 2, 32, &cpic_paddr);
		if (cpic_vaddr == NULL) {
			pmem_free(ypic_vaddr, fmemsize);
			break;
		}
		_fmem[i].Ypic_addr = ypic_paddr;
		loge("fmem[%d].Ypic_addr = %lx\n", i, _fmem[i].Ypic_addr);
		_fmem[i].Ypic_bot_addr = ypic_paddr + fmemsize / 2;
		loge("fmem[%d].Ypic_bot_addr = %lx\n",
		       i, _fmem[i].Ypic_bot_addr);
		_fmem[i].Cpic_addr = cpic_paddr;
		loge("fmem[%d].Cpic_addr = %lx\n", i, _fmem[i].Cpic_addr);
		_fmem[i].Cpic_bot_addr = cpic_paddr + fmemsize / 4;
		loge("fmem[%d].Cpic_bot_addr = %lx\n", i,
		       _fmem[i].Cpic_bot_addr);
		*fmem_cnt = i + 1;
	}

	fmem_x_size[MCVDEC_FMX_DEC] =
		fmem_x_size[MCVDEC_FMX_REF] =
		fmem_x_size[MCVDEC_FMX_FLT] = fmem_x;
	fmem_size[MCVDEC_FMX_DEC] =
		fmem_size[MCVDEC_FMX_REF] =
		fmem_size[MCVDEC_FMX_FLT] = fmemsize;

	return (i > 0) ? MCVDEC_NML_END : MCVDEC_FMEM_SKIP_BY_USER;
}
