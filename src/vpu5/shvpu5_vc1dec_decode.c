/**
   src/vpu5/shvpu5_vc1dec_decode.c

   This component implements the VC-1 video codec.
   The VC-1 video encoder/decoder is implemented
   on the Renesas's VPU5HG middleware library.

   Copyright (C) 2013 IGEL Co., Ltd
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
#include "vc1dec.h"
#include "shvpu5_decode.h"
#include "shvpu5_decode_omx.h"
#include "shvpu5_common_queue.h"
#include "shvpu5_common_uio.h"
#include "shvpu5_common_log.h"
#include "shvpu5_decode_api.h"
#include <vpu5/OMX_VPU5Ext.h>

#ifndef BUFFERING_COUNT
#define BUFFERING_COUNT 15
#endif

#define MAXFPS 30
#define VPU_UNIT(x) ((((x) + 255) /256) * 256)
#define ALIGN_MASK 0xfffff000
#define ALIGN(x) ((x + ~ALIGN_MASK) & ALIGN_MASK);

static inline void *
malloc_aligned(size_t size, int align)
{
	return calloc(1, size);
}
typedef enum {
	VC1_SP,
	VC1_MP,
	VC1_AP,
	N_PROFILE,
} MpegProfile;

static const int vbv_sizes [N_PROFILE][OMX_VPU5VC1NLevel] = {
	[VC1_SP][OMX_VPU5VC1LevelLow] = 327680 / 8,
	[VC1_SP][OMX_VPU5VC1LevelMed] = 1261560 / 8,
	[VC1_MP][OMX_VPU5VC1LevelLow] = 5013504 / 8,
	[VC1_MP][OMX_VPU5VC1LevelMed] = 10010624 / 8,
	[VC1_MP][OMX_VPU5VC1LevelHigh] = 40009728 / 8,
	[VC1_AP][OMX_VPU5VC1Level0] = 4096000 / 8,
	[VC1_AP][OMX_VPU5VC1Level1] = 20408000 / 8,
	[VC1_AP][OMX_VPU5VC1Level2] = 40960000 / 8,
	[VC1_AP][OMX_VPU5VC1Level3] = 90112000 / 8,
};

static int
vc1Codec_init_instrinsic(void ***intrinsic) {
	/* Initilize intrinsic header callbacks*/
	*intrinsic = NULL;
	return 0;
}

static void
vc1Codec_deinit_instrinsic(void **intrinsic) {
}

static unsigned int
vc1Codec_ir_buf_size(int num_views, shvpu_decode_PrivateType *privType,
		     shvpu_decode_codec_t *pCodec) {
	OMX_PARAM_REVPU5MAXPARAM *max_param = &privType->maxVideoParameters;
	int vbv_size = vbv_sizes[VC1_AP][max_param->eVPU5VC1Level];
	int max_slice_cnt = pCodec->cprop.max_slice_cnt;
	int mb_width = ((max_param->nWidth + 15) / 16);
	int mb_height = ((max_param->nHeight + 15) / 16);
	int hdr_fr_cnt = vbv_size < 5000000 ?
		MAXFPS + 2 : vbv_size * MAXFPS / 5000000 + 2;
	int bitplane = hdr_fr_cnt * (mb_width + 1) / 2 * mb_height;
        return  ALIGN(512 * hdr_fr_cnt * 2 * num_views +
                      VPU_UNIT( 992 * max_slice_cnt / 2) *
                      2 * hdr_fr_cnt * num_views + 3072);
}

static unsigned int
vc1Codec_imd_buf_size(int num_views, shvpu_decode_PrivateType *privType,
		      shvpu_decode_codec_t *pCodec) {
	OMX_PARAM_REVPU5MAXPARAM *max_param = &privType->maxVideoParameters;
	int vbv_size = vbv_sizes[VC1_AP][max_param->eVPU5VC1Level];
	int mb_width = ((max_param->nWidth + 15) / 16);
	int mb_height = ((max_param->nHeight + 15) / 16);
	int mss = 192 * mb_width * mb_height;
	int imd_fr_cnt = vbv_size < 5000000 ?
		MAXFPS + 2 : vbv_size * MAXFPS / 5000000 + 2;
	mss = mss < 768 ? 768 : mss;
	vbv_size = privType->features.thumbnail_mode ?  mss * 3 : vbv_size;
	return ALIGN((vbv_size + mss * 2) * 4  +
		     VPU_UNIT(mb_width * (mb_height + 3) * 8 / 4) *
		     4 * imd_fr_cnt * num_views);
}

