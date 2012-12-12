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
#include "shvpu5_avcdec_omx.h"
#include "shvpu5_common_queue.h"
#include "shvpu5_parse_api.h"
#include "shvpu5_common_log.h"

typedef	struct {
	OMX_BUFFERHEADERTYPE 	*pOMXBuffer[2];
	void			*buffer[2];
	size_t			size;
	size_t			splitBufferLen;
	OMX_BOOL		hasPicData;
} nal_t;

struct avcparse_meta {
	OMX_BUFFERHEADERTYPE *pPrevBuffer;
	size_t		      prevBufferOffset;
	queue_t		      NalQueue;
	OMX_BOOL	      prevPictureNal;
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

static unsigned char *
extract_avcnal(unsigned char *buf0, size_t len0, unsigned char *buf1, size_t len1)
{
	int i, j;
	unsigned char *buf = buf0;
	int len;

	len = len0;

	for (i = 0; i < 2; i++) {
		for (j = 0; j < len - 2; j++) {
			if (buf[j] == 0 && buf[j+1] == 0 && buf[j+2] == 1)
				return &buf[j];
		}
		if (i == 0 && buf1) { // cross buffer checking
			if (buf[j] == 0 && buf[j+1] == 0 && buf1[0] == 1)
				return &buf[j];
			else if (buf[j+1] == 0 && buf1[0] == 0 && buf1[1] == 1)
				return &buf[j+1];
		}
		buf = buf1;
		len = len1;
	}

	return NULL;
}

static inline OMX_BOOL
isSubsequentPic(nal_t *pNal, OMX_BOOL prevPictureNal)
{
	int type, first_mb, has_eos;
	size_t len;
	OMX_U8 *pbuf;

	/* type */
	len =
		pNal->size - pNal->splitBufferLen;
	switch (len) {
	case 0:
	case 1:
	case 2:
	case 3:
		pbuf = pNal->buffer[1];
		type = pbuf[3 - len] & 0x1fU;
		first_mb = pbuf[4 - len] & 0x80U;
		break;
	case 4:
		pbuf = pNal->buffer[0];
		type = pbuf[3] & 0x1fU;
		pbuf = pNal->buffer[1];
		first_mb = pbuf[0] & 0x80U;
		break;
	default:
		pbuf = pNal->buffer[0];
		type = pbuf[3] & 0x1fU;
		first_mb = pbuf[4] & 0x80U;
		break;
	}

	switch (type) {
	case 2:
		logd("DP-A\n");
		break;
	case 3:
		logd("DP-B\n");
			break;
	case 4:
		logd("DP-C\n");
		break;
	case 1:
		logd("non IDR(%02x)\n", first_mb);
		pNal->hasPicData = OMX_TRUE;
		if (prevPictureNal && (first_mb == 0x80U))
			return OMX_TRUE;
		break;
	case 5:
		logd("IDR(%02x)\n", first_mb);
		pNal->hasPicData = OMX_TRUE;
		if (prevPictureNal && (first_mb == 0x80U))
			return OMX_TRUE;
		break;
	case 6:
		logd("SEI\n");
		if (prevPictureNal)
			return OMX_TRUE;
		break;
	case 8:
		logd("PPS\n");
		break;
	case 9:
		logd("AUD\n");
		if (prevPictureNal)
			return OMX_TRUE;
		break;
	case 7:
		logd("SPS\n");
		if (prevPictureNal)
			return OMX_TRUE;
		break;
	case 10:
		logd("EoSeq\n");
		break;
	case 11:
		logd("EoStr\n");
		has_eos = 1;
		if (prevPictureNal)
			return OMX_TRUE;
		break;
	case 12:
		logd("Filler data\n");
		if (prevPictureNal)
			return OMX_TRUE;
		break;
	default:
		loge("UNKNOWN(%2d)\n", type);
		break;
	}

	return OMX_FALSE;
}

static size_t
trim_trailing_zero(unsigned char *addr, size_t size)
{
	while ((size > 0) &&
	       (addr[size - 1] == 0x00U))
		size--;

	return size;
}

static int
copyNalData(pic_t *pPic, queue_t *pNalQueue) {

	phys_input_buf_t *pBuf;
	nal_t **pNal, **pNalHead;
	buffer_avcdec_metainfo_t buffer_meta;
	int i,j;

	if (pNalQueue->nelem <= 0)
		return 0;

	pBuf = calloc(1, sizeof(*pBuf));
	if (pBuf == NULL)
		return -1;

	pNal = pNalHead = calloc(pNalQueue->nelem, sizeof(nal_t *));
	if (pNalHead == NULL) {
		free (pBuf);
		return -1;
	}

	pPic->hasSlice = 1;

	i = 0;
	while (pNalQueue->nelem > 0) {
		(*pNal) = dequeue(pNalQueue);
		if ((*pNal) == NULL) {
			DEBUG(DEB_LEV_ERR,
			      "Had NULL input nal!!\n");
			break;
		}

		int len = (*pNal)->size - (*pNal)->splitBufferLen;
		(*pNal)->pOMXBuffer[0]->nFilledLen -= len;
		(*pNal)->pOMXBuffer[0]->nOffset += len;
		len = (*pNal)->splitBufferLen;
		int shrink = -1;
		if ((*pNal)->splitBufferLen) {
			shrink = trim_trailing_zero((*pNal)->buffer[1],
					   (*pNal)->splitBufferLen);
			(*pNal)->size -= ((*pNal)->splitBufferLen - shrink);
			(*pNal)->splitBufferLen = shrink;
			(*pNal)->pOMXBuffer[1]->nFilledLen -= len;
			(*pNal)->pOMXBuffer[1]->nOffset += len;
		}
		if (!shrink) {
			(*pNal)->size = trim_trailing_zero((*pNal)->buffer[0],
					   (*pNal)->size);
		}

		pBuf->size += (*pNal)->size;
		pBuf->buf_sizes[i++] = (*pNal)->size;
		pBuf->n_sbufs++;
		if (!pPic->has_meta && ((*pNal)->hasPicData)) {
			logd("store buffer metadata\n");
			buffer_meta = save_omx_buffer_metainfo((*pNal)->pOMXBuffer[0]);
			pPic->buffer_meta = buffer_meta;
			pPic->has_meta = 1;
		}
		pNal++;
	}

	pBuf->size = (pBuf->size + 0x200 + 0x600 + 255) / 256;
	if ((pBuf->size % 2) == 0)
		pBuf->size++;
	pBuf->size *= 0x200;
	pBuf->base_addr = pmem_alloc(pBuf->size, 256, NULL);
	pNal = pNalHead;
	pBuf->buf_offsets[0] = pBuf->base_addr + 256;
	for (i = 0; i < pBuf->n_sbufs; i++) {
		int size = (*pNal)->size;
		size_t offDst = 0;
		int len = size - (*pNal)->splitBufferLen;
		for (j = 0; j < 2; j++) {
			void *pData = (*pNal)->buffer[j];
			memcpy(pBuf->buf_offsets[i] + offDst, pData, len);
			size -= len;
			offDst += len;
			if (size <= 0)
				break;
			len = (*pNal)->splitBufferLen;
		}
		if (i < (pBuf->n_sbufs-1)) {
			pBuf->buf_offsets[i + 1] =
				pBuf->buf_offsets[i] + (*pNal)->size;
		}
		free(*pNal);
		pNal++;
	}
	free(pNalHead);
	pPic->pBufs[pPic->n_bufs++] = pBuf;
	pPic->size += pBuf->size;
	pPic->n_sbufs += pBuf->n_sbufs;
	return 0;
}

static void
skipFirstPadding(OMX_BUFFERHEADERTYPE *pInBuffer)
{
	void *pHead, *pStart;
	size_t nPadding;

	pStart = pInBuffer->pBuffer + pInBuffer->nOffset;
	pHead = extract_avcnal((unsigned char *)pStart, pInBuffer->nFilledLen,
			       NULL, 0);
	if (pHead) {
		nPadding = (OMX_U32)pHead - (OMX_U32)pStart;
		pInBuffer->nOffset += nPadding;
		pInBuffer->nFilledLen -= nPadding;
	}

	return;
}

static inline OMX_BOOL
isInsideBuffer(void *pHead, void *pBuffer, size_t nSize)
{
	if (pBuffer && (nSize > 0) &&
	    ((OMX_U32)pHead >= (OMX_U32)pBuffer) &&
	    ((OMX_U32)pHead < ((OMX_U32)pBuffer + nSize)))
		return OMX_TRUE;

	return OMX_FALSE;
}

static OMX_BOOL
parseAVCBuffer(shvpu_decode_PrivateType *shvpu_decode_Private,
	    OMX_BUFFERHEADERTYPE *pBuffer,
	    OMX_BOOL		 eos,
	    pic_t		 *pActivePic,
	    OMX_BOOL 		 *pIsInBufferNeeded) {

	struct avcparse_meta *avcparse = shvpu_decode_Private->avCodec->codec_priv;
	unsigned char *pHead, *pStart;
	unsigned char *pStartSub;
	size_t nRemainSize, nSizeSub;
	nal_t *pNal;
	OMX_BOOL splitBuffer;
	OMX_BOOL lastBuffer = OMX_FALSE;
	queue_t *NalQueue = &avcparse->NalQueue;

	copyNalData(pActivePic, NalQueue);

	if (avcparse->pPrevBuffer) {
		pStart = avcparse->pPrevBuffer->pBuffer +
			avcparse->pPrevBuffer->nOffset;
		nRemainSize = avcparse->pPrevBuffer->nFilledLen;
		pStartSub = pBuffer->pBuffer + pBuffer->nOffset;
		nSizeSub = pBuffer->nFilledLen;
	} else {
		skipFirstPadding(pBuffer);
		pStart = pBuffer->pBuffer + pBuffer->nOffset;
		nRemainSize = pBuffer->nFilledLen;
		pStartSub = NULL;
		nSizeSub = 0;
		avcparse->prevBufferOffset = pBuffer->nOffset;
	}


	while (nRemainSize > 3) {
		pHead = extract_avcnal((unsigned char *)pStart + 3,
					nRemainSize - 3,
					pStartSub, nSizeSub);

		if (!pHead) {
			if (eos) {
				pHead = pStart + nRemainSize - 1;
				lastBuffer = OMX_TRUE;
			} else {
				break;
			}
		}

		pNal = calloc(1, sizeof(nal_t));
		if (avcparse->pPrevBuffer) {
			pNal->pOMXBuffer[0] = avcparse->pPrevBuffer;
			pNal->pOMXBuffer[1] = pBuffer;
		} else {
			pNal->pOMXBuffer[0] = pBuffer;
		}

		pNal->buffer[0] = pStart;
		splitBuffer = !((pHead >= pStart) &&
				(pHead < pStart + nRemainSize));

		if (splitBuffer) {
			pNal->size = nRemainSize + pHead - pStartSub;
			nRemainSize = nSizeSub - (pHead - pStartSub);
			pNal->buffer[1] = pStartSub;
			pNal->splitBufferLen = (pHead - pStartSub);
			avcparse->pPrevBuffer = pBuffer;
			avcparse->prevBufferOffset = pNal->splitBufferLen +
				pBuffer->nOffset;
			pStartSub = NULL;
			nSizeSub = 0;
		} else {
			pNal->size = pHead - pStart;
			nRemainSize -= pNal->size;
			avcparse->prevBufferOffset = (pHead - pStart) +
				pBuffer->nOffset;
		}

		pStart = pHead;

		if (isSubsequentPic(pNal, avcparse->prevPictureNal)) {
			avcparse->prevPictureNal = pNal->hasPicData;
			copyNalData(pActivePic, NalQueue);
			queue(NalQueue, pNal);
			avcparse->pPrevBuffer = NULL;
			return OMX_TRUE;
		}
		queue(NalQueue, pNal);
		avcparse->prevPictureNal = pNal->hasPicData;
		if (lastBuffer) {
			copyNalData(pActivePic, NalQueue);
			memset(avcparse, 0, sizeof(*avcparse));
			return OMX_FALSE;
		}
	}

	avcparse->pPrevBuffer = pBuffer;
	*pIsInBufferNeeded = OMX_TRUE;
	return OMX_FALSE;
}

void
flushAvcParser(shvpu_decode_PrivateType *shvpu_decode_Private) {
	struct avcparse_meta *avcparse =
			shvpu_decode_Private->avCodec->codec_priv;
	tsem_t *pPicSem = shvpu_decode_Private->pPicSem;
	queue_t *pPicQueue = shvpu_decode_Private->pPicQueue;
	queue_t *pNalQueue = &avcparse->NalQueue;
	pic_t *pPic;
	nal_t *nal;

	tsem_reset(pPicSem);

	while (pPicQueue->nelem > 0) {
		pPic = dequeue(pPicQueue);
		free(pPic);
	}

	while (pNalQueue->nelem > 0) {
		nal = dequeue(pNalQueue);
		free(nal);
	}
}

void
deinitAvcParser(shvpu_decode_PrivateType *shvpu_decode_Private) {
	struct avcparse_meta *avcparse =
			shvpu_decode_Private->avCodec->codec_priv;
	free (avcparse);
}

static const unsigned char nal_data_eos[16] = {
	0x00, 0x00, 0x01, 0x0B,
};
static struct input_parse_ops avc_parse_ops = {
	.parseBuffer = parseAVCBuffer,
	.parserFlush = flushAvcParser,
	.parserDeinit = deinitAvcParser,
	.EOSCode = nal_data_eos,
	.EOSCodeLen = sizeof(nal_data_eos),
};

int
initAvcParser(shvpu_decode_PrivateType *shvpu_decode_Private) {
	struct avcparse_meta *avcparse;
	shvpu_avcdec_codec_t *pCodec = shvpu_decode_Private->avCodec;
	avcparse = pCodec->codec_priv =
		calloc(1, sizeof(struct avcparse_meta));
	memset(avcparse, 0, sizeof(struct avcparse_meta));
	queue_init(&avcparse->NalQueue);
	pCodec->pops = &avc_parse_ops;
	return 0;
}
