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
#include "mciph_hg.h"
#include "mcvdec.h"
#include "avcdec.h"
#include "shvpu5_avcdec.h"
#include "shvpu5_avcdec_omx.h"

#define MAX_REF_FRAME_CNT 16
int ibuf_ready;

OMX_U32 noVPUInstance = 0;
static inline void *
malloc_aligned(size_t size, int align)
{
	return calloc(1, size);
}

static unsigned long
load_fw(char *filename, int *size)
{
	void *vaddr;
	unsigned char *p;
	unsigned long paddr;
	int fd;
	size_t len;
	ssize_t ret;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("fw open");
		goto fail_open;
	}
	len = lseek(fd, 0, SEEK_END);
	logd("size of %s = %x\n", filename, len);

	*size = len;
	vaddr = p = pmem_alloc(len, 32, &paddr);
	if (vaddr == NULL) {
		fprintf(stderr, "pmem alloc failed.\n");
		goto fail_pmem_alloc;
	}

	lseek(fd, 0, SEEK_SET);
	do {
		ret = read(fd, p, len);
		if (ret <= 0) {
			perror("read fw");
			goto fail_read;
		}
		len -= ret;
		p += ret;
	} while (len > 0);
	return paddr;
fail_read:
	pmem_free(vaddr, lseek(fd, 0, SEEK_END));
fail_pmem_alloc:
	close(fd);
fail_open:
	return -1;
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
}
	/* malloc() on 32bit environment must allocate
	   an 8-bytes aligned region. */

