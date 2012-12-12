/**
   src/vpu5/shvpu5_decode.c

   This component implements H.264 / MPEG-4 AVC video codec.
   The H.264 / MPEG-4 AVC video encoder/decoder is implemented
   on the Renesas's VPU5HG middleware library.

   Copyright (C) 2012 IGEL Co., Ltd
   Copyright (C) 2012 Renesas Solutions Corp.

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
#include "shvpu5_avcdec_omx.h"
#include "shvpu5_common_queue.h"
#include "shvpu5_common_uio.h"
#include "shvpu5_common_log.h"
#include "shvpu5_decode_api.h"

#ifndef BUFFERING_COUNT
#define BUFFERING_COUNT 15
#endif

#define VP5_IRQ_ENB 0x10
#define VP5_IRQ_STA 0x14

long
decode_init(shvpu_decode_PrivateType *shvpu_decode_Private)
{
	extern const MCVDEC_API_T avcdec_api_tbl;
	shvpu_avcdec_codec_t *pCodec;
	MCVDEC_CONTEXT_T *pContext;
	long ret;
	int zero = 0;
	unsigned long reg_base;
	unsigned long ce_firmware_addr;
	struct codec_init_ops *cops;
	int num_views;

	/*** allocate memory ***/
	pCodec = (shvpu_avcdec_codec_t *)
			calloc(1, sizeof(shvpu_avcdec_codec_t));
	if (pCodec == NULL)
		return -1L;
	memset((void *)pCodec, 0, sizeof(shvpu_avcdec_codec_t));

	/*** workaround clear VP5_IRQ_ENB and VPU5_IRQ_STA ***/
	reg_base = uio_register_base();
	vpu5_mmio_write(reg_base + VP5_IRQ_ENB, (unsigned long) &zero, 1);
	vpu5_mmio_write(reg_base + VP5_IRQ_STA, (unsigned long) &zero, 1);

	/*** initialize driver ***/
	ret = shvpu_driver_init(&pCodec->pDriver);
	if (ret != MCIPH_NML_END)
		return ret;

	extern unsigned long uio_virt_to_phys(void *, long, unsigned long);
	extern long notify_buffering(MCVDEC_CONTEXT_T *, long);
	extern long notify_userdata(MCVDEC_CONTEXT_T *,
				    MCVDEC_USERDATA_T *, long);
	static const MCVDEC_CMN_PROPERTY_T _cprop_def = {
		.max_slice_cnt		= 16,
		.fmem_alloc_mode	= MCVDEC_ALLOC_FRAME_OR_FIELD,
		.output_unit		= MCVDEC_UNIT_FRAME,
		.fmem_notice_mode	= MCVDEC_FMEM_INDEX_ENABLE,
		.first_hdr_enable	= MCVDEC_OFF,
		.ec_mode		= MCVDEC_ECMODE_TYPE1,
		.max_imd_ratio_10	= 80,
		.func_userdata_callback		= notify_userdata,
		.func_imd_buffering_ready	= notify_buffering,
		.virt_to_phys_func		= uio_virt_to_phys,
#if defined(VPU5HA_SERIES)
		.buffering_pic_cnt		= BUFFERING_COUNT,
		.ce_config		= MCVDEC_2CE,
		.num_views		= 1,
#endif
	};

	pCodec->cprop = _cprop_def;

	switch(shvpu_decode_Private->video_coding_type) {
	case OMX_VIDEO_CodingAVC:
		pCodec->cprop.stream_type		= MCVDEC_H264,
		avcCodec_init(&pCodec->vpu_codec_params);
		break;
	default:
		goto free_pcodec;
		
	};
	cops = pCodec->vpu_codec_params.ops;

	pCodec->cprop.func_get_intrinsic_header	= cops->intrinsic_func_callback;
	pCodec->wbuf_dec.work_area_size = pCodec->vpu_codec_params.wbuf_size;
	pCodec->wbuf_dec.work_area_addr =
		calloc(1, pCodec->wbuf_dec.work_area_size);
	logd("work_area_addr = %p\n",
	     pCodec->wbuf_dec.work_area_addr);

	ce_firmware_addr =
		shvpu5_load_firmware(pCodec->vpu_codec_params.ce_firmware_name,
			&pCodec->fw_size.ce_firmware_size);

	logd("ce_firmware_addr = %lx\n", ce_firmware_addr);
	pCodec->fw.vlc_firmware_addr =
		shvpu5_load_firmware(pCodec->vpu_codec_params.vlc_firmware_name,
			&pCodec->fw_size.vlc_firmware_size);
	logd("vlc_firmware_addr = %lx\n",
	     pCodec->fw.vlc_firmware_addr);

#if defined(VPU_VERSION_5)
	pCodec->fw.ce_firmware_addr = ce_firmware_addr;
	num_views = 1;
#elif defined(VPU5HA_SERIES)
	pCodec->fw.ce_firmware_addr[0] = ce_firmware_addr;
	num_views = pCodec->cprop.num_views;
