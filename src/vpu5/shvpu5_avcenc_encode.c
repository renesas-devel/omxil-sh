/**
   src/vpu5/shvpu5_avcenc_encode.c

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
#include <string.h>
#include "mciph.h"
#include "mciph_hg.h"
#include "shvpu5_avcenc.h"
#include "avcenc.h"

static inline void *
malloc_aligned(size_t size, int align)
{
	return malloc(size);
}

static unsigned long
load_fw(char *filename)
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

	/* malloc() on 32bit environment must allocate
	   an 8-bytes aligned region. */

static inline int
alloc_fmem(int width, int height, MCVENC_FMEM_INFO_T *fmem)
{
	int mem_x, mem_y;
	void *Ypic_vaddr, *Cpic_vaddr;

	mem_x = ((width + 15) / 16) * 16;
	mem_y = ((height + 15) / 16) * 16;
	Ypic_vaddr = pmem_alloc(mem_x * mem_y, 32, &fmem->Ypic_addr);
	if (Ypic_vaddr == NULL)
		return -1;
	Cpic_vaddr = pmem_alloc(mem_x * mem_y / 2, 32, &fmem->Cpic_addr);
	if (Cpic_vaddr == NULL) {
		pmem_free(Ypic_vaddr, mem_x * mem_y);
		return -1;
	}

	return 0;
}

static inline void
free_fmem(int width, int height, MCVENC_FMEM_INFO_T *fmem)
{
	int mem_x, mem_y;
	void *Ypic_vaddr, *Cpic_vaddr;

	mem_x = ((width + 15) / 16) * 16;
	mem_y = ((height + 15) / 16) * 16;
	if (fmem->Ypic_addr) {
		Ypic_vaddr = uio_phys_to_virt(fmem->Ypic_addr);
		if (Ypic_vaddr)
			pmem_free(Ypic_vaddr, mem_x * mem_y);
	}
	if (fmem->Cpic_addr) {
		Cpic_vaddr = uio_phys_to_virt(fmem->Cpic_addr);
		if (Cpic_vaddr)
			pmem_free(Cpic_vaddr, mem_x * mem_y / 2);
	}

	return;
}


shvpu_codec_t *
encode_new()
{
        extern unsigned long uio_virt_to_phys(void *, long, unsigned long);
	shvpu_codec_t *pCodec;

	/* allocate */
	pCodec = (shvpu_codec_t *)calloc(1, sizeof(shvpu_codec_t));
	if (!pCodec)
		return NULL;
	memset((void *)pCodec, 0, sizeof(shvpu_codec_t));

	/* initialize const MCVENC_CMN_PROPERTY_T parameters */
	pCodec->cmnProp.stream_type = MCVENC_H264;
	pCodec->cmnProp.framerate_tick = 1;
	pCodec->cmnProp.interlace_mode = MCVENC_PROGRESSIVE;
	pCodec->cmnProp.stream_struct = MCVENC_FRAME_STRUCTURE;
	pCodec->cmnProp.fmem_alloc_mode	= MCVENC_ALLOC_FRAME;
	pCodec->cmnProp.field_ref_mode = MCVENC_FREF1_AUTO;
	pCodec->cmnProp.ce_config = MCVENC_2CE;
	pCodec->cmnProp.virt_to_phys_func = uio_virt_to_phys;
	pCodec->cmnProp.max_GOP_length = 30;
	pCodec->cmnProp.B_pic_mode = 0;
	pCodec->cmnProp.num_ref_frames = 1;

	/* initialize the (non-zero) default AVCENC_OPTION_T parameters */
	pCodec->avcOpt.start_code_mode = AVCENC_ON;
	pCodec->avcOpt.hrc_mode = AVCENC_OFF;
	/* MEMO: cbp_size and cbp_remain never set. */
	pCodec->avcOpt.sps_profile_idc = AVCENC_BASELINE;
	pCodec->avcOpt.sps_constraint_set0_flag = AVCENC_ON;
	pCodec->avcOpt.sps_constraint_set1_flag = AVCENC_ON;
	pCodec->avcOpt.sps_constraint_set2_flag = AVCENC_ON;
	pCodec->avcOpt.sps_constraint_set3_flag = AVCENC_OFF;
	pCodec->avcOpt.sps_level_idc = 10;
	pCodec->avcOpt.sps_pic_order_cnt_type =	AVCENC_POC_TYPE_2;
	pCodec->avcOpt.sps_gaps_in_frame_num_value_allowed_flag = AVCENC_OFF;
	pCodec->avcOpt.pps_cabac_mode = AVCENC_CAVLC;
	pCodec->avcOpt.pps_pic_init_qp_minus26 = AVCENC_REF_CMN_QP;
	pCodec->avcOpt.pps_transform_8x8_mode_flag = AVCENC_OFF;
	pCodec->avcOpt.slh_slice_type_mode = AVCENC_SLICE_TYPE_A;
	pCodec->avcOpt.slh_disable_deblocking_filter_idc = AVCENC_DBF_MODE_0;

	return pCodec;
}

