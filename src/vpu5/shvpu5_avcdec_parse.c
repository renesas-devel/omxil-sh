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

typedef unsigned int	uintptr_t;

/* stream end code table indicated by stream type */
static unsigned char end_code_table[8][16] = {
	{ 0x00 },                                       /* reserved */
	{ 0x00 },                                       /* reserved */
	{ 0x00,0x00,0x01,0x0B },        /* H.264  */
	{ 0x00 },                                       /* reserved */
	{ 0x00 },                                       /* reserved */
	{ 0x00 },                                       /* reserved */
	{ 0x00 },                                       /* reserved */
	{ 0x00 },                                       /* reserved */
};
static unsigned char end_code_table_no_start_code[8][16] = {
	{ 0x00 },                                       /* reserved */
	{ 0x00 },                                       /* reserved */
	{ 0x0B },                                       /* H.264  */
	{ 0x00 },                                       /* reserved */
	{ 0x00 },                                       /* reserved */
	{ 0x00 },                                       /* reserved */
	{ 0x00 },                                       /* reserved */
	{ 0x00 },                                       /* reserved */
};

static inline unsigned char *
extract_avcnal(unsigned char *buf0, size_t len0, unsigned char *buf1, size_t len1)
{
	unsigned char start_code[3], *head;
	size_t start_code_size;
	unsigned long start_code_mask;

	start_code[0] = 0x00;
	start_code[1] = 0x00;
	start_code[2] = 0x01;
	start_code_size = 3;
	start_code_mask = 0U;

	/* search start-code */
	head = mcvdec_search_startcode(buf0, len0, buf1, len1,
				       start_code,
				       start_code_size,
				       start_code_mask);

	return head;
}

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
			break;
		case 8:
			logd("%d:PPS\n", i);
			break;
		case 9:
			logd("AUD\n");
			return OMX_TRUE;
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
			return OMX_TRUE;
		default:
			loge("UNKNOWN(%2d)\n", type[i]);
			break;
		}
	}

	return OMX_FALSE;
}

void
skipFirstPadding(OMX_BUFFERHEADERTYPE *pInBuffer)
{
	void *pHead, *pStart;
	size_t nPadding;

	pStart = pInBuffer->pBuffer + pInBuffer->nOffset;
	pHead = extract_avcnal(pStart, pInBuffer->nFilledLen,
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

static inline int
registerPic(queue_t *pPicQueue, tsem_t *pPicSem,
	    queue_t *pNalQueue, tsem_t *pNalSem, OMX_BOOL hasSlice)
{
	pic_t *pPic;

	pPic = calloc(1, sizeof(pic_t));
	if (pPic == NULL)
		return -1;

	pPic->n_nals = pPic->size = 0;
	pPic->hasSlice = hasSlice;
	while (pNalSem->semval > 0) {
		tsem_down(pNalSem);
		if (pNalQueue->nelem > 0) {
			pPic->pNal[pPic->n_nals] =
				dequeue(pNalQueue);
			if (pPic->pNal[pPic->n_nals] == NULL) {
				DEBUG(DEB_LEV_ERR,
				      "Had NULL input nal!!\n");
				break;
			}
			pPic->size += pPic->pNal[pPic->n_nals]->size;
			pPic->n_nals++;
		}
	}
	queue(pPicQueue, pPic);
	tsem_up(pPicSem);
	logd("a picture with %d nals queued. %d=%d\n",
	     pPic->n_nals, pPicSem->semval, pPicQueue->nelem);

	return 0;
}

nal_t *
parseBuffer(OMX_COMPONENTTYPE * pComponent,
	    nal_t *pPrevNal,
	    OMX_BOOL * pIsInBufferNeeded)
{
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		(shvpu_avcdec_PrivateType *) pComponent->pComponentPrivate;
	tsem_t *pNalSem = shvpu_avcdec_Private->pNalSem;
	tsem_t *pPicSem = shvpu_avcdec_Private->pPicSem;
	queue_t *pNalQueue = shvpu_avcdec_Private->pNalQueue;
	queue_t *pPicQueue = shvpu_avcdec_Private->pPicQueue;
	nal_t *pNal[2] = { NULL, NULL };
	pic_t *pPic;
	void *pHead, *pStart, *pStartSub;
	size_t nRemainSize, nSizeSub;
	OMX_BOOL splitBuffer, hasSlice;
	int which, cur = 0;

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
		pHead = extract_avcnal(pStart + 3, nRemainSize - 3,
				       pStartSub, nSizeSub);
		if (pHead == NULL) {
			if (pStartSub) {
				logd("it must be the final\n");
				shvpu_avcdec_Private->bIsEOSReached = OMX_TRUE;
				goto register_nal;
			}

			*pIsInBufferNeeded = OMX_TRUE;
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

		/* check picture boundary and
		   associate queued nals with a picture */
		if (isSubsequentPic(pNal, cur ^ 1, &hasSlice)) {
			registerPic(pPicQueue, pPicSem,
				    pNalQueue, pNalSem, hasSlice);
			return pNal[cur];
		}

		cur ^= 1;
	}

	return pNal[cur ^ 1];
}

void free_remaining_pictures(shvpu_avcdec_PrivateType *shvpu_avcdec_Private) {
	tsem_t *pNalSem = shvpu_avcdec_Private->pNalSem;
	tsem_t *pPicSem = shvpu_avcdec_Private->pPicSem;
	queue_t *pNalQueue = shvpu_avcdec_Private->pNalQueue;
	queue_t *pPicQueue = shvpu_avcdec_Private->pPicQueue;
	shvpu_codec_t *pCodec = shvpu_avcdec_Private->avCodec;
	pic_t *pPic;
	nal_t *nal;

	tsem_reset(pPicSem);
	tsem_reset(pNalSem);

	while (pPicQueue->nelem > 0) {
		pPic = shvpu_dequeue(pPicQueue);
		free(pPic);
	}

	while (pNalQueue->nelem > 0) {
		nal = shvpu_dequeue(pNalQueue);
		free(nal);
	}
}
