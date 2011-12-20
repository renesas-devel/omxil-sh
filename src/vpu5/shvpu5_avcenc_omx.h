/**
   src/vpu5/shvpu5_avcenc_omx.h

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

#ifndef _OMX_VIDEOENC_COMPONENT_H_
#define _OMX_VIDEOENC_COMPONENT_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <OMX_Types.h>
#include <OMX_Component.h>
#include <OMX_Core.h>
#include <string.h>
#include <bellagio/omx_base_filter.h>
#include "shvpu5_avcenc.h"
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>

#define VIDEO_ENC_BASE_NAME "OMX.re.video_encoder"
#define VIDEO_ENC_H264_NAME "OMX.re.video_encoder.avc"
#define VIDEO_ENC_H264_ROLE "video_encoder.avc"

#define AVC_PROFILE_COUNT 3

typedef	struct {
	long			id;
	OMX_HANDLETYPE		hMarkTargetComponent;
	OMX_PTR			pMarkData;
	OMX_TICKS		nTimeStamp;
	OMX_U32			nFlags;
} buffer_metainfo_t;

/** Video Encoder component private structure.
  */
DERIVEDCLASS(shvpu_avcenc_PrivateType, omx_base_filter_PrivateType)
#define shvpu_avcenc_PrivateType_FIELDS				\
	omx_base_filter_PrivateType_FIELDS			\
	/** @param avCodec pointer to the VPU5HG video decoder */	\
	shvpu_codec_t *avCodec;						\
	/** @param avcodecReady boolean flag that 		\
	    is true when the video coded has been initialized */\
	OMX_BOOL avcodecReady;					\
	/** @param isFirstBuffer Field that 			\
	    the buffer is the first buffer */			\
	OMX_S32 isFirstBuffer;					\
	/** @param isNewBuffer Field that			\
	    indicate a new buffer has arrived */		\
	OMX_S32 isNewBuffer;					\
	/** @param pBMIQueue Field that 			\
	    pointer to a queue for buffer metadata */		\
	queue_t *pBMIQueue;					\
	/** @param avcType
	    current AVCSettings from OMX_Set/GetParameter*/	\
	OMX_VIDEO_PARAM_AVCTYPE avcType;			\
	/** @param bitrateType
	    current AVCSettings from OMX_Set/GetParameter*/	\
	OMX_VIDEO_PARAM_BITRATETYPE bitrateType;
ENDCLASS(shvpu_avcenc_PrivateType)

/* Component private entry points enclaration */
OMX_ERRORTYPE
shvpu_avcenc_Constructor(OMX_COMPONENTTYPE *pComponent,
			 OMX_STRING cComponentName);
OMX_ERRORTYPE
shvpu_avcenc_Destructor(OMX_COMPONENTTYPE *pComponent);
OMX_ERRORTYPE
shvpu_avcenc_Init(OMX_COMPONENTTYPE *pComponent);
OMX_ERRORTYPE
shvpu_avcenc_Deinit(OMX_COMPONENTTYPE *pComponent);
OMX_ERRORTYPE
shvpu_avcenc_MessageHandler(OMX_COMPONENTTYPE * pComponent,
			    internalRequestMessageType * message);
OMX_ERRORTYPE
shvpu_avcenc_SetParameter(OMX_HANDLETYPE hComponent,
			  OMX_INDEXTYPE nParamIndex,
			  OMX_PTR ComponentParameterStructure);
OMX_ERRORTYPE
shvpu_avcenc_GetParameter(OMX_HANDLETYPE hComponent,
			  OMX_INDEXTYPE nParamIndex,
			  OMX_PTR ComponentParameterStructure);
OMX_ERRORTYPE
shvpu_avcenc_AllocateBuffer(omx_base_PortType *pPort,
			    OMX_BUFFERHEADERTYPE** pBuffer,
			    OMX_U32 nPortIndex, OMX_PTR pAppPrivate,
			    OMX_U32 nSizeBytes);
OMX_ERRORTYPE
shvpu_avcenc_FreeBuffer(omx_base_PortType *pPort,
			OMX_U32 nPortIndex, OMX_BUFFERHEADERTYPE* pBuffer);
OMX_ERRORTYPE
shvpu_avcenc_ComponentRoleEnum(OMX_HANDLETYPE hComponent,
			       OMX_U8 * cRole, OMX_U32 nIndex);
OMX_ERRORTYPE
shvpu_avcenc_GetExtensionIndex(OMX_HANDLETYPE hComponent,
				OMX_STRING cParameterName,
				OMX_INDEXTYPE *pIndexType);
#endif