long
encode_init(shvpu_codec_t *pCodec)
{
	MCVENC_CONTEXT_T *pContext;
	long ret;
	extern const MCIPH_API_T mciph_hg_api_tbl;
	extern const MCVENC_API_T avcenc_api_tbl;
	MCVENC_CMN_PROPERTY_T *pCmnProp = &pCodec->cmnProp;
        AVCENC_OPTION_T	*pAvcOpt = &pCodec->avcOpt;

	/*** initialize vpu ***/
	pCodec->wbufVpu5.work_size = MCIPH_HG_WORKAREA_SIZE;
	pCodec->wbufVpu5.work_area_addr =
		malloc_aligned(pCodec->wbufVpu5.work_size, 4);
	logd("work_area_addr = %p\n", pCodec->wbufVpu5.work_area_addr);
	if ((pCodec->wbufVpu5.work_area_addr == NULL) ||
	    ((unsigned int)pCodec->wbufVpu5.work_area_addr & 0x03U)) {
		ret = -1L;
		goto init_failed;
	}

	pCodec->vpu5Init.vpu_base_address		= 0xfe900000;
	pCodec->vpu5Init.vpu_image_endian		= MCIPH_LIT;
	pCodec->vpu5Init.vpu_stream_endian		= MCIPH_LIT;
	pCodec->vpu5Init.vpu_firmware_endian	= MCIPH_LIT;
	pCodec->vpu5Init.vpu_interrupt_enable	= MCIPH_ON;
	pCodec->vpu5Init.vpu_clock_supply_control	= MCIPH_CLK_CTRL;
	pCodec->vpu5Init.vpu_constrained_mode	= MCIPH_OFF;
	pCodec->vpu5Init.vpu_address_mode		= MCIPH_ADDR_32BIT;
	pCodec->vpu5Init.vpu_reset_mode		= MCIPH_RESET_SOFT;
	logd("----- invoke mciph_vpu5Init() -----\n");
	ret = mciph_vpu5_init(&(pCodec->wbufVpu5),
			      (MCIPH_API_T *)&mciph_hg_api_tbl,
			      &(pCodec->vpu5Init),
			      &(pCodec->pDrvInfo));
	logd("----- resume from mciph_vpu5_init() -----\n");
	if (ret != MCIPH_NML_END)
		goto init_failed;

	/*** initialize encoder ***/
	extern unsigned long uio_virt_to_phys(void *, long, unsigned long);
	static MCVENC_WORK_INFO_T wbuf_enc = {
		.work_area_size = 0x5800,  /* 20 + 2KiB */
	};
	static MCVENC_FIRMWARE_INFO_T fw;
	wbuf_enc.work_area_addr = malloc_aligned(wbuf_enc.work_area_size, 4);
	logd("work_area_addr = %p\n", wbuf_enc.work_area_addr);
	fw.ce_firmware_addr = load_fw(VPU5HG_FIRMWARE_PATH "/p264e_h.bin");
	logd("ce_firmware_addr = %lx\n", fw.ce_firmware_addr);
	fw.vlc_firmware_addr = load_fw(VPU5HG_FIRMWARE_PATH "/s264e.bin");
	logd("vlc_firmware_addr = %lx\n", fw.vlc_firmware_addr);
	logd("----- invoke mcvenc_init_encoder() -----\n");
	ret = mcvenc_init_encoder((MCVENC_API_T *)&avcenc_api_tbl,
				  pCmnProp, &wbuf_enc,
				  &fw, pCodec->pDrvInfo,
				  &pContext);
	logd("----- resume from mcvenc_init_encoder() -----\n");
	if (ret != MCIPH_NML_END)
		return ret;

	pContext->user_info = (void *)pCodec;
	logd("drv_info = %p\n", pCodec->pDrvInfo);
	pCodec->pContext = pContext;

	/*** initialize work area ***/
	void *vaddr;
	unsigned long paddr;
	static MCVENC_IMD_INFO_T imd_info;
	static MCVENC_LDEC_INFO_T ldec_info;
	static MCVENC_IR_INFO_T ir_info;
	static MCVENC_MV_INFO_T mv_info;
	size_t a, b, mem_x, mem_y, mb_width, mb_height, mv_info_size;
	int i;

	a = 4 * pCmnProp->bitrate / 8;
	b = pCmnProp->x_pic_size * pCmnProp->y_pic_size * 4;
	imd_info.imd_buff_size = (a > b) ? a : b;
	imd_info.imd_buff_size =
		((imd_info.imd_buff_size + 2047) / 2048) * 2048;
	vaddr = pmem_alloc(imd_info.imd_buff_size,
			   32, &imd_info.imd_buff_addr);
	logd("imd_info.imd_buff_addr = %lx\n",
	       imd_info.imd_buff_addr);
	if (!vaddr)
		return -1;
	ldec_info.ldec_num = 3;
	for (i=0; i<ldec_info.ldec_num; i++) {
		ret = alloc_fmem(pCmnProp->x_pic_size,
				 pCmnProp->y_pic_size,
				 &ldec_info.fmem[i][0]);
		if (ret < 0)
			return -1;
		ldec_info.fmem[i][1].Ypic_addr = 0U;
		ldec_info.fmem[i][1].Cpic_addr = 0U;
	}

	ir_info.ir_info_size = 4800;
	ir_info.ir_info_addr = (unsigned long)
		pmem_alloc(ir_info.ir_info_size, 32, &paddr);
	logd("ir_info.ir_info_addr = %lx\n", ir_info.ir_info_addr);
	if (!ir_info.ir_info_addr)
		return -1;
	mb_width = (pCmnProp->x_pic_size + 15) / 16;
	mb_height = (pCmnProp->y_pic_size + 15) / 16;
	mv_info_size = ((16 * mb_width *
			 ((mb_height + 1) / 2) + 255) / 256) * 256;
	vaddr = pmem_alloc(mv_info_size, 32, &mv_info.mv_info_addr[0]);
	logd("mv_info.mv_info_addr[0] = %lx\n", mv_info.mv_info_addr[0]);
	if (!vaddr)
		return -1;
	vaddr = pmem_alloc(mv_info_size, 32, &mv_info.mv_info_addr[1]);
	logd("mv_info.mv_info_addr[1] = %lx\n", mv_info.mv_info_addr[1]);
	if (!vaddr)
		return -1;
	mv_info.mv_info_addr[2] = 0;
	mv_info.mv_info_addr[3] = 0;
	if (!vaddr)
		return -1;

	logd("----- invoke mcvenc_set_vpu5_work_area() -----\n");
	ret = mcvenc_set_vpu5_work_area(pContext, &imd_info,
					&ldec_info, &ir_info, &mv_info);
	logd("----- resume from mcvenc_set_vpu5_work_area() -----\n");
	if (ret != MCIPH_NML_END)
		return ret;

#if 0
	/*** set common option ***/
	logd("----- invoke mcvenc_set_option() -----\n");
	ret = mcvenc_set_option(pContext, MCVDEC_PLAY_FORWARD, 0, 0);
	logd("----- resume from mcvenc_set_option() -----\n");
#endif

	/*** set avc specific option ***/
	if (pCodec->avcOptSet) {
		logd("----- invoke avcdec_set_option() -----\n");
		ret = avcenc_set_option(pContext, pAvcOpt,
					pCodec->avcOptSet);
		logd("----- resume from avcdec_set_option() = %d -----\n",
		     ret);
	}

#if 0
	/*** set VUI ***/
	ret = avcenc_set_VUI();

	/*** set Q matrix ***/
	ret = avcenc_set_Q_matrix();
#endif

init_failed:
	return ret;
}

