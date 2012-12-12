/**
   src/vpu5/shvpu5_avcdec_notify.c

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
#include "mcvdec.h"
#include "shvpu5_decode.h"
#include "shvpu5_decode_omx.h"
#include "shvpu5_common_log.h"

long
notify_buffering(MCVDEC_CONTEXT_T *context, long status)
{
	shvpu_decode_PrivateType *shvpu_decode_Private =
		(shvpu_decode_PrivateType *)context->user_info;
	shvpu_decode_codec_t *pCodec = shvpu_decode_Private->avCodec;

	logd("%s(%ld) invoked.\n", __FUNCTION__, status);
	pthread_mutex_lock(&pCodec->mutex_buffering);
	pCodec->enoughPreprocess = OMX_TRUE;
	pthread_cond_broadcast(&pCodec->cond_buffering);
	pthread_mutex_unlock(&pCodec->mutex_buffering);
	if (pCodec->enoughHeaders) {
		if (shvpu_decode_Private->enable_sync)
			pCodec->codecMode = MCVDEC_MODE_SYNC;
		else
			pCodec->codecMode = MCVDEC_MODE_MAIN;
	}
	return MCVDEC_NML_END;
}

long
notify_userdata(MCVDEC_CONTEXT_T *context,
		MCVDEC_USERDATA_T *userdata, long userdata_layer)
{
	logd("%s() invoked.\n", __FUNCTION__);
	return MCVDEC_NML_END;
}
