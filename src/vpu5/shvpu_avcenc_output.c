/*
 * simple_avcenc: encout.c
 * Copyright (C) 2010 IGEL Co., Ltd
 */
#include <stdio.h>
#include <stdlib.h>
#include "mcvenc.h"
#include "shvpu_avcenc.h"

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

	for (i=0; i<2; i++) {
		if (pCodec->streamBuffer[i].status ==
		    SHVPU_BUFFER_STATUS_READY)
			break;
	}
	if (i>=2) {
		printf("%s: no buffer available\n", __FUNCTION__);
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
	int ret, i;

	logd("%s(%d,%d) invoked.\n", __FUNCTION__,
	     vlc_pic_info ? vlc_pic_info->capt_frm_id : -1,
	     vlc_pic_info ? vlc_pic_info->strm_frm_id : -1);
	logd("%s: pic_type = ", __FUNCTION__);
	switch (vlc_pic_info->vlc_pic_type) {
	case MCVENC_I_PIC: loge("I"); break;
	case MCVENC_P_PIC: loge("P"); break;
	case MCVENC_B_PIC: loge("B"); break;
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

	for (i=0; i<2; i++) {
		pStreamBuffer = &pCodec->streamBuffer[i];
		if (strm_buff_info->buff_addr ==
		    pStreamBuffer->bufferInfo.buff_addr) {
			pStreamBuffer->bufferInfo.strm_size =
				strm_buff_info->strm_size;
			pStreamBuffer->status =
				SHVPU_BUFFER_STATUS_FILL;
			pStreamBuffer->frameId =
				vlc_pic_info->capt_frm_id;
			break;
		}
	}

	if (i>=2) {
		printf("%s: no buffer found.\n", __FUNCTION__);
	}

	return;
}