int
encode_set_profile(shvpu_codec_t *pCodec, int profile_id)
{
        AVCENC_OPTION_T	*pAvcOpt = &pCodec->avcOpt;

	switch (profile_id) {
	case 66:
		pAvcOpt->sps_profile_idc = AVCENC_BASELINE;
		pAvcOpt->sps_pic_order_cnt_type = AVCENC_POC_TYPE_2;
		pAvcOpt->sps_constraint_set0_flag = AVCENC_ON;
		pAvcOpt->sps_constraint_set1_flag = AVCENC_ON;
		pAvcOpt->sps_constraint_set2_flag = AVCENC_ON;
		break;
	case 77:
		pAvcOpt->sps_profile_idc = AVCENC_MAIN;
		pAvcOpt->sps_pic_order_cnt_type = AVCENC_POC_TYPE_0;
		pAvcOpt->pps_cabac_mode = AVCENC_CABAC_INIT_IDC_0;
		pAvcOpt->sps_constraint_set0_flag = AVCENC_OFF;
		pAvcOpt->sps_constraint_set1_flag = AVCENC_ON;
		pAvcOpt->sps_constraint_set2_flag = AVCENC_OFF;
		break;
	case 100:
		pAvcOpt->sps_profile_idc = AVCENC_HIGH;
		pAvcOpt->sps_pic_order_cnt_type = AVCENC_POC_TYPE_0;
		pAvcOpt->pps_cabac_mode = AVCENC_CABAC_INIT_IDC_0;
		pAvcOpt->sps_constraint_set0_flag = AVCENC_OFF;
		pAvcOpt->sps_constraint_set1_flag = AVCENC_OFF;
		pAvcOpt->sps_constraint_set2_flag = AVCENC_OFF;
		break;
	default:
		return -1;
	}

	pCodec->avcOptSet |= AVCENC_OPT_SPS | AVCENC_OPT_PPS;

	return 0;
}

