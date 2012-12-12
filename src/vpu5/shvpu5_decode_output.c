/**
   src/vpu5/shvpu_avcdec5_output.c

   This component implements H.264 / MPEG-4 AVC video codec.
   The H.264 / MPEG-4 AVC video encoder/decoder is implemented
   on the Renesas's VPU5HG middleware library.

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
#include "shvpu5_decode.h"
#include "shvpu5_decode_omx.h"
#include "shvpu5_common_uio.h"
#include "shvpu5_common_log.h"

long
mcvdec_uf_get_frame_memory(MCVDEC_CONTEXT_T *context,
			   long xpic_size,
			   long ypic_size,
			   long required_fmem_cnt,
			   long nsampling,
			   long *fmem_cnt,
			   long fmem_x_size[],
			   long fmem_size[],
			   MCVDEC_FMEM_INFO_T *fmem[])
{
	MCVDEC_FMEM_INFO_T *_fmem;
	size_t fmemsize;
	long fmem_x;
	int i, ret;
	void *ypic_vaddr;
	unsigned long ypic_paddr, cpic_paddr;
	unsigned long align, alloc_size;
        shvpu_decode_PrivateType *shvpu_decode_Private =
                (shvpu_decode_PrivateType *)context->user_info;

	logd("%s(%d, %d, %d, %d) invoked.\n",
	       __FUNCTION__, xpic_size, ypic_size,
	       required_fmem_cnt, nsampling);

	if (shvpu_decode_Private->features.tl_conv_mode == OMX_FALSE) {
		fmem_x = ROUND_2POW(xpic_size, 32);
		align = 32;
		fmemsize = fmem_x * (ROUND_2POW(ypic_size, 16));
		alloc_size = fmemsize * 3 / 2;
	} else {
		unsigned long pitch;
		int next_power = 0;
		int align_bits;

		pitch = xpic_size;
		for (i = 0; i < 32; i++) {
			if (pitch <= 1)
				break;
			if (pitch & 1)
				next_power = 1;
			pitch >>=1;
		}
		pitch = (1 << (i + next_power));
		shvpu_decode_Private->ipmmui_data = init_ipmmu(
			shvpu_decode_Private->uio_start_phys, pitch,
			shvpu_decode_Private->features.tl_conv_tbm,
			shvpu_decode_Private->features.tl_conv_vbm);
		align_bits = i + next_power +
			shvpu_decode_Private->features.tl_conv_tbm;

		fmem_x = pitch;
		align = (1 << align_bits);
		fmemsize = fmem_x * (ROUND_2POW(ypic_size, 16));
		alloc_size = ((fmemsize * 3 / 2) + (align - 1)) & ~(align - 1);
		alloc_size += align;
	}
#ifdef MERAM_ENABLE
	open_meram(&shvpu_decode_Private->meram_data);
	setup_icb(&shvpu_decode_Private->meram_data,
		&shvpu_decode_Private->meram_data.decY_icb,
		fmem_x, ROUND_2POW(ypic_size, 16), 128, 0xD, 1, 21);
	setup_icb(&shvpu_decode_Private->meram_data,
		&shvpu_decode_Private->meram_data.decC_icb,
		fmem_x, ROUND_2POW(ypic_size, 16) / 2, 64, 0xC, 1, 22);
#endif

	/*
	   if the SYNC mode, the required_fmem_cnt value may not
	   be enough because of few (buffered) stream information.
	   A simple heuristic solution is one extra buffer chunk
	   prepared.
	*/
	if (shvpu_decode_Private->avCodec->codecMode == MCVDEC_MODE_SYNC)
		required_fmem_cnt += 1;

	shvpu_decode_Private->avCodec->fmem = (shvpu_fmem_data *)
		calloc (required_fmem_cnt, sizeof(shvpu_fmem_data));

	_fmem = *fmem = (MCVDEC_FMEM_INFO_T *)
		calloc(required_fmem_cnt, sizeof(MCVDEC_FMEM_INFO_T));
	shvpu_decode_Private->avCodec->fmem_size = required_fmem_cnt;
	if (*fmem == NULL || shvpu_decode_Private->avCodec->fmem == NULL)
		return MCVDEC_FMEM_SKIP_BY_USER;


	for (i=0; i<required_fmem_cnt; i++) {
		ypic_vaddr = pmem_alloc(alloc_size, align, &ypic_paddr);
		if (ypic_vaddr == NULL)
			break;
		shvpu_decode_Private->avCodec->fmem[i].fmem_start = ypic_paddr;
		shvpu_decode_Private->avCodec->fmem[i].fmem_len = alloc_size;
		if (shvpu_decode_Private->features.tl_conv_mode == OMX_TRUE) {
			/*alignment offset*/
			ypic_paddr = (ypic_paddr + (align - 1)) & ~(align - 1);
#ifndef VPU_INTERNAL_TL
			/*access via IPMMUI*/
			ypic_paddr = phys_to_ipmmui(
				shvpu_decode_Private->ipmmui_data,
				ypic_paddr);
#endif
		}
		cpic_paddr = ypic_paddr + fmemsize;
		_fmem[i].Ypic_addr = ypic_paddr;
		logd("fmem[%d].Ypic_addr = %lx\n", i, _fmem[i].Ypic_addr);
		_fmem[i].Ypic_bot_addr = ypic_paddr + fmemsize / 2;
		logd("fmem[%d].Ypic_bot_addr = %lx\n",
		       i, _fmem[i].Ypic_bot_addr);
		_fmem[i].Cpic_addr = cpic_paddr;
		logd("fmem[%d].Cpic_addr = %lx\n", i, _fmem[i].Cpic_addr);
		_fmem[i].Cpic_bot_addr = cpic_paddr + fmemsize / 4;
		logd("fmem[%d].Cpic_bot_addr = %lx\n", i,
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