static unsigned int
vc1Codec_mv_buf_size(int num_views, shvpu_decode_PrivateType *privType,
		      shvpu_decode_codec_t *pCodec)  {
	OMX_PARAM_REVPU5MAXPARAM *max_param = &privType->maxVideoParameters;
	int mb_width = ((max_param->nWidth + 15) / 16);
	int mb_height = ((max_param->nHeight + 15) / 16);
	return ALIGN(VPU_UNIT((64 * mb_width * ((mb_height + 3) / 4)) * 4 +
			512) * 2);
}

static void
vc1Codec_buf_sizes (int num_views, shvpu_decode_PrivateType *priv,
			shvpu_decode_codec_t *pCodec,
			long *imd_size,
			long *ir_size,
			long *mv_size) {
	*imd_size = vc1Codec_imd_buf_size(num_views, priv, pCodec);
	*ir_size = vc1Codec_ir_buf_size(num_views, priv, pCodec);
	*mv_size = vc1Codec_mv_buf_size(num_views, priv, pCodec);
}

void vc1Codec_deinit(shvpu_codec_params_t *vpu_codec_params) {
	shvpu_codec_params_t *pCodec = vpu_codec_params;
	VC1DEC_PARAMS_T *vc1dec_params = pCodec->codec_params;

	free(vc1dec_params);
}

static struct codec_init_ops vc1_ops = {
	.init_intrinsic_array = vc1Codec_init_instrinsic,
	.deinit_intrinsic_array = vc1Codec_deinit_instrinsic,
	.calc_buf_sizes = vc1Codec_buf_sizes,
	.intrinsic_func_callback = NULL,
	.deinit_codec = vc1Codec_deinit,
};

int
vc1Codec_init(shvpu_codec_params_t *vpu_codec_params,
			const shvpu_decode_PrivateType *privType) {
	shvpu_codec_params_t *pCodec = vpu_codec_params;
	VC1DEC_PARAMS_T *vc1dec_params;
	const OMX_PARAM_REVPU5MAXPARAM *max_param = &privType->maxVideoParameters;
	int mb_width = ((max_param->nWidth + 15) / 16);
	int mb_height = ((max_param->nHeight + 15) / 16);
	static char ce_file[] = VPU5HG_FIRMWARE_PATH "/pvc1d_h.bin";
	/* default to AP mode */
	static char vlc_file[] = VPU5HG_FIRMWARE_PATH "/svc1apd.bin";

	/*** initialize decoder ***/
	static const VC1DEC_PARAMS_T _vc1dec_params_def = {
		.vbv_param_enable = MCVDEC_OFF,
		.need_search_sc = MCVDEC_ON,
		.mv_info_mode = MCVDEC_OUTPUT,
		.post_filter_mode = MCVDEC_NO_OUTPUT,
	};

	pCodec->ce_firmware_name = ce_file;
	pCodec->vlc_firmware_name = vlc_file;
	pCodec->wbuf_size = 0x108000;
	pCodec->api_tbl = (MCVDEC_API_T *) &vc1dec_api_tbl;

	vc1dec_params = pCodec->codec_params =
		calloc(1, sizeof(VC1DEC_PARAMS_T));
	memcpy(vc1dec_params, &_vc1dec_params_def, sizeof(VC1DEC_PARAMS_T));

	/*TODO: SPMP settings */
	vc1dec_params->input_stream_format = VC1DEC_INPUT_AP_IDX;

	pCodec->ops = &vc1_ops;
	pCodec->private_data = NULL;
	return 0;
}