int
encode_set_level(shvpu_codec_t *pCodec, int level_id, int is1b)
{
        AVCENC_OPTION_T	*pAvcOpt = &pCodec->avcOpt;

	pAvcOpt->sps_constraint_set3_flag = AVCENC_OFF;
	pAvcOpt->sps_level_idc = level_id;
	if (is1b && (pAvcOpt->sps_profile_idc != AVCENC_HIGH))
		pAvcOpt->sps_constraint_set3_flag = AVCENC_ON;

	pCodec->avcOptSet |= AVCENC_OPT_SPS;

	return 0;
}

int
encode_set_options(shvpu_codec_t *pCodec, int num_ref_frames,
		   int max_GOP_length, int num_b_frames,
		   int isCABAC, int cabac_init_idc)
{
	MCVENC_CMN_PROPERTY_T *pCprop = &pCodec->cmnProp;
	AVCENC_OPTION_T *pAvcOpt = &pCodec->avcOpt;

	//nSliceHeaderSpacing

	if ((num_ref_frames != 1) && (num_ref_frames != 2))
		return -1;
	pCprop->num_ref_frames = num_ref_frames;

	if ((num_b_frames == 0) && (max_GOP_length < 10)) {
		pCprop->max_GOP_length = 0;
	} else if ((max_GOP_length < 10) || (max_GOP_length > 120)) {
		return -1;
	} else {
		pCprop->max_GOP_length = max_GOP_length;
	}

	if (num_b_frames < 0)
		return -1;
	pCprop->B_pic_mode =
		(num_b_frames > 2) ? MCVENC_ADP_B_INS : num_b_frames;

	if (isCABAC) {
		switch (cabac_init_idc) {
		case 0:
			pAvcOpt->pps_cabac_mode = AVCENC_CABAC_INIT_IDC_0;
			break;
		case 1:
			pAvcOpt->pps_cabac_mode = AVCENC_CABAC_INIT_IDC_1;
			break;
		case 2:
			pAvcOpt->pps_cabac_mode = AVCENC_CABAC_INIT_IDC_2;
			break;
		default:
			return -1;
		}
	} else {
		pAvcOpt->pps_cabac_mode = AVCENC_CAVLC;
	}
	pCodec->avcOptSet |= AVCENC_OPT_PPS;


/*	bEnableUEP - unequal protection
	bEnableFMO - flexible macroblock ordering - not supported
	bEnableASO - arbitrary slice ordering - not supported
	bEnableRS - redundant slices - not supported*/


	/*nAllowedPictureTypes - allowed picture types (whazzat)?
	  bFrameMBsOnly - frames contain ONLY macroblocks
	  bEntropyCodingCABAC - entropy decoding
	  bconstIpred - intra-prediction
	  bDirect8x8Interface - derivation of luma motions vectors
	  bDirectSpacialTemporal - spacial or temporal mode in B-slice coding
	  nCabacInitIdx - init CABAC
	  eLoopFilterMode - AVC loop filter*/

	return 0;
}

