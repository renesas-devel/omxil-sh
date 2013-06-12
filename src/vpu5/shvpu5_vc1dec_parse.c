/**
   src/vpu5/shvpu5_vc1dec_parse.c

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

#include <sys/types.h>
#include <OMX_Types.h>
#include <OMX_Core.h>
#include "mcvdec.h"
#include "shvpu5_decode_omx.h"
#include "shvpu5_common_queue.h"
#include "shvpu5_parse_api.h"
#include "shvpu5_common_log.h"
#include <vc1dec.h>

typedef enum {
	PROFILE_SIMPLE,
	PROFILE_MAIN,
	PROFILE_RESERVED,
	PROFILE_ADVANCED,
} vc1_profile;


struct vc1parse_meta {
	int		first_frame;
	vc1_profile	profile;
};

static buffer_avcdec_metainfo_t
save_omx_buffer_metainfo(OMX_BUFFERHEADERTYPE *pBuffer)
{
	buffer_avcdec_metainfo_t buffer_meta;

	buffer_meta.hMarkTargetComponent = pBuffer->hMarkTargetComponent;
	pBuffer->hMarkTargetComponent = NULL;
	logd("%s: hMarkTargetComponent = %p\n",
	     __FUNCTION__, buffer_meta.hMarkTargetComponent);
	buffer_meta.pMarkData = pBuffer->pMarkData;
	pBuffer->pMarkData = NULL;
	logd("%s: pMarkData = %p\n", __FUNCTION__, buffer_meta.pMarkData);
	buffer_meta.nTimeStamp = pBuffer->nTimeStamp;
	pBuffer->nTimeStamp = 0;
	logd("%s: nTimeStamp = %d\n", __FUNCTION__, buffer_meta.nTimeStamp);
	buffer_meta.nFlags = pBuffer->nFlags &
		(OMX_BUFFERFLAG_STARTTIME | OMX_BUFFERFLAG_DECODEONLY);
	pBuffer->nFlags = 0;
	logd("%s: nFlags = %08x\n", __FUNCTION__, buffer_meta.nFlags);

	return buffer_meta;
}

static int
reinit_spmp_mode(shvpu_decode_PrivateType *shvpu_decode_Private,
			VC1DEC_SEQ_HDR_SPMP_SYNTAX_T *seq_hdr_spmp) { 
	int ret;
	shvpu_decode_codec_t *pCodec = shvpu_decode_Private->avCodec;
	VC1DEC_PARAMS_T *vc1dec_params = pCodec->vpu_codec_params.codec_params;
	/* deinit */
	decode_finalize(shvpu_decode_Private->avCodecContext);
	phys_pmem_free(pCodec->fw.vlc_firmware_addr,
			pCodec->fw_size.vlc_firmware_size);

	/* make changes for SPMP mode */
	pCodec->fw.vlc_firmware_addr =
		shvpu5_load_firmware(VPU5HG_FIRMWARE_PATH "/svc1d.bin",
			&pCodec->fw_size.vlc_firmware_size);
	
	vc1dec_params->input_stream_format = VC1DEC_INPUT_SPMP_IDX;
	vc1dec_params->seq_header_spmp = *seq_hdr_spmp;

	/*reinitialize VPU */
	ret = mcvdec_init_decoder(pCodec->vpu_codec_params.api_tbl,
				  &pCodec->cprop,
				  &pCodec->wbuf_dec,
				  &pCodec->fw, shvpu_decode_Private->intrinsic,
				  pCodec->pDriver->pDrvInfo,
				  &shvpu_decode_Private->avCodecContext);
	if (ret) {
		loge("ERROR: SPMP init: mcvdec_init_decoder (%d)\n", ret);
		return ret;
	}

	ret = mcvdec_set_vpu5_work_area(shvpu_decode_Private->avCodecContext,
					&pCodec->imd_info,
					&pCodec->ir_info,
					&pCodec->mv_info);
	if (ret) {
		loge("ERROR: SPMP init: mcvdec_set_work_area(%d)\n", ret);
		return ret;
	}

	ret = mcvdec_set_play_mode(shvpu_decode_Private->avCodecContext,
						MCVDEC_PLAY_FORWARD, 0, 0);
	if (ret) {
		loge("ERROR: SPMP init: mcvdec_set_play_mode(%d)\n", ret);
		return ret;
	}
	shvpu_decode_Private->avCodecContext->user_info =
					(void *)shvpu_decode_Private;
	return 0;
}

static void *
alloc_phys_buffer(int *size) {
	int lsize = *size;
	lsize = (lsize + 0x200 + 0x600 + 255) / 256;
	if ((lsize % 2) == 0)
		lsize++;
	lsize *= 0x200;
	*size = lsize;
	return pmem_alloc(lsize, 256, NULL);
}

static int
alloc_picture_buffer(pic_t *pPic, unsigned int size, phys_input_buf_t *oldBuf) {
	int full_size = size;
	static int resize_cnt = 0;
	phys_input_buf_t *pBuf;
	pBuf = calloc(1, sizeof(*pBuf));
	if (pBuf == NULL)
		return -1;
	memset(pBuf, 0, sizeof(*pBuf));
	pBuf->base_addr = alloc_phys_buffer(&full_size);
	if (!pBuf->base_addr)
		return -1;
	pBuf->size = full_size;
	pBuf->buf_offsets[0] = (unsigned char *)pBuf->base_addr + 256;
	pBuf->n_sbufs = 1;
	pPic->pBufs[0] = pBuf;
	pPic->n_bufs = 1;
	pPic->n_sbufs = 1;
	if (!oldBuf)
		return 0;

	if (size <= oldBuf->buf_sizes[0]) {
		loge("%s: realloc buffer size too small. req = %d, given = %d\n",
			__FUNCTION__, oldBuf->buf_sizes[0], size);
		return -1;
	}

	logd("Resized %d input data buffers. May cause reduced performance",
			resize_cnt++);

	memcpy(pBuf->buf_offsets[0], oldBuf->buf_offsets[0],
		oldBuf->buf_sizes[0]);
	pmem_free(oldBuf->base_addr, oldBuf->size);
	free(oldBuf);
	return 0;
}

