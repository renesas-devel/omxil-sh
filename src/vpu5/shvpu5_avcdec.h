/**
   src/vpu5/shvpu5_avcdec.h

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
#ifdef MERAM_ENABLE
#include <meram/meram.h>
#endif

void
free_remaining_streams(queue_t *pSIQueue);

int
decode_finalize(void *context);

typedef struct {
	OMX_BOOL use_buffer_mode;
} decode_features_t;

typedef struct {
#ifdef MERAM_ENABLE
	MERAM *meram;
	ICB *decY_icb;
	ICB *decC_icb;
#endif
} shvpu_meram_t;

#endif /* __SIMPLE_AVCDEC_H_ */
