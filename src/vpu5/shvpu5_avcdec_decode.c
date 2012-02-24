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
#elif defined(VPU_VERSION_5HA)
#include "mciph_ip0_cmn.h"
#include "mciph_ip0_dec.h"
#endif
#include "mcvdec.h"
#include "avcdec.h"
#include "shvpu5_avcdec.h"
#include "shvpu5_avcdec_bufcalc.h"
#include "shvpu5_avcdec_omx.h"
#include "shvpu5_common_queue.h"
#include "shvpu5_common_uio.h"
#include "shvpu5_common_log.h"

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

#define VP5_IRQ_ENB 0xfe900010
#define VP5_IRQ_STA 0xfe900014

long header_processed_callback( MCVDEC_CONTEXT_T *context,
				long 	data_type,
				void 	*api_data,
				long	data_id,
				long 	status) {
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private;
	OMX_VIDEO_PARAM_PROFILELEVELTYPE *pProfile;
	shvpu_avcdec_Private = (shvpu_avcdec_PrivateType *)context->user_info;
	pProfile = &shvpu_avcdec_Private->pVideoCurrentProfile;
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
	/* malloc() on 32bit environment must allocate
	   an 8-bytes aligned region. */

long
decode_init(shvpu_avcdec_PrivateType *shvpu_avcdec_Private)
{
	extern const MCVDEC_API_T avcdec_api_tbl;
	shvpu_avcdec_codec_t *pCodec;
	MCVDEC_CONTEXT_T *pContext;
	unsigned long ce_firmware_addr;
	int num_views;
	long ret;
	int zero = 0;

	/*** allocate memory ***/
	pCodec = (shvpu_avcdec_codec_t *)
			calloc(1, sizeof(shvpu_avcdec_codec_t));
	if (pCodec == NULL)
		return -1L;
	memset((void *)pCodec, 0, sizeof(shvpu_avcdec_codec_t));

	/*** workaround clear VP5_IRQ_ENB and VPU5_IRQ_STA ***/
	vpu5_mmio_write(VP5_IRQ_ENB, (unsigned long) &zero, 1);
	vpu5_mmio_write(VP5_IRQ_STA, (unsigned long) &zero, 1);


	/*** initialize driver ***/
	ret = shvpu_driver_init(&pCodec->pDriver);
	if (ret != MCIPH_NML_END)
		return ret;

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
#if defined(VPU_VERSION_5HA)
		.intra_pred_conceal_mode = MCVDEC_INTRA_CONCEAL_DC,
		.need_search_sc = MCVDEC_NA,
		.mv_info_mode = MCVDEC_ALL_OUTPUT,
		.post_filter_mode = MCVDEC_NA,
#endif
	};
	extern unsigned long uio_virt_to_phys(void *, long, unsigned long);
	extern long notify_buffering(MCVDEC_CONTEXT_T *, long);
	extern long notify_userdata(MCVDEC_CONTEXT_T *,
				    MCVDEC_USERDATA_T *, long);
	static const MCVDEC_CMN_PROPERTY_T _cprop_def = {
		.stream_type		= MCVDEC_H264,
		.max_slice_cnt		= 16,
		.fmem_alloc_mode	= MCVDEC_ALLOC_FRAME_OR_FIELD,
		.output_unit		= MCVDEC_UNIT_FRAME,
		.fmem_notice_mode	= MCVDEC_FMEM_INDEX_ENABLE,
		.first_hdr_enable	= MCVDEC_OFF,
		.ec_mode		= MCVDEC_ECMODE_TYPE1,
		.max_imd_ratio_10	= 80,
		.func_get_intrinsic_header	= header_processed_callback,
		.func_userdata_callback		= notify_userdata,
		.func_imd_buffering_ready	= notify_buffering,
		.virt_to_phys_func		= uio_virt_to_phys,
#if defined(VPU_VERSION_5HA)
		.buffering_pic_cnt		= BUFFERING_COUNT,
		.ce_config		= MCVDEC_2CE,
		.num_views		= 1,
#endif
	};
	static const MCVDEC_WORK_INFO_T _wbuf_dec_def = {
#if defined(VPU_VERSION_5)
		.work_area_size = 0xea000,  /* 104 + 832KiB */
#elif defined(VPU_VERSION_5HA)
		.work_area_size = 0x190000,  /* 2048 + 915KiB + 630KiB */
#endif
	};

	pCodec->avcdec_params = _avcdec_params_def;
	pCodec->avcdec_params.slice_buffer_addr =
		malloc_aligned(pCodec->avcdec_params.
			       slice_buffer_size, 4);
	logd("slice_buffer_addr = %p\n",
	     pCodec->avcdec_params.slice_buffer_addr);

	pCodec->wbuf_dec = _wbuf_dec_def;
	pCodec->wbuf_dec.work_area_addr =
		malloc_aligned(pCodec->wbuf_dec.work_area_size, 4);
	logd("work_area_addr = %p\n",
	     pCodec->wbuf_dec.work_area_addr);
	ce_firmware_addr =
		shvpu5_load_firmware(VPU5HG_FIRMWARE_PATH "/p264d_h.bin",
			&pCodec->fw_size.ce_firmware_size);

	logd("ce_firmware_addr = %lx\n", ce_firmware_addr);
	pCodec->fw.vlc_firmware_addr =
		shvpu5_load_firmware(VPU5HG_FIRMWARE_PATH "/s264d.bin",
			&pCodec->fw_size.vlc_firmware_size);
	logd("vlc_firmware_addr = %lx\n",
	     pCodec->fw.vlc_firmware_addr);

	pCodec->cprop = _cprop_def;

#if defined(VPU_VERSION_5)
	pCodec->fw.ce_firmware_addr = ce_firmware_addr;
	num_views = 1;
#elif defined(VPU_VERSION_5HA)
	pCodec->fw.ce_firmware_addr[0] = ce_firmware_addr;
	num_views = pCodec->cprop.num_views;
#endif

	pCodec->cprop.codec_params = &pCodec->avcdec_params;
	/* Initilize intrinsic header callbacks*/
	memset(shvpu_avcdec_Private->intrinsic, 0, sizeof (void *) *
		AVCDEC_INTRINSIC_ID_CNT);
	shvpu_avcdec_Private->intrinsic[0] =
		malloc_aligned(sizeof(AVCDEC_SPS_SYNTAX_T), 1);

	logd("----- invoke mcvdec_init_decoder() -----\n");
	ret = mcvdec_init_decoder((MCVDEC_API_T *)&avcdec_api_tbl,
				  &pCodec->cprop,
				  &pCodec->wbuf_dec,
				  &pCodec->fw, shvpu_avcdec_Private->intrinsic,
				  pCodec->pDriver->pDrvInfo, &pContext);
	logd("----- resume from mcvdec_init_decoder() -----\n");
	if (ret != MCIPH_NML_END)
		return ret;

	/*** initialize work area ***/
	void *vaddr;
	unsigned long paddr;

	pCodec->imd_info.imd_buff_size = inb_buf_size_calc(
		shvpu_avcdec_Private->maxVideoParameters.eVPU5AVCLevel,
		shvpu_avcdec_Private->maxVideoParameters.nWidth,
		shvpu_avcdec_Private->maxVideoParameters.nHeight,
		num_views);
	/* VPU may access more 2048 bytes over the buffer.*/
	pCodec->imd_info.imd_buff_size += 2048; 

	vaddr = pmem_alloc(pCodec->imd_info.imd_buff_size,
				32, &pCodec->imd_info.imd_buff_addr);
	logd("imd_info.imd_buff_addr = %lx\n",
	       pCodec->imd_info.imd_buff_addr);
	if (!vaddr)
		return -1;
	pCodec->imd_info.imd_buff_mode = MCVDEC_MODE_NOMAL;

        pCodec->ir_info.ir_info_size = ir_info_size_calc(
		shvpu_avcdec_Private->maxVideoParameters.eVPU5AVCLevel,
		pCodec->cprop.max_slice_cnt,
		num_views);

	pCodec->ir_info.ir_info_addr = (unsigned long)
		pmem_alloc(pCodec->ir_info.ir_info_size, 32, &paddr);
	logd("ir_info.ir_info_addr = %lx\n", pCodec->ir_info.ir_info_addr);
	if (!pCodec->ir_info.ir_info_addr)
		return -1;

        pCodec->mv_info.mv_info_size = mv_info_size_calc(
                       shvpu_avcdec_Private->maxVideoParameters.nWidth,
                       shvpu_avcdec_Private->maxVideoParameters.nHeight,
                       pCodec->avcdec_params.max_num_ref_frames_plus1,
		       num_views);

	vaddr = pmem_alloc(pCodec->mv_info.mv_info_size,
				32, &pCodec->mv_info.mv_info_addr);
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
	if (shvpu_avcdec_Private->enable_sync) {
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
	pContext->user_info = (void *)shvpu_avcdec_Private;
	shvpu_avcdec_Private->avCodec = pCodec;
	shvpu_avcdec_Private->avCodecContext = pContext;

	return ret;
}

void
decode_deinit(shvpu_avcdec_PrivateType *shvpu_avcdec_Private) {
	buffer_avcdec_metainfo_t *pBMI;

	if (shvpu_avcdec_Private) {
		shvpu_avcdec_codec_t *pCodec = shvpu_avcdec_Private->avCodec;
		decode_finalize(shvpu_avcdec_Private->avCodecContext);
		if (shvpu_avcdec_Private->intrinsic)
			free(shvpu_avcdec_Private->intrinsic[0]);
		if (shvpu_avcdec_Private->avCodec && 
			shvpu_avcdec_Private->avCodec->fmem) {
			int i, bufs = shvpu_avcdec_Private->avCodec->fmem_size;

			shvpu_fmem_data *outbuf = shvpu_avcdec_Private->avCodec->fmem;
			for (i = 0 ; i < bufs; i++) {
				phys_pmem_free(outbuf->fmem_start,
						outbuf->fmem_len);
				outbuf++;
			}
			free(shvpu_avcdec_Private->avCodec->fmem);
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
#elif defined(VPU_VERSION_5HA)
		phys_pmem_free(pCodec->fw.ce_firmware_addr[0],
			pCodec->fw_size.ce_firmware_size);
#endif
		phys_pmem_free(pCodec->fw.vlc_firmware_addr,
			pCodec->fw_size.vlc_firmware_size);

		free_remaining_streams(pCodec->pSIQueue);
		free(pCodec->pSIQueue);

#ifdef MERAM_ENABLE
		close_meram(&shvpu_avcdec_Private->meram_data);
#endif

		free(pCodec);

		shvpu_avcdec_Private->avCodec = NULL;
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
