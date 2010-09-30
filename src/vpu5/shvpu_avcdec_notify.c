/**
   src/vpu/shvpu_avcdec_notify.c

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
#include "mcvdec.h"
#include "shvpu_avcdec.h"
#include "shvpu_avcdec_omx.h"

long
notify_buffering(MCVDEC_CONTEXT_T *context, long status)
{
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		(shvpu_avcdec_PrivateType *)context->user_info;
	shvpu_codec_t *pCodec = shvpu_avcdec_Private->avCodec;

	logd("%s(%ld) invoked.\n", __FUNCTION__, status);
	pCodec->enoughPreprocess = OMX_TRUE;
	if (pCodec->enoughHeaders)
		if (shvpu_avcdec_Private->enable_sync)
			pCodec->codecMode = MCVDEC_MODE_SYNC;
		else
			pCodec->codecMode = MCVDEC_MODE_MAIN;

	return MCVDEC_NML_END;
}

long
notify_userdata(MCVDEC_CONTEXT_T *context,
		MCVDEC_USERDATA_T *userdata, long userdata_layer)
{
	loge("%s() invoked.\n", __FUNCTION__);
	return MCVDEC_NML_END;
}
