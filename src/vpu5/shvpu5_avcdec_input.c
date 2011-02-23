/**
   src/vpu5/shvpu5_avcdec_input.c

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
#include "mcvdec.h"
#include "shvpu5_avcdec.h"
#include "shvpu5_avcdec_omx.h"
#include "shvpu5_common_queue.h"
#include "shvpu5_common_log.h"

#define MAX_NALS        8192

typedef struct {
	long	id;
	int	n;
	MCVDEC_STRM_INFO_T *pStrmInfo;
	void*	uioBuf;
	size_t	uioBufSize;
} si_element_t;

long
mcvdec_uf_release_stream(MCVDEC_CONTEXT_T *context,
			 long strm_id,
			 MCVDEC_ERROR_INFO_T *error_info,
			 long imd_size)
{
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		(shvpu_avcdec_PrivateType *)context->user_info;
	shvpu_codec_t *pCodec = shvpu_avcdec_Private->avCodec;
	queue_t *pSIQueue = pCodec->pSIQueue;
	si_element_t *si;
	int i;

	if (error_info->dec_status != 0)
		logd("%s(%ld, %ld, %ld) invoked.\n",
		     __FUNCTION__, strm_id,
		     error_info->dec_status, imd_size);

	for (i=shvpu_getquenelem(pSIQueue); i>0; i--) {
		si = shvpu_dequeue(pSIQueue);
		if (si->id == strm_id)
			break;
		shvpu_queue(pSIQueue, si);
		si = NULL;
	}

	if (si) {
		pmem_free(si->uioBuf, si->uioBufSize);
		free(si->pStrmInfo);
		free(si);
	}

	pCodec->releaseBufCount++;
	return MCVDEC_NML_END;
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
setup_eos(MCVDEC_INPUT_STRM_T *input_strm, int frame, queue_t *pSIQueue)
{
	const unsigned char nal_data_eos[16] = {
		0x00, 0x00, 0x01, 0x0B,
	};
	MCVDEC_STRM_INFO_T *si_eos;
	size_t uioBufSize;
	OMX_U8 *uioBuf, *pBuf; 
	si_element_t *si;

	logd("%s invoked.\n", __FUNCTION__);
	uioBufSize = (4 + 0x200 + 0x600 + 255) / 256;
	if ((uioBufSize % 2) == 0)
		uioBufSize++;
	uioBufSize *= 256;
	uioBuf = pBuf = pmem_alloc(uioBufSize, 32, NULL);
	if (uioBuf == NULL) {
		loge("%s: No memory for uio buffer\n",
		     __FUNCTION__);
		return MCVDEC_INPUT_SKIP_BY_USER;
	}
	pBuf += 0x200;

	memcpy(pBuf, nal_data_eos, 4);
	si_eos = calloc (1, sizeof(MCVDEC_STRM_INFO_T));
	si_eos->strm_buff_addr = pBuf;
	si_eos->strm_buff_size = 4;
	input_strm->second_id = 0;
	input_strm->strm_info = si_eos;
        input_strm->strm_cnt = 1;
	input_strm->strm_id = frame;

	si = calloc(1, sizeof(si_element_t));
	if (si) {
		si->id = input_strm->strm_id;
		si->n = input_strm->strm_cnt;
		si->pStrmInfo = input_strm->strm_info;
		si->uioBuf = uioBuf;
		si->uioBufSize = uioBufSize;
		shvpu_queue(pSIQueue, si);
	} else {
		loge("memory alloc for si_element_t failed\n");
	}

	return MCVDEC_NML_END;
}

static buffer_metainfo_t *
save_omx_buffer_metainfo(OMX_BUFFERHEADERTYPE *pBuffer)
{
	buffer_metainfo_t *pBMI;

	pBMI = calloc(1, sizeof(buffer_metainfo_t));
	if (pBMI == NULL) {
		loge("bmi alloc failed.\n");
		return NULL;
	}

	pBMI->hMarkTargetComponent = pBuffer->hMarkTargetComponent;
	pBuffer->hMarkTargetComponent = NULL;
	logd("%s: hMarkTargetComponent = %p\n",
	     __FUNCTION__, pBMI->hMarkTargetComponent);
	pBMI->pMarkData = pBuffer->pMarkData;
	pBuffer->pMarkData = NULL;
	logd("%s: pMarkData = %p\n", __FUNCTION__, pBMI->pMarkData);
	pBMI->nTimeStamp = pBuffer->nTimeStamp;
	pBuffer->nTimeStamp = 0;
	logd("%s: nTimeStamp = %d\n", __FUNCTION__, pBMI->nTimeStamp);
	pBMI->nFlags = pBuffer->nFlags &
		(OMX_BUFFERFLAG_STARTTIME | OMX_BUFFERFLAG_DECODEONLY);
	pBuffer->nFlags = 0;
	logd("%s: nFlags = %08x\n", __FUNCTION__, pBMI->nFlags);

	return pBMI;
}

long
mcvdec_uf_request_stream(MCVDEC_CONTEXT_T * context,
			 MCVDEC_INPUT_STRM_T *input_strm,
			 void *pic_option)
{
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		(shvpu_avcdec_PrivateType *)context->user_info;
	int i, j;
	MCVDEC_STRM_INFO_T *pStrmInfo;
	shvpu_codec_t *pCodec = shvpu_avcdec_Private->avCodec;
	tsem_t *pPicSem = shvpu_avcdec_Private->pPicSem;
	queue_t *pPicQueue = shvpu_avcdec_Private->pPicQueue;
	queue_t *pSIQueue = pCodec->pSIQueue;
	queue_t *pBMIQueue = pCodec->pBMIQueue;
	int *pFrameCount = &pCodec->frameCount;
	pic_t *pPic;
	nal_t *nal;
	buffer_metainfo_t *pBMI = NULL;
	size_t size, len, uioBufSize;
	OMX_U32 offDst, offSrc;
	OMX_U8 *pData, *uioBuf, *pBuf; 
	si_element_t *si;

	logd("%s invoked.\n", __FUNCTION__);

	if (pPicSem->semval == 0) {
		if (shvpu_avcdec_Private->bIsEOSReached) {
			logd("%s: EOS!\n", __FUNCTION__);
			if (pCodec->has_eos == 0) {
				pCodec->has_eos = 1;
				return setup_eos(input_strm, *pFrameCount,
					pSIQueue);
			} else {
				return MCVDEC_INPUT_END;
			}
		}
		loge("%s: No picture data!\n", __FUNCTION__);
		return MCVDEC_INPUT_SKIP_BY_USER;
	}
	tsem_down(pPicSem);
	if (getquenelem(pPicQueue) == 0) {
		loge("%s: Inconsistent semaphore value!\n",
		     __FUNCTION__);
		return MCVDEC_INPUT_SKIP_BY_USER;
	}
	pPic = dequeue(pPicQueue);
	if (pPic == NULL) {
		loge("%s: NULL picture!!\n", __FUNCTION__);
		return MCVDEC_INPUT_SKIP_BY_USER;
	}

	pStrmInfo = calloc(pPic->n_nals, sizeof(MCVDEC_STRM_INFO_T));
	if (pStrmInfo == NULL) {
		loge("%s: No memory for stream info data\n",
		     __FUNCTION__);
		return MCVDEC_INPUT_SKIP_BY_USER;
	}

	/* copy data into uio memory buffer */
	uioBufSize = (pPic->size + 0x200 + 0x600 + 255) / 256;
	if ((uioBufSize % 2) == 0)
		uioBufSize++;
	uioBufSize *= 256;
	uioBuf = pBuf = pmem_alloc(uioBufSize, 32, NULL);
	if (uioBuf == NULL) {
		loge("%s: No memory for uio buffer\n",
		     __FUNCTION__);
		return MCVDEC_INPUT_SKIP_BY_USER;
	}
	pBuf += 0x200;
	for (i = 0; i < pPic->n_nals; i++) {
		nal = pPic->pNal[i];
		if (pBMI == NULL) {
			logd("store buffer metadata\n");
			pBMI = save_omx_buffer_metainfo(nal->pBuffer[0]);
		}
			
		size = nal->size;
		offSrc = nal->offset;
		offDst = 0;
		for (j = 0; j < 2; j++) {
			pData = nal->pBuffer[j]->pBuffer + offSrc;
			len = nal->pBuffer[j]->nFilledLen +
			    nal->pBuffer[j]->nOffset - offSrc;
			if (size < len)
				len = size;
			memcpy(pBuf + offDst, pData, len);
			size -= len;
			nal->pBuffer[j]->nFilledLen -= len;
			nal->pBuffer[j]->nOffset += len;
			offDst += len;
			if (size <= 0)
				break;
			offSrc = 0;
		}
		pStrmInfo[i].strm_buff_size = nal->size;
		pStrmInfo[i].strm_buff_addr = pBuf;
		free(nal);
		pBuf += offDst;
		if (((OMX_U32)pBuf - (OMX_U32)uioBuf) >= uioBufSize) {
			loge("uio buffer overflow %d >= %d\n",
			     (OMX_U32)pBuf - (OMX_U32)uioBuf, uioBufSize);
			break;
		}
	}
	if (pPic->hasSlice)
		pCodec->bufferingCount += 1;
	free(pPic);

	input_strm->second_id = 0;
	input_strm->strm_info = pStrmInfo;
        input_strm->strm_cnt = pPic->n_nals;
	input_strm->strm_id = *pFrameCount;
	*pFrameCount += 1;

	for (j=0; j<input_strm->strm_cnt; j++) {
		unsigned char *addr;
		addr = input_strm->strm_info[j].strm_buff_addr;
		logd("BEFORE: strm_buff_size = %d\n",
		     input_strm->strm_info[j].strm_buff_size);
		input_strm->strm_info[j].strm_buff_size =
			trim_trailing_zero(input_strm->strm_info[j].
					   strm_buff_addr,
					   input_strm->strm_info[j].
					   strm_buff_size);
		logd("AFTER : strm_buff_size = %d\n",
		     input_strm->strm_info[j].strm_buff_size);
	}

	si = calloc(1, sizeof(si_element_t));
	if (si) {
		si->id = input_strm->strm_id;
		si->n = input_strm->strm_cnt;
		si->pStrmInfo = input_strm->strm_info;
		si->uioBuf = uioBuf;
		si->uioBufSize = uioBufSize;
		shvpu_queue(pSIQueue, si);
	} else {
		loge("memory alloc for si_element_t failed\n");
	}

	if (pBMI) {
		pBMI->id = input_strm->strm_id;
		shvpu_queue(pBMIQueue, pBMI);
	}

	logd("%s: %d: %d nals input\n",
	     __FUNCTION__, input_strm->strm_id, input_strm->strm_cnt);

	return MCVDEC_NML_END;
}
/* free_remaining_streams
 * release any streams that are still being held by the
 * VPU. Only likely if playback has been forcefully terminated
   before the EOS is received.*/
void
free_remaining_streams(queue_t *pSIQueue)
{
	si_element_t *si;
	int i;
	for (i=shvpu_getquenelem(pSIQueue); i>0; i--) {
		si = shvpu_dequeue(pSIQueue);
		pmem_free(si->uioBuf, si->uioBufSize);
		free(si->pStrmInfo);
		free(si);
	}
}