int
encode_set_bitrate(shvpu_codec_t *pCodec, int bitrate, char mode)
{
	switch (mode) {
	case 'v':
		pCodec->cmnProp.rate_control_mode = MCVENC_VBR;
		break;
	case 'c':
		pCodec->cmnProp.rate_control_mode = MCVENC_CBR_NML;
		break;
	case 'S':
		pCodec->cmnProp.rate_control_mode= MCVENC_CBR_SKIP;
		//TODO: check the limitations for B pic and
		//stream_struct
		break;
	case ' ':
		/* not set */
		break;
	default:
		loge ("Attempt to set unsupported rate control\n");
		return -1;
	}

	pCodec->cmnProp.bitrate = bitrate;

	return 0;
}

int
encode_set_propaties(shvpu_codec_t *pCodec, int width, int height,
		     int framerate, int bitrate, char ratecontrol)
{
	pCodec->cmnProp.x_pic_size = width;
	pCodec->cmnProp.fmem_x_size[MCVENC_FMX_LDEC] =
		pCodec->cmnProp.fmem_x_size[MCVENC_FMX_REF] =
		pCodec->cmnProp.fmem_x_size[MCVENC_FMX_CAPT] = width;
	pCodec->cmnProp.y_pic_size = height;
	pCodec->cmnProp.framerate_resolution = framerate;

	encode_set_bitrate(pCodec, bitrate, ratecontrol);

	return 0;
}