long
decode_init(shvpu_avcdec_PrivateType *shvpu_avcdec_Private)
{
	extern const MCIPH_API_T mciph_hg_api_tbl;
	extern const MCVDEC_API_T avcdec_api_tbl;
	static const MCIPH_VPU5_INIT_T _vpu5_init_def = {
		.vpu_base_address		= 0xfe900000,
		.vpu_image_endian		= MCIPH_LIT,
		.vpu_stream_endian		= MCIPH_LIT,
		.vpu_firmware_endian		= MCIPH_LIT,
		.vpu_interrupt_enable		= MCIPH_ON,
		.vpu_clock_supply_control	= MCIPH_CLK_CTRL,
		.vpu_constrained_mode		= MCIPH_OFF,
		.vpu_address_mode		= MCIPH_ADDR_32BIT,
		.vpu_reset_mode			= MCIPH_RESET_SOFT,
	};
	shvpu_codec_t *pCodec;
	static shvpu_codec_t pCodec_bak;
	MCVDEC_CONTEXT_T *pContext;
	long ret;

	/*** allocate memory ***/
	pCodec = (shvpu_codec_t *)calloc(1, sizeof(shvpu_codec_t));
	if (pCodec == NULL)
		return -1L;
	memset((void *)pCodec, 0, sizeof(shvpu_codec_t));

	if (noVPUInstance == 0) {
		/*** initialize vpu ***/
		pCodec->wbuf_vpu5.work_size = MCIPH_HG_WORKAREA_SIZE;
		pCodec->wbuf_vpu5.work_area_addr =
			malloc_aligned(pCodec->wbuf_vpu5.work_size, 4);
		logd("work_area_addr = %p\n", pCodec->wbuf_vpu5.work_area_addr);
		if ((pCodec->wbuf_vpu5.work_area_addr == NULL) ||
		    ((unsigned int)pCodec->wbuf_vpu5.work_area_addr & 0x03U))
			return -1L;
		pCodec->vpu5_init = _vpu5_init_def;

		logd("----- invoke mciph_vpu5_init() -----\n");
		ret = mciph_vpu5_init(&pCodec->wbuf_vpu5,
			      (MCIPH_API_T *)&mciph_hg_api_tbl,
				      &pCodec->vpu5_init, &pCodec->drvInfo);
		logd("----- resume from mciph_vpu5_init() -----\n");

		pCodec_bak = *pCodec;

		if (ret != MCIPH_NML_END)
			return ret;
	} else {
		pCodec->wbuf_vpu5 = pCodec_bak.wbuf_vpu5;
		pCodec->vpu5_init = pCodec_bak.vpu5_init;
		pCodec->drvInfo = pCodec_bak.drvInfo;
	}
	noVPUInstance++;

	/*** initialize decoder ***/
	static const AVCDEC_PARAMS_T _avcdec_params_def = {
		.supple_info_enable = MCVDEC_ON,
		.max_num_ref_frames_plus1 = 17,
		.slice_buffer_size = 0x8800, /* 128 * 17 * 16 */
		.user_dpb_size = 0,
		.constrained_mode_disable = MCVDEC_OFF,
		.eprev_del_enable = MCVDEC_ON,
		.input_stream_format = AVCDEC_WITH_START_CODE,
		.collist_proc_mode = AVCDEC_COLLIST_SLICE,
		.forced_VclHrdBpPresentFlag = MCVDEC_OFF,
		.forced_NalHrdBpPresentFlag = MCVDEC_OFF,
		.forced_CpbDpbDelaysPresentFlag = MCVDEC_OFF,
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
		.func_get_intrinsic_header	= header_processed_callback,
		.func_userdata_callback		= notify_userdata,
		.func_imd_buffering_ready	= notify_buffering,
		.virt_to_phys_func		= uio_virt_to_phys,
	};
	static const MCVDEC_WORK_INFO_T _wbuf_dec_def = {
		.work_area_size = 0xea000,  /* 104 + 832KiB */
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
	pCodec->fw.ce_firmware_addr =
		load_fw(VPU5HG_FIRMWARE_PATH "/p264d_h.bin",
			&(pCodec->fw_size.ce_firmware_size));
	logd("ce_firmware_addr = %lx\n", pCodec->fw.ce_firmware_addr);
	pCodec->fw.vlc_firmware_addr =
		load_fw(VPU5HG_FIRMWARE_PATH "/s264d.bin",
			&(pCodec->fw_size.vlc_firmware_size));
	logd("vlc_firmware_addr = %lx\n",
	     pCodec->fw.vlc_firmware_addr);
	pCodec->cprop = _cprop_def;
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
				  pCodec->drvInfo, &pContext);
	logd("----- resume from mcvdec_init_decoder() -----\n");
	if (ret != MCIPH_NML_END)
		return ret;

	/*** initialize work area ***/
	void *vaddr;
	unsigned long paddr;

       pCodec->imd_info.imd_buff_size = inb_buf_size_calc(
                       shvpu_avcdec_Private->maxVideoParameters.eVPU5AVCLevel,
                       shvpu_avcdec_Private->maxVideoParameters.nWidth,
                       shvpu_avcdec_Private->maxVideoParameters.nHeight);

	vaddr = pmem_alloc(pCodec->imd_info.imd_buff_size,
				32, &pCodec->imd_info.imd_buff_addr);
	logd("imd_info.imd_buff_addr = %lx\n",
	       pCodec->imd_info.imd_buff_addr);
	if (!vaddr)
		return -1;
	pCodec->imd_info.imd_buff_mode = MCVDEC_MODE_NOMAL;

        pCodec->ir_info.ir_info_size = ir_info_size_calc(
                       shvpu_avcdec_Private->maxVideoParameters.eVPU5AVCLevel);

	pCodec->ir_info.ir_info_addr =
		pmem_alloc(pCodec->ir_info.ir_info_size, 32, &paddr);
	logd("ir_info.ir_info_addr = %lx\n", pCodec->ir_info.ir_info_addr);
	if (!pCodec->ir_info.ir_info_addr)
		return -1;

        pCodec->mv_info.mv_info_size = mv_info_size_calc(
                       shvpu_avcdec_Private->maxVideoParameters.nWidth,
                       shvpu_avcdec_Private->maxVideoParameters.nHeight,
                       MAX_REF_FRAME_CNT);

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
	if (shvpu_avcdec_Private->enable_sync)
		pCodec->codecMode = MCVDEC_MODE_SYNC;
	else
		pCodec->codecMode = MCVDEC_MODE_BUFFERING;
	pCodec->pSIQueue = calloc(1, sizeof(queue_t));
	shvpu_queue_init(pCodec->pSIQueue);
	pCodec->pBMIQueue = calloc(1, sizeof(queue_t));
	shvpu_queue_init(pCodec->pBMIQueue);
	pCodec->enoughHeaders = pCodec->enoughPreprocess = OMX_FALSE;
	pContext->user_info = (void *)shvpu_avcdec_Private;
	shvpu_avcdec_Private->avCodec = pCodec;
	shvpu_avcdec_Private->avCodecContext = pContext;

	return ret;
}