#endif
	pCodec->cprop.codec_params = pCodec->vpu_codec_params.codec_params;

	cops->init_intrinsic_array(&shvpu_decode_Private->intrinsic);

	logd("----- invoke mcvdec_init_decoder() -----\n");
	ret = mcvdec_init_decoder((MCVDEC_API_T *)&avcdec_api_tbl,
				  &pCodec->cprop,
				  &pCodec->wbuf_dec,
				  &pCodec->fw, shvpu_decode_Private->intrinsic,
				  pCodec->pDriver->pDrvInfo, &pContext);
	logd("----- resume from mcvdec_init_decoder() -----\n");
	if (ret != MCIPH_NML_END)
		return ret;

	/*** initialize work area ***/
	void *vaddr;
	unsigned long paddr;

	cops->calc_buf_sizes(num_views,
		shvpu_decode_Private,
		pCodec,
		&pCodec->imd_info.imd_buff_size,
        	&pCodec->ir_info.ir_info_size,
        	&pCodec->mv_info.mv_info_size);
		

	pCodec->imd_info.imd_buff_mode = MCVDEC_MODE_NOMAL;

	vaddr = pmem_alloc(pCodec->imd_info.imd_buff_size,
				512, &pCodec->imd_info.imd_buff_addr);
	logd("imd_info.imd_buff_addr = %lx\n",
	       pCodec->imd_info.imd_buff_addr);
	if (!vaddr)
		return -1;

	pCodec->ir_info.ir_info_addr = (unsigned long)
		pmem_alloc(pCodec->ir_info.ir_info_size, 512, &paddr);
	logd("ir_info.ir_info_addr = %lx\n", pCodec->ir_info.ir_info_addr);
	if (!pCodec->ir_info.ir_info_addr)
		return -1;

	vaddr = pmem_alloc(pCodec->mv_info.mv_info_size,
				512, &pCodec->mv_info.mv_info_addr);
	logd("mv_info.mv_info_addr = %lx\n", pCodec->mv_info.mv_info_addr);
	if (!vaddr)
		return -1;

	logd("----- invoke mcvdec_set_vpu5_work_area() -----\n");
	ret = mcvdec_set_vpu5_work_area(pContext, &pCodec->imd_info,
					&pCodec->ir_info,
					&pCodec->mv_info);
	logd("----- resume from mcvdec_set_vpu5_work_area() -----\n");
	if (ret != MCIPH_NML_END)
		return ret;

	/*** set play mode ***/
	logd("----- invoke mcvdec_set_play_mode() -----\n");
	ret = mcvdec_set_play_mode(pContext, MCVDEC_PLAY_FORWARD, 0, 0);
	logd("----- resume from mcvdec_set_play_mode() -----\n");

	pCodec->frameCount = pCodec->bufferingCount = 0;

	if (shvpu_decode_Private->enable_sync) {
		pCodec->codecMode = MCVDEC_MODE_SYNC;
		pCodec->outMode = MCVDEC_OUTMODE_PULL;
	} else {
		pCodec->codecMode = MCVDEC_MODE_BUFFERING;
		pCodec->outMode = MCVDEC_OUTMODE_PUSH;
	}

	pCodec->pSIQueue = calloc(1, sizeof(queue_t));
	shvpu_queue_init(pCodec->pSIQueue);
	pCodec->enoughHeaders = pCodec->enoughPreprocess = OMX_FALSE;
	pthread_cond_init(&pCodec->cond_buffering, NULL);
	pthread_mutex_init(&pCodec->mutex_buffering, NULL);
	pContext->user_info = (void *)shvpu_decode_Private;
	shvpu_decode_Private->avCodec = pCodec;
	shvpu_decode_Private->avCodecContext = pContext;

	return ret;

free_pcodec:
	free(pCodec);
	return -1;
}

void
decode_deinit(shvpu_decode_PrivateType *shvpu_decode_Private) {
	if (shvpu_decode_Private) {
		shvpu_avcdec_codec_t *pCodec = shvpu_decode_Private->avCodec;
		decode_finalize(shvpu_decode_Private->avCodecContext);
		if (shvpu_decode_Private->intrinsic)
			pCodec->vpu_codec_params.ops->deinit_intrinsic_array
					(shvpu_decode_Private->intrinsic);
		if (shvpu_decode_Private->avCodec && 
			shvpu_decode_Private->avCodec->fmem) {
			int i, bufs = shvpu_decode_Private->avCodec->fmem_size;

			shvpu_fmem_data *outbuf = shvpu_decode_Private->avCodec->fmem;
			for (i = 0 ; i < bufs; i++) {
				phys_pmem_free(outbuf->fmem_start,
						outbuf->fmem_len);
				outbuf++;
			}
			free(shvpu_decode_Private->avCodec->fmem);
		}

		phys_pmem_free(pCodec->mv_info.mv_info_addr,
			pCodec->mv_info.mv_info_size);
		pmem_free((void *)pCodec->ir_info.ir_info_addr,
			pCodec->ir_info.ir_info_size);
		phys_pmem_free(pCodec->imd_info.imd_buff_addr,
			pCodec->imd_info.imd_buff_size);
#if defined(VPU_VERSION_5)
		phys_pmem_free(pCodec->fw.ce_firmware_addr,
			pCodec->fw_size.ce_firmware_size);
#elif defined(VPU5HA_SERIES)
		phys_pmem_free(pCodec->fw.ce_firmware_addr[0],
			pCodec->fw_size.ce_firmware_size);
#endif
		phys_pmem_free(pCodec->fw.vlc_firmware_addr,
			pCodec->fw_size.vlc_firmware_size);

		free_remaining_streams(pCodec->pSIQueue);
		free(pCodec->pSIQueue);

#ifdef MERAM_ENABLE
		close_meram(&shvpu_decode_Private->meram_data);
#endif

		free(pCodec);

		shvpu_decode_Private->avCodec = NULL;
	}
}

int
decode_finalize(void *context)
{
	int ret;

	logd("----- invoke mcvdec_end_decoder() -----\n");
	ret = mcvdec_end_decoder(context);
	logd("----- resume from mcvdec_end_decoder() -----\n");

	return ret;
}