static int
expand_picture_buffer(pic_t *pPic, int size) {
	return alloc_picture_buffer(pPic, size, pPic->pBufs[0]);
}

static int
init_picture_buffer(pic_t *pPic, int size) {
	return alloc_picture_buffer(pPic, size, NULL);
}

static const unsigned char vc1_eop_code[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0D
};

static int
copyBufData(pic_t *pPic,
		OMX_BUFFERHEADERTYPE *pBuffer,
		int len, int prefix_len,
		int trunc, int add_eop) {
	phys_input_buf_t *pBuf;
	unsigned char *buf;
	unsigned int *size;
	static const char start_code [] = { 0, 0, 1 };

	pBuf = pPic->pBufs[0];

	buf = (unsigned char *)pBuf->buf_offsets[0];
	size = &pBuf->buf_sizes[0];

	if (trunc) {
		*size -= trunc;
		pPic->size -= trunc;
		return -1;
	}

	if (prefix_len) {
		memcpy(buf + *size, start_code + (3 - prefix_len), prefix_len);
		*size += (3 - prefix_len);
	}
	if (!len)
		return pPic->size;
	memcpy(buf + *size, pBuffer->pBuffer + pBuffer->nOffset, len);
	*size += len;
	if (add_eop) {
		memcpy(buf + *size, &vc1_eop_code, 8);
		*size += 8;
	}
	pBuffer->nFilledLen -= len;
	pBuffer->nOffset += len;
	pPic->size += *size;
	return pPic->size;
}

static OMX_BOOL
parseVc1Buffer(shvpu_decode_PrivateType *shvpu_decode_Private,
	    OMX_BUFFERHEADERTYPE *pBuffer,
	    OMX_BOOL		 eos,
	    pic_t		 *pActivePic,
	    OMX_BOOL 		 *pIsInBufferNeeded) {

	unsigned char *pHead, *pStart;
	size_t nRemainSize;
	unsigned int parsed;
	int total_parsed;
	int start_code;
	int bufsize;
	long ret;
	int add_special_code = 0;
	VC1DEC_SEQ_HDR_SPMP_SYNTAX_T seq_hdr_spmp;
	buffer_avcdec_metainfo_t buffer_meta;
	struct vc1parse_meta *vc1parse;
	shvpu_decode_codec_t *pCodec = shvpu_decode_Private->avCodec;
	vc1parse = pCodec->codec_priv;

	pStart = pBuffer->pBuffer + pBuffer->nOffset;
	nRemainSize = pBuffer->nFilledLen;

	bufsize = pBuffer->nFilledLen;

	if (!bufsize)
		goto out;

	if(pBuffer->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
		vc1parse->first_frame = 0;
		ret = vc1dec_get_seq_header_spmp(pStart, &seq_hdr_spmp);
		logd ("vc1dec_get_seq_header_spmp ret = %d\n", ret);
		if (ret == VC1DEC_RTN_NORMAL || ret == VC1DEC_RTN_WARN) {
			vc1parse->profile = seq_hdr_spmp.profile >> 2;	
			if (vc1parse->profile != PROFILE_ADVANCED) {
				reinit_spmp_mode(shvpu_decode_Private,
								&seq_hdr_spmp);
			}
			logd("profile = %d\n", seq_hdr_spmp.profile);
		}
	}

/* just copy in the whole buffer as is for now */
	if (vc1parse->profile == PROFILE_ADVANCED) {
		bufsize += 8;
		add_special_code = 1;
	};

	init_picture_buffer(pActivePic, bufsize);
	buffer_meta = save_omx_buffer_metainfo(pBuffer);
	pActivePic->buffer_meta = buffer_meta;
	pActivePic->has_meta = 1;
	copyBufData(pActivePic, pBuffer, pBuffer->nFilledLen, 0, 0,
		add_special_code);
	pActivePic->hasSlice = 1;

out:
	*pIsInBufferNeeded = !eos;
	return OMX_TRUE;
}

void
flushVc1Parser(shvpu_decode_PrivateType *shvpu_decode_Private) {
}

void
deinitVc1Parser(shvpu_decode_PrivateType *shvpu_decode_Private) {
	struct vc1parse_meta *vc1parse;
	shvpu_decode_codec_t *pCodec = shvpu_decode_Private->avCodec;
	vc1parse = pCodec->codec_priv;
	free(vc1parse);
}

static const unsigned char vc1_data_eos[] = {
	0x00, 0x00, 0x01, 0x0a,
};

static struct input_parse_ops vc1_parse_ops = {
	.parseBuffer = parseVc1Buffer,
	.parserFlush = flushVc1Parser,
	.parserDeinit = deinitVc1Parser,
	.EOSCode = vc1_data_eos,
	.EOSCodeLen = sizeof(vc1_data_eos),
};

int
initVc1Parser(shvpu_decode_PrivateType *shvpu_decode_Private) {

	struct vc1parse_meta *vc1parse;
	shvpu_decode_codec_t *pCodec = shvpu_decode_Private->avCodec;
	vc1parse = pCodec->codec_priv =
		calloc(1, sizeof(struct vc1parse_meta));
	vc1parse->first_frame = 1;
	vc1parse->profile = PROFILE_ADVANCED;
	/* set mode depending on AP or SPMP mode */
	pCodec->pops = &vc1_parse_ops;
	return 0;
}
