/**
   src/vpu5/shvpu5_decode.h

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
#ifndef __SIMPLE_AVCDEC_H_
#define __SIMPLE_AVCDEC_H_
#include "mcvdec.h"
#include "queue.h"
#include <OMX_Types.h>
#include <OMX_Core.h>
#include <OMX_Component.h>

void
free_remaining_streams(queue_t *pSIQueue);

int
decode_finalize(void *context);

typedef struct {
	OMX_BOOL use_buffer_mode;
	OMX_BOOL dmac_mode;
	OMX_BOOL tl_conv_mode;
	OMX_U32  tl_conv_vbm;
	OMX_U32  tl_conv_tbm;
	OMX_BOOL thumbnail_mode;
} decode_features_t;

struct mem_list {
	struct mem_list *p_next;
	size_t size;
	void *va;
};

void *
pmem_alloc_reuse(struct mem_list **mlistHead, size_t size, size_t *asize,
			int align);

/* ROUND_2POW rounds up to the next muliple of y,
   which must be a power of 2 */
#define ROUND_2POW(x,y) ((x + (y - 1) ) & ~(y - 1))
#ifdef OUTPUT_BUFFER_ALIGN
#define ALIGN_STRIDE(x) (ROUND_2POW(x,OUTPUT_BUFFER_ALIGN))
#else
#define ALIGN_STRIDE(x) (x)
#endif

#define ROUND_NEXT_POW2(out, in) \
	{ \
		out = (in - 1); \
		out |= (out >> 1); \
		out |= (out >> 2); \
		out |= (out >> 4); \
		out |= (out >> 8); \
		out++; \
	}
#endif /* __SIMPLE_AVCDEC_H_ */