void
decode_deinit(shvpu_avcdec_PrivateType *shvpu_avcdec_Private) {
	buffer_metainfo_t *pBMI;
	noVPUInstance--;
	if (shvpu_avcdec_Private ) {
		shvpu_codec_t *pCodec = shvpu_avcdec_Private->avCodec;
		decode_finalize(shvpu_avcdec_Private->avCodecContext);
		if (shvpu_avcdec_Private->intrinsic)
			free(shvpu_avcdec_Private->intrinsic[0]);
		if (shvpu_avcdec_Private->avCodec && 
			shvpu_avcdec_Private->avCodec->fmem) {
			int i, bufs = shvpu_avcdec_Private->avCodec->fmem_size;

			MCVDEC_FMEM_INFO_T *outbuf = shvpu_avcdec_Private->avCodec->fmem;
			for (i = 0 ; i < bufs; i++) {
				phys_pmem_free(outbuf->Ypic_addr, (outbuf->Ypic_bot_addr - outbuf->Ypic_addr) *2 );
				phys_pmem_free(outbuf->Cpic_addr, (outbuf->Cpic_bot_addr - outbuf->Cpic_addr) * 2);
				outbuf++;
			}
			free(shvpu_avcdec_Private->avCodec->fmem);
		}

		phys_pmem_free(pCodec->mv_info.mv_info_addr,
			pCodec->mv_info.mv_info_size);
		pmem_free(pCodec->ir_info.ir_info_addr,
			pCodec->ir_info.ir_info_size);
		phys_pmem_free(pCodec->imd_info.imd_buff_addr,
			pCodec->imd_info.imd_buff_size);
		phys_pmem_free(pCodec->fw.ce_firmware_addr,
			pCodec->fw_size.ce_firmware_size);
		phys_pmem_free(pCodec->fw.vlc_firmware_addr,
			pCodec->fw_size.vlc_firmware_size);

		free_remaining_streams(pCodec->pSIQueue);
		free(pCodec->pSIQueue);

		while (pCodec->pBMIQueue->nelem > 0) {
			pBMI = shvpu_dequeue(pCodec->pBMIQueue);
			free(pBMI);
		}
		free(pCodec->pBMIQueue);
		free(pCodec);
		shvpu_avcdec_Private->avCodec = NULL;
	}
}
int
decode_prepare(void *context)
{
	long ret, hdr_ready;
	MCVDEC_CMN_PICINFO_T *pic_info;
	
	/* fill buffers */
	do {
		hdr_ready = MCVDEC_ON;
		logd("----- invoke mcvdec_decode_picture() -----\n");
		ret = mcvdec_decode_picture(context,
					    &pic_info,
					    MCVDEC_MODE_BUFFERING,
					    &hdr_ready);
		logd("----- resume from mcvdec_decode_picture() -----\n");
		logd("hdr_ready = %s\n", (hdr_ready == MCVDEC_ON) ?
		       "MCVDEC_ON" : "MCVDEC_OFF");

	} while ((ret == MCVDEC_NML_END) &&
		 ((hdr_ready != MCVDEC_ON) || (ibuf_ready != 1)));

	/* error check */
	switch (ret) {
	case MCVDEC_CAUTION:
		logd("%s: warning\n", __FUNCTION__);
	case MCVDEC_NML_END:
		break;
	default:
		logd("%s: error %ld\n", __FUNCTION__, ret);
		return -1;
	}

	return 0;
}

static int
show_error(void *context)
{
	MCVDEC_ERROR_INFO_T errinfo;
	int ret;

	ret = mcvdec_get_error_info(context, &errinfo);

	loge("mcvdec_get_error_info() = %d\n", ret);
	loge("errinfo.dec_status = %ld\n", errinfo.dec_status);
	loge("errinfo.refs_status = %ld\n", errinfo.refs_status);
	loge("errinfo.hdr_err_erc = %ld\n", errinfo.hdr_err_erc);
	loge("errinfo.hdr_err_elvl = %ld\n", errinfo.hdr_err_elvl);
	loge("errinfo.hdr_err_strm_idx = %ld\n", errinfo.hdr_err_strm_idx);
	loge("errinfo.hdr_err_strm_ofs = %ld\n", errinfo.hdr_err_strm_ofs);
	loge("errinfo.vlc_err_esrc = %lx\n", errinfo.vlc_err_esrc);
	loge("errinfo.vlc_err_elvl = %lx\n", errinfo.vlc_err_elvl);
	loge("errinfo.vlc_err_sn = %lx\n", errinfo.vlc_err_sn);
	loge("errinfo.vlc_err_mbh = %lx\n", errinfo.vlc_err_mbh);
	loge("errinfo.vlc_err_mbv = %lx\n", errinfo.vlc_err_mbv);
	loge("errinfo.vlc_err_erc = %lx\n", errinfo.vlc_err_erc);
	loge("errinfo.vlc_err_sbcv = %lx\n", errinfo.vlc_err_sbcv);
	loge("errinfo.ce_err_erc = %lx\n", errinfo.ce_err_erc);
	loge("errinfo.ce_err_epy = %lx\n", errinfo.ce_err_epy);
	loge("errinfo.ce_err_epx = %lx\n", errinfo.ce_err_epx);

	return ret;
}

