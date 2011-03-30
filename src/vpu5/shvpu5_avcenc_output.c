/**
   src/vpu5/shvpu5_avcenc_output.c

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
#include "mcvenc.h"
#include "shvpu5_avcenc.h"
#include "shvpu5_common_log.h"

long
mcvenc_uf_set_strm_addr(MCVENC_CONTEXT_T *context,
			MCVENC_VLC_PIC_INFO_T *vlc_pic_info,
			MCVENC_STRM_BUFF_INFO_T *strm_buff_info,
			long vlc_mode)
{
	shvpu_codec_t *pCodec = (shvpu_codec_t *)context->user_info;
	int i;

	logd("%s(%s,%d,%d,%d) invoked.\n", __FUNCTION__,
	     (vlc_mode == MCVENC_VLC_START) ? "MCVENC_VLC_START" :
	     ((vlc_mode == MCVENC_VLC_RESTART) ? "MCVENC_VLC_RESTART" :
	      "MCVENC_VLC_CONTINUE"),
	     vlc_pic_info ? vlc_pic_info->capt_frm_id : -1,
	     vlc_pic_info ? vlc_pic_info->strm_frm_id : -1,
	     vlc_pic_info ? vlc_pic_info->is_size : -1);

	for (i=0; i<SHVPU_AVCENC_OUTBUF_NUM; i++) {
		if (pCodec->streamBuffer[i].status ==
		    SHVPU_BUFFER_STATUS_READY)
			break;
	}
	if (i>=SHVPU_AVCENC_OUTBUF_NUM) {
		loge("%s: no buffer available\n", __FUNCTION__);
		return MCVENC_VLC_CANCEL;
	}

	/* TODO: insert AUD */

	strm_buff_info->buff_addr =
		pCodec->streamBuffer[i].bufferInfo.buff_addr;
	strm_buff_info->buff_size =
		pCodec->streamBuffer[i].bufferInfo.buff_size;
	pCodec->streamBuffer[i].status =
		SHVPU_BUFFER_STATUS_SET;

	return MCVENC_NML_END;
}

void
mcvenc_uf_strm_available(MCVENC_CONTEXT_T *context,
			 MCVENC_VLC_PIC_INFO_T *vlc_pic_info,
			 MCVENC_STRM_BUFF_INFO_T *strm_buff_info)
{
	shvpu_codec_t *pCodec = (shvpu_codec_t *)context->user_info;
	shvpu_avcenc_outbuf_t *pStreamBuffer;
	int i;

	logd("%s(%d,%d) invoked.\n", __FUNCTION__,
	     vlc_pic_info ? vlc_pic_info->capt_frm_id : -1,
	     vlc_pic_info ? vlc_pic_info->strm_frm_id : -1);
	logd("%s: pic_type = ", __FUNCTION__);
	switch (vlc_pic_info->vlc_pic_type) {
	case MCVENC_I_PIC: logd("I"); break;
	case MCVENC_P_PIC: logd("P"); break;
	case MCVENC_B_PIC: logd("B"); break;
	default: loge("UNKNOWN"); break;
	}
        logd("-PIC, struct = ");
	switch (vlc_pic_info->vlc_pic_struct) {
	case MCVENC_TOP_FLD_PIC: logd("Top"); break;
	case MCVENC_BOT_FLD_PIC: logd("Bottom"); break;
	case MCVENC_FRAME_PIC: logd("Frame"); break;
	default: logd("UNKNOWN"); break;
	}
	logd(", size = %d\n", strm_buff_info->strm_size);

	for (i=0; i<SHVPU_AVCENC_OUTBUF_NUM; i++) {
		pStreamBuffer = &pCodec->streamBuffer[i];
		if (strm_buff_info->buff_addr ==
		    pStreamBuffer->bufferInfo.buff_addr) {
			pStreamBuffer->bufferInfo.strm_size =
				strm_buff_info->strm_size;
			pStreamBuffer->status =
				SHVPU_BUFFER_STATUS_FILL;
			pStreamBuffer->frameId =
				vlc_pic_info->strm_frm_id;
			if (vlc_pic_info->vlc_pic_type == MCVENC_I_PIC)
				pStreamBuffer->picType = SHVPU_PICTURE_TYPE_I;
			else if (vlc_pic_info->vlc_pic_type == MCVENC_B_PIC)
				pStreamBuffer->picType = SHVPU_PICTURE_TYPE_B;
			else if (vlc_pic_info->vlc_pic_type == MCVENC_P_PIC)
				pStreamBuffer->picType = SHVPU_PICTURE_TYPE_P;
			break;
		}
	}

	if (i>=SHVPU_AVCENC_OUTBUF_NUM) {
		loge("%s: no buffer found.\n", __FUNCTION__);
	}

	if (pCodec->cmnProp.B_pic_mode == 0) {
		/* for using the VPU exclusively */
		uiomux_unlock_vpu();
	}
	return;
}
