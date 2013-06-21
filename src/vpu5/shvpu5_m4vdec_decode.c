/**
   src/vpu5/shvpu5_mpegdec_decode.c

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
#include "m4vdec.h"
#include "shvpu5_decode.h"
#include "shvpu5_decode_omx.h"
#include "shvpu5_common_queue.h"
#include "shvpu5_common_uio.h"
#include "shvpu5_common_log.h"
#include "shvpu5_decode_api.h"
#include <vpu5/OMX_VPU5Ext.h>

#include "meram/meram.h"

#ifndef BUFFERING_COUNT
#define BUFFERING_COUNT 15
#endif

#define MAXFPS 30
#define VPU_UNIT(x) ((((x) + 255) /256) * 256)
#define ALIGN_MASK 0xfffff000
#define ALIGN(x) ((x + ~ALIGN_MASK) & ALIGN_MASK);

#define MERAM_BASE 0xe8080000
static inline void *
malloc_aligned(size_t size, int align)
{
	return calloc(1, size);
}
typedef enum {
	MPEG_SP,
	MPEG_ASP,
	H263,
	N_PROFILE,
} MpegProfile;

static const int vbv_sizes [N_PROFILE][OMX_VPU5MpegNLevel] = {
	[MPEG_SP][OMX_VPU5MpegLevel0] = 163840,
	[MPEG_SP][OMX_VPU5MpegLevel1] = 163840,
	[MPEG_SP][OMX_VPU5MpegLevel2] = 655360,
	[MPEG_SP][OMX_VPU5MpegLevel3] = 655360,
	[MPEG_SP][OMX_VPU5MpegLevel4A] = 1310720,
	[MPEG_SP][OMX_VPU5MpegLevel5] = 1835008,
	[MPEG_SP][OMX_VPU5MpegLevel6] = 4063232,

	[MPEG_ASP][OMX_VPU5MpegLevel0] = 163840,
	[MPEG_ASP][OMX_VPU5MpegLevel1] = 163840,
	[MPEG_ASP][OMX_VPU5MpegLevel2] = 655360,
	[MPEG_ASP][OMX_VPU5MpegLevel3] = 655360,
	[MPEG_ASP][OMX_VPU5MpegLevel3B] = 1064960,
	[MPEG_ASP][OMX_VPU5MpegLevel4] = 1310720,
	[MPEG_ASP][OMX_VPU5MpegLevel5] = 1835008,

	[H263][OMX_VPU5MpegLevel_Baseline] = 797632,
};

static int
mpegCodec_init_instrinsic(void ***intrinsic) {
	/* Initilize intrinsic header callbacks*/
	*intrinsic = NULL;
	return 0;
}

static void
mpegCodec_deinit_instrinsic(void **intrinsic) {
}

static unsigned int
mpegCodec_ir_buf_size(int num_views, shvpu_decode_PrivateType *privType,
		     shvpu_decode_codec_t *pCodec) {
	int vbv_size = vbv_sizes[MPEG_SP][OMX_VPU5MpegLevel6];
	int max_slice_cnt = pCodec->cprop.max_slice_cnt;
        int hdr_fr_cnt = vbv_size < 5000000 ?
                MAXFPS + 2 : vbv_size * MAXFPS / 5000000 + 2;
        return  ALIGN(512 * hdr_fr_cnt * 2 * num_views +
                      VPU_UNIT( 992 * max_slice_cnt / 2) *
                      2 * hdr_fr_cnt * num_views + 3072);
}

static unsigned int
mpegCodec_imd_buf_size(int num_views, shvpu_decode_PrivateType *privType,
		      shvpu_decode_codec_t *pCodec) {
	OMX_PARAM_REVPU5MAXPARAM *max_param = &privType->maxVideoParameters;
	int vbv_size = vbv_sizes[MPEG_SP][OMX_VPU5MpegLevel6];
	int mss = vbv_size;
	int mb_width = ((max_param->nWidth + 15) / 16);
	int mb_height = ((max_param->nHeight + 15) / 16);
	int imd_fr_cnt = vbv_size < 5000000 ?
		MAXFPS + 2 : vbv_size * MAXFPS / 5000000 + 2;
	vbv_size = privType->features.thumbnail_mode ?
		mss * 3 : vbv_size;
	return ALIGN((vbv_size + mss * 2) +
		     VPU_UNIT(mb_width * (mb_height + 3) * 8 / 4) *
		     4 * imd_fr_cnt * num_views);
}

static unsigned int
mpegCodec_mv_buf_size(int num_views, shvpu_decode_PrivateType *privType,
		      shvpu_decode_codec_t *pCodec)  {
	OMX_PARAM_REVPU5MAXPARAM *max_param = &privType->maxVideoParameters;
	int mb_width = ((max_param->nWidth + 15) / 16) * 16 / 16;
	int mb_height = ((max_param->nHeight + 15) / 16) * 16 / 16;
	return ALIGN(VPU_UNIT((64 * mb_width * ((mb_height + 3) / 4)) * 4 + 512) *
		     2);
}

