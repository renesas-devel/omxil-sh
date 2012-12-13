/**
   src/vpu5/shvpu5_avcdec_decode.c

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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include "mciph.h"
#if defined(VPU_VERSION_5)
#include "mciph.h"
#elif defined(VPU5HA_SERIES)
#include "mciph_ip0_cmn.h"
#include "mciph_ip0_dec.h"
#endif
#include "mcvdec.h"
#include "avcdec.h"
#include "shvpu5_decode.h"
#include "shvpu5_avcdec_bufcalc.h"
#include "shvpu5_decode_omx.h"
#include "shvpu5_common_queue.h"
#include "shvpu5_common_uio.h"
#include "shvpu5_common_log.h"
#include "shvpu5_decode_api.h"

#ifndef BUFFERING_COUNT
#define BUFFERING_COUNT 15
#endif
static inline void *
malloc_aligned(size_t size, int align)
{
	return calloc(1, size);
}

typedef enum {
	AVC_BASELINE = 66,
	AVC_MAIN = 77,
	AVC_HIGH = 100
} AVCProfile;

typedef enum {
    AVC_LEVEL1_B = 9,
    AVC_LEVEL1 = 10,
    AVC_LEVEL1_1 = 11,
    AVC_LEVEL1_2 = 12,
    AVC_LEVEL1_3 = 13,
    AVC_LEVEL2 = 20,
    AVC_LEVEL2_1 = 21,
    AVC_LEVEL2_2 = 22,
    AVC_LEVEL3 = 30,
    AVC_LEVEL3_1 = 31,
    AVC_LEVEL3_2 = 32,
    AVC_LEVEL4 = 40,
    AVC_LEVEL4_1 = 41
} AVCLevel;

#define VP5_IRQ_ENB 0x10
#define VP5_IRQ_STA 0x14

static long header_processed_callback( MCVDEC_CONTEXT_T *context,
				long 	data_type,
				void 	*api_data,
				long	data_id,
				long 	status) {
	shvpu_decode_PrivateType *shvpu_decode_Private;
	OMX_VIDEO_PARAM_PROFILELEVELTYPE *pProfile;
	shvpu_decode_Private = (shvpu_decode_PrivateType *)context->user_info;
	pProfile = &shvpu_decode_Private->pVideoCurrentProfile;
	if (status)
		return -1;
	switch (data_type) {
	case AVCDEC_SPS_SYNTAX:
		switch(((AVCDEC_SPS_SYNTAX_T *)api_data)->sps_profile_idc) {
		case AVC_BASELINE:
			pProfile->eProfile = OMX_VIDEO_AVCProfileBaseline;
			break;
		case AVC_MAIN:
			pProfile->eProfile = OMX_VIDEO_AVCProfileMain;
			break;
		case AVC_HIGH:
			pProfile->eProfile = OMX_VIDEO_AVCProfileHigh;
			break;
		default:
			break;
		}
		switch(((AVCDEC_SPS_SYNTAX_T *)api_data)->sps_level_idc) {
		case AVC_LEVEL1_B:
			pProfile->eLevel = OMX_VIDEO_AVCLevel1b;
			break;
		case AVC_LEVEL1:
		default:
			pProfile->eLevel = OMX_VIDEO_AVCLevel1;
			break;
		case AVC_LEVEL1_1:
			pProfile->eLevel = OMX_VIDEO_AVCLevel11;
			break;
		case AVC_LEVEL1_2:
			pProfile->eLevel = OMX_VIDEO_AVCLevel12;
			break;
		case AVC_LEVEL1_3:
			pProfile->eLevel = OMX_VIDEO_AVCLevel13;
			break;
		case AVC_LEVEL2:
			pProfile->eLevel = OMX_VIDEO_AVCLevel2;
			break;
		case AVC_LEVEL2_1:
			pProfile->eLevel = OMX_VIDEO_AVCLevel21;
			break;
		case AVC_LEVEL2_2:
			pProfile->eLevel = OMX_VIDEO_AVCLevel22;
			break;
		case AVC_LEVEL3:
			pProfile->eLevel = OMX_VIDEO_AVCLevel3;
			break;
		case AVC_LEVEL3_1:
			pProfile->eLevel = OMX_VIDEO_AVCLevel31;
			break;
		case AVC_LEVEL3_2:
			pProfile->eLevel = OMX_VIDEO_AVCLevel32;
			break;
		case AVC_LEVEL4:
			pProfile->eLevel = OMX_VIDEO_AVCLevel4;
			break;
		case AVC_LEVEL4_1:
			pProfile->eLevel = OMX_VIDEO_AVCLevel41;
			break;
		}
	}
	logd("Got a header callback\n");
	return 0;
}

static int
avcCodec_init_instrinsic(void ***intrinsic) {
	/* Initilize intrinsic header callbacks*/
	*intrinsic = calloc(AVCDEC_INTRINSIC_ID_CNT, sizeof (void *));
	if (!*intrinsic)
		return -1;
	memset(*intrinsic, 0, sizeof (void *) * AVCDEC_INTRINSIC_ID_CNT);
	**intrinsic =
		malloc_aligned(sizeof(AVCDEC_SPS_SYNTAX_T), 1);
	if (!**intrinsic)
		return -1;
	return 0;
}

