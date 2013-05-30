/**
   src/vpu5/shvpu5_avcdec_parse.c

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

#include <sys/types.h>
#include <OMX_Types.h>
#include <OMX_Core.h>
#include "mcvdec.h"
#include "shvpu5_decode_omx.h"
#include "shvpu5_common_queue.h"
#include "shvpu5_parse_api.h"
#include "shvpu5_common_log.h"

#define VOP_START_CODE 0xB6
#define BUFFER_SIZE (512 * 1024)

typedef enum {
	START,
	FOUND_0,
	FOUND_00,
	FOUND_001,
} parse_state;

struct m4vparse_meta {
	OMX_BOOL	      foundVOP;
	int		      crossBufStartCode;
	parse_state	      state;
	size_t		      buffer_size;
	size_t		      buffer_pos;
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
find_start_code(unsigned char *buf, size_t len, parse_state *state,
								unsigned int *parsed) {

	unsigned int i;
	for (i = 0; i < len; i++) {
		switch (*state) {
		case START:
			if (buf[i] == 0)
				*state = FOUND_0;
			break;
		case FOUND_0:
			if (buf[i] == 0)
				*state = FOUND_00;
			else
				*state = START;
			break;
		case FOUND_00:
			if (buf[i] == 1)
				*state = FOUND_001;
			else
				*state = START;
			break;
		case FOUND_001:
			*state = START;
			if (parsed)
				*parsed = i + 1;
			return (int) buf[i];
		}
	}
	if (parsed)
		*parsed = len;
	return -1;
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

static int
copyBufData(pic_t *pPic,
		OMX_BUFFERHEADERTYPE *pBuffer,
		int len, int prefix_len,
		int trunc) {
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
	pBuffer->nFilledLen -= len;
	pBuffer->nOffset += len;
	pPic->size += *size;
	return pPic->size;
}

static OMX_BOOL
parseMpegBuffer(shvpu_decode_PrivateType *shvpu_decode_Private,
	    OMX_BUFFERHEADERTYPE *pBuffer,
	    OMX_BOOL		 eos,
	    pic_t		 *pActivePic,
	    OMX_BOOL 		 *pIsInBufferNeeded) {

	struct m4vparse_meta *m4vparse = shvpu_decode_Private->avCodec->codec_priv;
	unsigned char *pHead, *pStart;
	size_t nRemainSize;
	unsigned int parsed;
	int total_parsed;
	int start_code;
	buffer_avcdec_metainfo_t buffer_meta;

	pStart = pBuffer->pBuffer + pBuffer->nOffset;
	nRemainSize = pBuffer->nFilledLen;

	if (!pActivePic->n_bufs)
		init_picture_buffer(pActivePic, BUFFER_SIZE);

	total_parsed = 0;
	while (nRemainSize > 3) {
		start_code = find_start_code(pStart, nRemainSize, &m4vparse->state, &parsed);

		nRemainSize -= parsed;
		pStart += parsed;
		total_parsed += parsed;

		/* Copy everything in every buffer until we find the start code
		   after a VOP. In that case, copy until the end of the VOP
		   In the unlikely case of a start code (following a VOP) which is split
		   across two buffers, the start code will only be detected after we have
		   copied part of it over to the VPU buffers. In this case we have to do the
		   following:
			1) truncate the extra bytes that were copied to the VPU buffers
			2) recreate the start code prefix in a new buffer (always 0x001) */

		if (start_code < 0) {
			unsigned int end = m4vparse->buffer_pos +
						pBuffer->nFilledLen +
						m4vparse->crossBufStartCode;
			if (end > m4vparse->buffer_size) {
				m4vparse->buffer_size = end * 3 / 2;
				expand_picture_buffer(pActivePic,
					m4vparse->buffer_size);
			}
			m4vparse->buffer_pos = copyBufData(pActivePic,
					pBuffer, pBuffer->nFilledLen,
					m4vparse->crossBufStartCode, 0);
			m4vparse->crossBufStartCode = 0;
		} else if (m4vparse->foundVOP || eos) {
			int crossBufStartCode = 0;
			unsigned int end;
			if (!eos)
				total_parsed -= 4;
			if (total_parsed < 0)
				crossBufStartCode = -total_parsed;

			if (!pActivePic->has_meta) {
				logd("store buffer metadata\n");
				buffer_meta = save_omx_buffer_metainfo(pBuffer);
				pActivePic->buffer_meta = buffer_meta;
				pActivePic->has_meta = 1;
			}
			end = m4vparse->buffer_pos + total_parsed +
				m4vparse->crossBufStartCode;
			if (end > m4vparse->buffer_size && !crossBufStartCode) {
				m4vparse->buffer_size = end * 3 / 2;
				expand_picture_buffer(pActivePic,
					m4vparse->buffer_size);
			}
			m4vparse->buffer_pos = copyBufData(pActivePic,
					pBuffer, total_parsed,
					m4vparse->crossBufStartCode,
					crossBufStartCode);
			m4vparse->crossBufStartCode = crossBufStartCode;
			pActivePic->hasSlice = 1;
			m4vparse->foundVOP = 0;
			m4vparse->buffer_pos = 0;
			m4vparse->buffer_size = BUFFER_SIZE;
			return !eos;
		}

		if (start_code >= 0)
			m4vparse->foundVOP = (start_code == VOP_START_CODE);
	}
	*pIsInBufferNeeded = OMX_TRUE;
	return OMX_FALSE;
}

void
flushMpegParser(shvpu_decode_PrivateType *shvpu_decode_Private) {
	struct m4vparse_meta *m4vparse =
			shvpu_decode_Private->avCodec->codec_priv;
	memset(m4vparse, 0, sizeof(*m4vparse));
	m4vparse->buffer_size = BUFFER_SIZE;
}

void
deinitMpegParser(shvpu_decode_PrivateType *shvpu_decode_Private) {
	struct m4vparse_meta *m4vparse =
			shvpu_decode_Private->avCodec->codec_priv;
	free(m4vparse);
}

static const unsigned char mpeg4_data_eos[16] = {
	0x00, 0x00, 0x01, 0xB1,
};

static struct input_parse_ops m4v_parse_ops = {
	.parseBuffer = parseMpegBuffer,
	.parserFlush = flushMpegParser,
	.parserDeinit = deinitMpegParser,
	.EOSCode = mpeg4_data_eos,
	.EOSCodeLen = sizeof(mpeg4_data_eos),
};

int
initMpegParser(shvpu_decode_PrivateType *shvpu_decode_Private) {
	struct m4vparse_meta *m4vparse;
	shvpu_decode_codec_t *pCodec = shvpu_decode_Private->avCodec;
	m4vparse = pCodec->codec_priv =
		calloc(1, sizeof(struct m4vparse_meta));
	memset(m4vparse, 0, sizeof(struct m4vparse_meta));
	m4vparse->buffer_size = BUFFER_SIZE;
	pCodec->pops = &m4v_parse_ops;
	return 0;
}