static void
mpegCodec_buf_sizes (int num_views, shvpu_decode_PrivateType *priv,
			shvpu_decode_codec_t *pCodec,
			long *imd_size,
			long *ir_size,
			long *mv_size) {
	*imd_size = mpegCodec_imd_buf_size(num_views, priv, pCodec);
	*ir_size = mpegCodec_ir_buf_size(num_views, priv, pCodec);
	*mv_size = mpegCodec_mv_buf_size(num_views, priv, pCodec);
}

typedef struct {
	MERAM *meram;
	int meram_block;
} m4v_priv_data_t;

void mpegCodec_deinit(shvpu_codec_params_t *vpu_codec_params) {
	shvpu_codec_params_t *pCodec = vpu_codec_params;
	m4v_priv_data_t *priv = pCodec->private_data;
	M4VDEC_PARAMS_T *m4vdec_params = pCodec->codec_params;

	phys_pmem_free(m4vdec_params->col_not_coded_buffer_addr,
		m4vdec_params->col_not_coded_buffer_size);
/*	phys_pmem_free(m4vdec_params->dp_buffer_addr,
		m4vdec_params->dp_buffer_size); */
	meram_free_memory_block(priv->meram, priv->meram_block,
		m4vdec_params->dp_buffer_size >> 10);
	meram_close(priv->meram);
	free(priv);
	free(m4vdec_params);
}

static struct codec_init_ops m4v_ops = {
	.init_intrinsic_array = mpegCodec_init_instrinsic,
	.deinit_intrinsic_array = mpegCodec_deinit_instrinsic,
	.calc_buf_sizes = mpegCodec_buf_sizes,
	.intrinsic_func_callback = NULL,
	.deinit_codec = mpegCodec_deinit,
};

int
mpegCodec_init(shvpu_codec_params_t *vpu_codec_params,
			const shvpu_decode_PrivateType *privType) {
	shvpu_codec_params_t *pCodec = vpu_codec_params;
	M4VDEC_PARAMS_T *m4vdec_params;
	const OMX_PARAM_REVPU5MAXPARAM *max_param = &privType->maxVideoParameters;
	int mb_width = ((max_param->nWidth + 15) / 16);
	int mb_height = ((max_param->nHeight + 15) / 16);
	static char ce_file[] = VPU5HG_FIRMWARE_PATH "/pmp4d_h.bin";
	static char vlc_file[] = VPU5HG_FIRMWARE_PATH "/smp4d.bin";
	m4v_priv_data_t *priv;

	/*** initialize decoder ***/
	static const M4VDEC_PARAMS_T _m4vdec_params_def = {
		.vbv_param_enable = MCVDEC_OFF,
		.input_stream_format = MCVDEC_NA,
		.need_search_sc = MCVDEC_ON,
		.mv_info_mode = MCVDEC_OUTPUT,
		.post_filter_mode = MCVDEC_OUTPUT,
	};

	pCodec->ce_firmware_name = ce_file;
	pCodec->vlc_firmware_name = vlc_file;
	pCodec->wbuf_size = 0x108000;
	pCodec->api_tbl = (MCVDEC_API_T *) &m4vdec_api_tbl;
	priv = calloc(1, sizeof(m4v_priv_data_t));

	priv->meram = meram_open();
	if (!priv->meram) {
		free(priv);
		return -1;
	}

	m4vdec_params = pCodec->codec_params =
		calloc(1, sizeof(M4VDEC_PARAMS_T));
	memcpy(m4vdec_params, &_m4vdec_params_def, sizeof(M4VDEC_PARAMS_T));

	m4vdec_params->dp_buffer_size = VPU_UNIT(48 * mb_width *
			mb_height + 392);

	priv->meram_block = meram_alloc_memory_block(priv->meram,
		m4vdec_params->dp_buffer_size >> 10);

	if (priv->meram_block < 0) {
		meram_close(priv->meram);
		free(priv);
		return -1;
	}

	m4vdec_params->dp_buffer_addr = (priv->meram_block << 10) + MERAM_BASE;
/*	pmem_alloc(m4vdec_params->dp_buffer_size,
				1, &m4vdec_params->dp_buffer_addr);*/
	m4vdec_params->col_not_coded_buffer_size = VPU_UNIT((mb_width *
			mb_height + 7)/8);
	pmem_alloc(m4vdec_params->col_not_coded_buffer_size, 1,
				&m4vdec_params->col_not_coded_buffer_addr);
	pCodec->ops = &m4v_ops;
	pCodec->private_data = priv;
	return 0;
}
