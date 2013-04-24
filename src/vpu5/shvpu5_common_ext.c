/**
   src/vpu5/shvpu5_common_ext.c

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
#include "shvpu5_common_uio.h"
#include "shvpu5_common_ext.h"
#include "vpu5/OMX_VPU5Ext.h"
#include <string.h>
static struct extension_index_entry extension_index_list [] = {
	{OMX_IndexParamVPUMaxOutputSetting, "OMX.RE.VPU5MaxOutputSetting"},
	{OMX_IndexParamVPUMaxInstance, "OMX.RE.VPU5MaxInstance"},
	{OMX_IndexParamSoftwareRenderMode, "OMX.RE.SoftwareRender"},
#ifdef ANDROID_CUSTOM
#ifdef DECODER_COMPONENT
	{OMX_IndexAndroidNativeEnable, "OMX.google.android.index.enableAndroidNativeBuffers"},
	{OMX_IndexAndroidUseNativeBuffer, "OMX.google.android.index.useAndroidNativeBuffer"},
	{OMX_IndexAndroidGetNativeBufferUsage, "OMX.google.android.index.getAndroidNativeBufferUsage"},
#endif
#ifdef ENCODER_COMPONENT
	{OMX_IndexAndroidMetaDataBuffers, "OMX.google.android.index.storeMetaDataInBuffers"},
#endif
#endif
};
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
OMX_ERRORTYPE lookup_ExtensionIndex(OMX_STRING cName, OMX_INDEXTYPE *pRes) {
	int i;
	for (i = 0; i < ARRAY_SIZE(extension_index_list); i++) {
		if (!strcmp(cName, extension_index_list[i].name)) {
			*pRes = extension_index_list[i].index;
			return OMX_ErrorNone;
		}
	}
	return OMX_ErrorUnsupportedIndex;
}
