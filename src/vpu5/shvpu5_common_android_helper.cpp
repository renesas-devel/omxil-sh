/**
   src/vpu5/shvpu_common_android_helper.cpp

   This component implements H.264 / MPEG-4 AVC video codec.
   The H.264 / MPEG-4 AVC video encoder/decoder is implemented
   on the Renesas's VPU5HG middleware library.

   Copyright (C) 2011 Renesas Solutions Corp.

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

#include <bellagio/omxcore.h>
#include <bellagio/omx_base_video_port.h>
#include <media/stagefright/HardwareAPI.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/android_native_buffer.h>
#include <ui/GraphicBuffer.h>
extern "C" {
#include "shvpu5_decode_omx.h"
#include "shvpu5_avcenc_omx.h"
#include "shvpu5_common_log.h"
}

using android::sp;
using android::UseAndroidNativeBufferParams;
using android::EnableAndroidNativeBuffersParams;
using android::GetAndroidNativeBufferUsageParams;
using android::StoreMetaDataInBuffersParams;
using android::GraphicBuffer;
using android::GraphicBufferMapper;
using android::Rect;
extern "C" {
OMX_ERRORTYPE shvpu_decode_UseAndroidNativeBuffer(
	shvpu_decode_PrivateType *shvpu_decode_Private,
	OMX_PTR ComponentParameterStructure) {

	OMX_ERRORTYPE eError;
	OMX_U32 portIndex;
	omx_base_PortType *port;
	OMX_U32 bufsize;
	int i;

	struct UseAndroidNativeBufferParams *pBuffer;

	pBuffer = (struct UseAndroidNativeBufferParams *)
			ComponentParameterStructure;

        portIndex = pBuffer->nPortIndex;
	eError =  checkHeader(ComponentParameterStructure,
		sizeof(struct UseAndroidNativeBufferParams));

        if (eError != OMX_ErrorNone)
		return eError;
        port = (omx_base_PortType *)
		shvpu_decode_Private->ports[portIndex];
	bufsize = pBuffer->nativeBuffer->stride *
		pBuffer->nativeBuffer->height;


	GraphicBufferMapper &mapper = GraphicBufferMapper::get();

	Rect bounds(pBuffer->nativeBuffer->width,
		    pBuffer->nativeBuffer->height);

	void *dst;

	mapper.lock(pBuffer->nativeBuffer->handle,
		GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &dst);

	logd("Using gralloc'd base %p", dst);
	mapper.unlock(pBuffer->nativeBuffer->handle);
        return shvpu_decode_port_UseBuffer(port, pBuffer->bufferHeader,
                portIndex, pBuffer->pAppPrivate,
		port->sPortParam.nBufferSize,
                (OMX_U8*) dst);
}
OMX_ERRORTYPE shvpu_decode_AndroidNativeBufferEnable(
	shvpu_decode_PrivateType *shvpu_decode_Private,
	OMX_PTR ComponentParameterStructure) {

	struct EnableAndroidNativeBuffersParams *pEnable;
	pEnable = (struct EnableAndroidNativeBuffersParams *)
		ComponentParameterStructure;
	shvpu_decode_Private->
		android_native.native_buffer_enable =
		pEnable->enable;

	return OMX_ErrorNone;
}
OMX_ERRORTYPE shvpu_decode_GetNativeBufferUsage(
	shvpu_decode_PrivateType *shvpu_decode_Private,
	OMX_PTR ComponentParameterStructure) {

	struct GetAndroidNativeBufferUsageParams *pUsage;
	pUsage = (struct GetAndroidNativeBufferUsageParams *)
		ComponentParameterStructure;
	pUsage->nUsage = 0;
	return OMX_ErrorNone;
}

OMX_ERRORTYPE shvpu_avcenc_SetMetaDataInBuffers(
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private,
	OMX_PTR ComponentParameterStructure) {

	struct StoreMetaDataInBuffersParams *pEnable;
	pEnable = (struct StoreMetaDataInBuffersParams *)
		ComponentParameterStructure;
	shvpu_avcenc_Private->avCodec->
		modeSettings.meta_input_buffers = pEnable->bStoreMetaData;
	return OMX_ErrorNone;
}
}