int
encode_header(void *context, unsigned char *pBuffer, size_t nBufferLen)
{
	int ret1, ret2;
	AVCENC_HEADER_BUFF_INFO_T head;

	/* SPS */
	head.buff_size = 384;
	if (nBufferLen < head.buff_size)
		return -1;
	head.buff_addr = pBuffer;
	ret1 = avcenc_put_SPS(context, &head);
	logd("avcenc_put_SPS = %d\n", ret1);
	if (ret1 < 0)
		return ret1;
	pBuffer += ret1;
	nBufferLen -= ret1;
	logd("(SPS)");

	/* PPS */
	head.buff_size = 64;
	if (nBufferLen < head.buff_size)
		return -1;
	head.buff_addr = pBuffer;
	ret2 = avcenc_put_PPS(context, &head);
	logd("avcenc_put_PPS = %d\n", ret2);
	if (ret2 < 0)
		return ret2;
	logd("(PPS)");

	return ret1 + ret2;
}

int
encode_main(MCVENC_CONTEXT_T *pContext, int frameId,
	    unsigned char *pBuffer, int nWidth, int nHeight,
	    void **ppConsumed)
{
	MCVENC_CAPT_INFO_T capt_info;
	MCVENC_FRM_STAT_T frm_stat;
	int memWidth, memHeight, ret;
	size_t yPicSize;

	/* register a buffer */
	capt_info.fmem[0].Ypic_addr =
		uio_virt_to_phys(pContext, MCIPH_ENC,
				 (unsigned long)pBuffer);
	if (capt_info.fmem[0].Ypic_addr == NULL)
		return -1;
	memWidth = ((nWidth + 15) / 16) * 16;
	memHeight = ((nHeight + 15) / 16) * 16;
	yPicSize = memWidth * memHeight;
	capt_info.fmem[0].Cpic_addr =
		capt_info.fmem[0].Ypic_addr + yPicSize;
	capt_info.fmem[1].Ypic_addr = 0U;
	capt_info.fmem[1].Cpic_addr = 0U;
	logd("----- invoke mcvenc_encode_picture() -----\n");
	ret = mcvenc_encode_picture(pContext, frameId, MCVENC_OFF,
				    &capt_info, &frm_stat);
	logd("----- resume from mcvenc_encode_picture() = %d "
	       "-----\n", ret);
	switch (ret) {
	default:
		printf("terminating because of an error(%d)\n",
		       ret);
		break;
	case MCVENC_SKIP_PIC:
		logd("[SKIP]");
	case MCVENC_STORE_PIC:
	case MCVENC_NML_END:
		if (frm_stat.ce_used_capt_frm_id >= 0)
			*ppConsumed = uio_phys_to_virt(frm_stat.capt[0].
						       Ypic_addr);
		break;
	}
	
	return ret;
}

int
encode_endcode(void *context, unsigned char *pBuffer, size_t nBufferLen)
{
	int ret1, ret2;
	AVCENC_HEADER_BUFF_INFO_T end;

	/* EOSeq */
	end.buff_size = 5;
	if (nBufferLen < end.buff_size)
		return -1;
	end.buff_addr = pBuffer;
	ret1 = avcenc_put_end_code(context, AVCENC_OUT_END_OF_SEQ, &end);
	logd("avcenc_put_end_code(EOSeq) = %d\n", ret1);
	if (ret1 < 0)
		return ret1;
	pBuffer += ret1;
	nBufferLen -= ret1;
	loge("(EOSq)");

	/* EOStr */
	end.buff_size = 5;
	if (nBufferLen < end.buff_size)
		return -1;
	end.buff_addr = pBuffer;
	ret2 = avcenc_put_end_code(context, AVCENC_OUT_END_OF_STRM, &end);
	logd("avcenc_put_end_code(EOStr) = %d\n", ret2);
	if (ret2 < 0)
		return ret2;
	loge("(EOSr)");

	return ret1 + ret2;
}

int
encode_finalize(void *context)
{
	int ret;

	logd("----- invoke mcvenc_end_encoder() -----\n");
	ret = mcvenc_end_encoder(context);
	logd("----- resume from mcvenc_end_encoder() -----\n");

	return ret;
}

void
encode_deinit(shvpu_codec_t *pCodec)
{
	free(pCodec->wbufVpu5.work_area_addr);
}