static void
avcCodec_deinit_instrinsic(void **intrinsic) {
	free(*intrinsic);
	free(intrinsic);
}

static unsigned int
avcCodec_ir_buf_size(int num_views, shvpu_decode_PrivateType *privType,
		     shvpu_decode_codec_t *pCodec) {
        return ir_info_size_calc(
		privType->maxVideoParameters.eVPU5AVCLevel,
		pCodec->cprop.max_slice_cnt,
		num_views);
}

static unsigned int
avcCodec_imd_buf_size(int num_views, shvpu_decode_PrivateType *privType,
		      shvpu_decode_codec_t *pCodec) {
	OMX_PARAM_REVPU5MAXPARAM *max_param = &privType->maxVideoParameters;
	return inb_buf_size_calc(
		max_param->eVPU5AVCLevel,
		max_param->nWidth,
		max_param->nHeight, num_views) + 2048;
}

static unsigned int
avcCodec_mv_buf_size(int num_views, shvpu_decode_PrivateType *privType,
		      shvpu_decode_codec_t *pCodec)  {
	OMX_PARAM_REVPU5MAXPARAM *max_param = &privType->maxVideoParameters;
	AVCDEC_PARAMS_T *avcdec_params = pCodec->vpu_codec_params.codec_params;
        return mv_info_size_calc(
                       max_param->nWidth,
                       max_param->nHeight,
                       avcdec_params->max_num_ref_frames_plus1,
		       num_views);
}

static void
avcCodec_buf_sizes (int num_views, shvpu_decode_PrivateType *priv,
			shvpu_decode_codec_t *pCodec,
			long *imd_size,
			long *ir_size,
			long *mv_size) {
	*imd_size = avcCodec_imd_buf_size(num_views, priv, pCodec);
	*ir_size = avcCodec_ir_buf_size(num_views, priv, pCodec);
	*mv_size = avcCodec_mv_buf_size(num_views, priv, pCodec);
}

void avcCodec_deinit(shvpu_codec_params_t *vpu_codec_params) {
	shvpu_codec_params_t *pCodec = vpu_codec_params;
	AVCDEC_PARAMS_T *avcdec_params = pCodec->codec_params;

	free(avcdec_params->slice_buffer_addr);
	free(avcdec_params);
}

static struct codec_init_ops avc_ops = {
	.init_intrinsic_array = avcCodec_init_instrinsic,
	.deinit_intrinsic_array = avcCodec_deinit_instrinsic,
	.calc_buf_sizes = avcCodec_buf_sizes,
	.intrinsic_func_callback = header_processed_callback,
	.deinit_codec = avcCodec_deinit,
};

int
avcCodec_init(shvpu_codec_params_t *vpu_codec_params) {
	shvpu_codec_params_t *pCodec = vpu_codec_params;
	AVCDEC_PARAMS_T *avcdec_params;
	static char ce_file[] = VPU5HG_FIRMWARE_PATH "/p264d_h.bin";
	static char vlc_file[] = VPU5HG_FIRMWARE_PATH "/s264d.bin";

	/*** initialize decoder ***/
	static const AVCDEC_PARAMS_T _avcdec_params_def = {
		.supple_info_enable = MCVDEC_ON,
		.max_num_ref_frames_plus1 = 17,
		.slice_buffer_size = 0x9000, /* 132 * 17 * 16 * (num_view = 1)*/
		.user_dpb_size = 0,
		.constrained_mode_disable = MCVDEC_OFF,
		.eprev_del_enable = MCVDEC_ON,
		.input_stream_format = AVCDEC_WITH_START_CODE,
		.collist_proc_mode = AVCDEC_COLLIST_SLICE,
		.forced_VclHrdBpPresentFlag = MCVDEC_OFF,
		.forced_NalHrdBpPresentFlag = MCVDEC_OFF,
		.forced_CpbDpbDelaysPresentFlag = MCVDEC_OFF,
#if defined(VPU5HA_SERIES)
		.intra_pred_conceal_mode = MCVDEC_INTRA_CONCEAL_DC,
		.need_search_sc = MCVDEC_NA,
		.mv_info_mode = MCVDEC_ALL_OUTPUT,
		.post_filter_mode = MCVDEC_NA,
#endif
	};

#if defined(VPU_VERSION_5)
	pCodec->wbuf_size = 0xea000;
#elif defined(VPU5HA_SERIES)
	pCodec->wbuf_size = 0x190000;
#endif

	pCodec->ce_firmware_name = ce_file;
	pCodec->vlc_firmware_name = vlc_file;
	avcdec_params = pCodec->codec_params =
		calloc(1, sizeof(AVCDEC_PARAMS_T));
	memcpy(avcdec_params, &_avcdec_params_def, sizeof(AVCDEC_PARAMS_T));
	avcdec_params->slice_buffer_addr =
		malloc_aligned(avcdec_params->
			       slice_buffer_size, 4);
	logd("slice_buffer_addr = %p\n",
	     avcdec_params->slice_buffer_addr);
	pCodec->ops = &avc_ops;
	return 0;
}