int
decode_main(void *context, int fd)
{
	MCVDEC_CMN_PICINFO_T *pic_info, *pic_infos[2];
	MCVDEC_FMEM_INFO_T *frame;
	long reti, reto, hdr_ready;
	size_t pic_size;
	void *vaddr;
	int last_decoded_frame = -1;

	/* fill buffers */
	do {
		logd("----- invoke mcvdec_decode_picture() -----\n");
		reti = mcvdec_decode_picture(context,
					    &pic_info,
					    MCVDEC_MODE_MAIN,
					    &hdr_ready);
		logd("----- resume from mcvdec_decode_picture() = %d "
		       "-----\n", reti);
		switch (reti) {
		case MCVDEC_CAUTION:
		case MCVDEC_CONCEALED_1:
		case MCVDEC_CONCEALED_2:
			printf("an error(%d) recoverd.\n", reti);
			show_error(context);
			break;
		case MCVDEC_UNSUPPORT:
		case MCVDEC_ERR_STRM:
			show_error(context);
		default:
			printf("terminating because of an error(%d)\n",
			       reti);
			printf("%d decoded frame(s)\n",
			       last_decoded_frame + 1);
			close(fd);
			return reti;
		case MCVDEC_NO_STRM:
		case MCVDEC_INPUT_END:
			printf("nothing to decode (%d)\n", reti);
		case MCVDEC_NML_END:
			break;
		}
		logd("----- invoke mcvdec_get_output_picture() -----\n");
		reto = mcvdec_get_output_picture(context,
						pic_infos, &frame,
						MCVDEC_OUTMODE_PUSH);
		logd("----- resume from mcvdec_get_output_picture() = %d "
		       "-----\n", reto);
		if (pic_infos[0]) {
			int i;
			logd("pic_infos[0]->frame_cnt = %d\n",
			       pic_infos[0]->frame_cnt);
			logd("pic_infos[0]->fmem_index = %d\n",
			       pic_infos[0]->fmem_index);
			logd("pic_infos[0]->strm_id = %d\n",
			       pic_infos[0]->strm_id);
			last_decoded_frame = pic_infos[0]->strm_id;
			logd("pic_infos[0]->xpic_size = %d\n",
			       pic_infos[0]->xpic_size);
			logd("pic_infos[0]->ypic_size = %d\n",
			       pic_infos[0]->ypic_size);
			for (i=0; i<4; i++)
				logd("pic_infos[0]->frame_crop[%d] = %d\n",
				       i, pic_infos[0]->frame_crop[i]);
		}
		if (frame) {
			logd("frame->Ypic_addr = %lx\n",
			       frame->Ypic_addr);
			logd("frame->Cpic_addr = %lx\n",
			       frame->Cpic_addr);
		}

		/* output Y/C picture data into a file */
		if (reto == MCVDEC_NML_END) {
			pic_size = pic_infos[0]->xpic_size *
				(pic_infos[0]->ypic_size -
				 pic_infos[0]->frame_crop[MCVDEC_CROP_BOTTOM]);
			vaddr = uio_phys_to_virt(frame->Ypic_addr);
			write(fd, vaddr, pic_size);
			vaddr = uio_phys_to_virt(frame->Cpic_addr);
			write(fd, vaddr, pic_size / 2);
		} else {
			printf("mcvdec_get_output_picture() = %d\n", reto);
		}

	} while (reti == MCVDEC_NML_END);

	return 0;
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
