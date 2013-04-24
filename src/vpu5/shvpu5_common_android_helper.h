/**
   src/vpu5/shvpu_common_android_helper.h

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
#if __cplusplus
extern "C" {
#endif
#ifdef DECODER_COMPONENT
#include "shvpu5_decode_omx.h"
#endif
#ifdef ENCODER_COMPONENT
#include "shvpu5_avcenc_omx.h"
#endif
#ifdef DECODER_COMPONENT
OMX_ERRORTYPE shvpu_decode_UseAndroidNativeBuffer(
	shvpu_decode_PrivateType *shvpu_decode_Private,
	OMX_PTR ComponentParameterStructure);

OMX_ERRORTYPE shvpu_decode_AndroidNativeBufferEnable(
	shvpu_decode_PrivateType *shvpu_decode_Private,
	OMX_PTR ComponentParameterStructure);

OMX_ERRORTYPE shvpu_decode_GetNativeBufferUsage(
	shvpu_decode_PrivateType *shvpu_decode_Private,
	OMX_PTR ComponentParameterStructure);
#endif

#ifdef ENCODER_COMPONENT
OMX_ERRORTYPE shvpu_avcenc_SetMetaDataInBuffers(
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private,
	OMX_PTR ComponentParameterStructure);
#endif

enum {
	HAL_PIXEL_FORMAT_RGB_565       	    = 0x4,
	HAL_PIXEL_FORMAT_YCrCb_420_SP       = 0x11,
	HAL_PIXEL_FORMAT_YV12		    = 0x32315659, // YCrCb 4:2:0 Planar
};
#if __cplusplus
}
#endif
