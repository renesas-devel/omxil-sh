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
#if 0
static inline OMX_BOOL
isSubsequentPic(nal_t *pNal[], int former, OMX_BOOL *pHasSlice)
{
	int type[2], first_mb[2], i, has_eos;
	size_t len;
	OMX_U8 *pbuf;

	/* type */
	for (i = 0; i < 2; i++) {
		if (pNal[former] == NULL) {
			type[i] = 9; /* AUD */
			continue;
		}
		len =
			pNal[former]->pBuffer[0]->nOffset +
			pNal[former]->pBuffer[0]->nFilledLen -
			pNal[former]->offset;
		switch (len) {
		case 0:
		case 1:
		case 2:
		case 3:
			pbuf = pNal[former]->pBuffer[1]->pBuffer;
			type[i] = pbuf[3 - len] & 0x1fU;
			first_mb[i] = pbuf[4 - len] & 0x80U;
			break;
		case 4:
			pbuf =
				pNal[former]->pBuffer[0]->pBuffer +
				pNal[former]->offset;
			type[i] = pbuf[3] & 0x1fU;
			if (pNal[former]->pBuffer[1]) {
				pbuf = pNal[former]->pBuffer[1]->pBuffer;
				first_mb[i] = pbuf[0] & 0x80U;
			}
			break;
		default:
			pbuf =
				pNal[former]->pBuffer[0]->pBuffer +
				pNal[former]->offset;
			type[i] = pbuf[3] & 0x1fU;
			first_mb[i] = pbuf[4] & 0x80U;
			break;
		}
		switch (type[i]) {
		case 1:
		case 5:
			pNal[former]->hasPicData = OMX_TRUE;
		}
		former ^= 1;
	}

	*pHasSlice = 0;
	for (i = 0; i < 2; i++) {
		switch (type[i]) {
		case 2:
			logd("%d:DP-A\n", i);
			break;
		case 3:
			logd("%d:DP-B\n", i);
			break;
		case 4:
			logd("%d:DP-C\n", i);
			break;
		case 1:
			logd("%d:non IDR(%02x)\n", i, first_mb[i]);
			if (*pHasSlice && (first_mb[i] == 0x80U))
				return OMX_TRUE;
			*pHasSlice = 1;
			break;
		case 5:
			logd("%d:IDR(%02x)\n", i, first_mb[i]);
			if (*pHasSlice && (first_mb[i] == 0x80U))
				return OMX_TRUE;
			*pHasSlice = 1;
			break;
		case 6:
			logd("%d:SEI\n", i);
			if (*pHasSlice)
				return OMX_TRUE;
			break;
		case 8:
			logd("%d:PPS\n", i);
			break;
		case 9:
			logd("AUD\n");
			if (*pHasSlice)
				return OMX_TRUE;
			break;
		case 7:
			logd("%d:SPS\n", i);
			if (*pHasSlice)
				return OMX_TRUE;
			break;
		case 10:
			logd("%d:EoSeq\n", i);
			break;
		case 11:
			logd("%d:EoStr\n", i);
			has_eos = 1;
			if (*pHasSlice)
				return OMX_TRUE;
			break;
		case 12:
			logd("%d:Filler data\n", i);
			if (*pHasSlice)
				return OMX_TRUE;
			break;
		default:
			loge("UNKNOWN(%2d)\n", type[i]);
			break;
		}
	}

	return OMX_FALSE;
}
#endif

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
		pBuf->size += (*pNal)->size;
		pBuf->nal_sizes[i++] = (*pNal)->size;
		pBuf->n_nals++;
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
	pBuf->nal_offsets[0] = pBuf->base_addr + 256;
	for (i = 0; i < pBuf->n_nals; i++) {
		int size = (*pNal)->size;
		size_t offDst = 0;
		int len = size - (*pNal)->splitBufferLen;
		for (j = 0; j < 2; j++) {
			void *pData = (*pNal)->buffer[j];
			memcpy(pBuf->nal_offsets[i] + offDst, pData, len);
			(*pNal)->pOMXBuffer[j]->nFilledLen -= len;
			(*pNal)->pOMXBuffer[j]->nOffset += len;
			size -= len;
			offDst += len;
			if (size <= 0)
				break;
			len = (*pNal)->splitBufferLen;
		}
		if (i < (pBuf->n_nals-1)) {
			pBuf->nal_offsets[i + 1] =
				pBuf->nal_offsets[i] + (*pNal)->size;
		}
		free(*pNal);
		pNal++;
	}
	free(pNalHead);
	pPic->pBufs[pPic->n_bufs++] = pBuf;
	pPic->size += pBuf->size;
	pPic->n_nals += pBuf->n_nals;
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
#if 0
static int
copyNalData(pic_t *pPic, queue_t *pNalQueue,
			tsem_t *pNalSem, OMX_BOOL hasSlice) {

	phys_input_buf_t *pBuf;
	nal_t **pNal, **pNalHead;
	buffer_avcdec_metainfo_t buffer_meta;
	int i,j;

	pBuf = calloc(1, sizeof(*pBuf));
	if (pBuf == NULL)
		return -1;

	pNal = pNalHead = calloc(pNalQueue->nelem, sizeof(nal_t));
	if (pNalHead == NULL) {
		free (pBuf);
		return -1;
	}

	pPic->hasSlice = hasSlice;

	i = 0;
	while (pNalSem->semval > 0) {
		tsem_down(pNalSem);
		if (pNalQueue->nelem > 0) {
			(*pNal) = dequeue(pNalQueue);
			if ((*pNal) == NULL) {
				DEBUG(DEB_LEV_ERR,
				      "Had NULL input nal!!\n");
				break;
			}
			pBuf->size += (*pNal)->size;
			pBuf->nal_sizes[i++] = (*pNal)->size;
			pBuf->n_nals++;
			if (!pPic->has_meta && ((*pNal)->hasPicData)) {
				logd("store buffer metadata\n");
				buffer_meta = save_omx_buffer_metainfo((*pNal)->pBuffer[0]);
				pPic->buffer_meta = buffer_meta;
				pPic->has_meta = 1;
			}
			pNal++;
		}
	}

	pBuf->size = (pBuf->size + 0x200 + 0x600 + 255) / 256;
	if ((pBuf->size % 2) == 0)
		pBuf->size++;
	pBuf->size *= 0x200;
	pBuf->base_addr = pmem_alloc(pBuf->size, 256, NULL);
	pNal = pNalHead;
	pBuf->nal_offsets[0] = pBuf->base_addr + 256;
	for (i = 0; i < pBuf->n_nals; i++) {
		int size = (*pNal)->size;
		size_t offSrc = (*pNal)->offset;
		size_t offDst = 0;
		for (j = 0; j < 2; j++) {
			void *pData = (*pNal)->pBuffer[j]->pBuffer + offSrc;
			int len = (*pNal)->pBuffer[j]->nFilledLen +
				(*pNal)->pBuffer[j]->nOffset - offSrc;
			if (size < len)
				len = size;
			memcpy(pBuf->nal_offsets[i] + offDst, pData, len);
			size -= len;
			(*pNal)->pBuffer[j]->nFilledLen -= len;
			(*pNal)->pBuffer[j]->nOffset += len;
			offDst += len;
			if (size <= 0)
				break;
			offSrc = 0;
		}
		if (i < (pBuf->n_nals-1)) {
			pBuf->nal_offsets[i + 1] =
				pBuf->nal_offsets[i] + (*pNal)->size;
		}
		free(*pNal);
		pNal++;
	}
	free(pNalHead);
	pPic->pBufs[pPic->n_bufs++] = pBuf;
	pPic->size += pBuf->size;
	pPic->n_nals += pBuf->n_nals;
	return 0;
}
#endif

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
	queue_t *NalQueue = &avcparse->NalQueue;

	copyNalData(pActivePic, NalQueue);

	if (avcparse->pPrevBuffer) {
		pStart = avcparse->pPrevBuffer->pBuffer + avcparse->prevBufferOffset;
		nRemainSize = avcparse->pPrevBuffer->nOffset +
			avcparse->pPrevBuffer->nFilledLen -
			avcparse->prevBufferOffset;
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
		logd("pstart = %x, pStartSub = %x\n", pStart, pStartSub);
		pHead = extract_avcnal((unsigned char *)pStart + 3, nRemainSize - 3,
				       pStartSub, nSizeSub);

		if (!pHead) {
			if (eos)
				pHead = pStart + nRemainSize;
			else {
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
		splitBuffer = !((pHead >= pStart) && (pHead < pStart + nRemainSize));

		if (splitBuffer) {
			pNal->size = nRemainSize + pHead - pStartSub;
			nRemainSize = nSizeSub - (pHead - pStartSub);
			pNal->buffer[1] = pStartSub;
			pNal->splitBufferLen = (pHead - pStartSub);
			avcparse->pPrevBuffer = pBuffer;
			avcparse->prevBufferOffset = pNal->splitBufferLen + pBuffer->nOffset;
			pStartSub = NULL;
			nSizeSub = 0;
		} else {
			pNal->size = pHead - pStart;
			nRemainSize -= pNal->size;
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
	}

	avcparse->pPrevBuffer = pBuffer;
	*pIsInBufferNeeded = OMX_TRUE;
	return OMX_FALSE;
}

#if 0
nal_t *
parseAVCBuffer(OMX_COMPONENTTYPE * pComponent,
	    nal_t *pPrevNal,
	    pic_t **pPic,
	    OMX_BOOL * pIsInBufferNeeded)
{
	shvpu_decode_PrivateType *shvpu_decode_Private =
		(shvpu_decode_PrivateType *) pComponent->pComponentPrivate;
	tsem_t *pNalSem = shvpu_decode_Private->pNalSem;
	tsem_t *pPicSem = shvpu_decode_Private->pPicSem;
	queue_t *pNalQueue = shvpu_decode_Private->pNalQueue;
	queue_t *pPicQueue = shvpu_decode_Private->pPicQueue;
	nal_t *pNal[2] = { NULL, NULL };
	void *pHead, *pStart, *pStartSub;
	size_t nRemainSize, nSizeSub;
	OMX_BOOL splitBuffer, hasSlice;
	int which, cur = 0;
	pic_t *pActivePic = *pPic;
;
	/* error check */
	if (!pPrevNal) {
		loge("NULL pPrevNAL!!!\n");
		return NULL;
	}

	pNal[cur] = pPrevNal;
	cur ^= 1;
	splitBuffer = (pNal[cur ^ 1]->pBuffer[1]) ? OMX_TRUE : OMX_FALSE;

	while (pNal[cur ^ 1]->size >= 3) {
		/* look for a NAL head */
		pStart = pNal[cur ^ 1]->pBuffer[0]->pBuffer +
			pNal[cur ^ 1]->offset;
		nRemainSize = pNal[cur ^ 1]->size;
		if (splitBuffer) {
			pStartSub = pNal[cur ^ 1]->pBuffer[1]->pBuffer +
				pNal[cur ^ 1]->pBuffer[1]->nOffset;
			nSizeSub = pNal[cur ^ 1]->pBuffer[1]->nFilledLen;
		} else {
			pStartSub = NULL;
			nSizeSub = 0;
		}
		pHead = extract_avcnal((unsigned char *)pStart + 3, nRemainSize - 3,
				       pStartSub, nSizeSub);
		if (pHead == NULL) {
			if (pStartSub) {
				logd("it must be the final\n");
				shvpu_decode_Private->bIsEOSReached = OMX_TRUE;
				goto register_nal;
			}

			*pIsInBufferNeeded = OMX_TRUE;
			if (pNalQueue->nelem > 0) {
				copyNalData(pActivePic, pNalQueue, pNalSem, 0);
			}
			break;
		}

		/* create a NAL entry */
		pNal[cur] = calloc(1, sizeof(nal_t));
		which = isInsideBuffer(pHead, pStartSub, nSizeSub) ? 1 : 0;
		pNal[cur]->pBuffer[0] = pNal[cur ^ 1]->pBuffer[which];
		pNal[cur]->offset = (OMX_U32)pHead -
			(OMX_U32)pNal[cur ^ 1]->pBuffer[which]->pBuffer;

		/* in the meanwhile, it assumes that the nal size
		   may be equal to remain size of the buffer */
		pNal[cur]->size = pNal[cur]->pBuffer[0]->nOffset +
			pNal[cur]->pBuffer[0]->nFilledLen -
			pNal[cur]->offset;

		/* fix up size of the previous nal */
		if (splitBuffer) {
			pNal[cur ^ 1]->size +=
				(OMX_U32)pHead - (OMX_U32)pStartSub;
			splitBuffer = OMX_FALSE;
		} else {
			pNal[cur ^ 1]->size =
				(OMX_U32)pHead - (OMX_U32)pStart;
		}

	register_nal:
		/* queue the previous nal */
		queue(pNalQueue, pNal[cur ^ 1]);
		tsem_up(pNalSem);

		if (!pActivePic) {
			pActivePic = *pPic = calloc(1, sizeof(pic_t));
			memset(pActivePic, 0, sizeof(pic_t));
		}

		/* check picture boundary and
		   associate queued nals with a picture */
		if (isSubsequentPic(pNal, cur ^ 1, &hasSlice)) {
			copyNalData(pActivePic, pNalQueue, pNalSem,
					hasSlice);
			queue(pPicQueue, pActivePic);
			tsem_up(pPicSem);
			pActivePic = *pPic = NULL;
			return pNal[cur];
		}

		cur ^= 1;
	}

	return pNal[cur ^ 1];
}
#endif
void
flushAvcParser(shvpu_decode_PrivateType *shvpu_decode_Private) {
	tsem_t *pPicSem = shvpu_decode_Private->pPicSem;
	queue_t *pPicQueue = shvpu_decode_Private->pPicQueue;
	queue_t *pNalQueue = shvpu_decode_Private->pNalQueue;
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

static struct input_parse_ops avc_parse_ops = {
	.parseBuffer = parseAVCBuffer,
	.parserFlush = flushAvcParser,
	.parserDeinit = deinitAvcParser
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
