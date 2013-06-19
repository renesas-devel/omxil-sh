/**
   src/vpu5/shvpu5_decode_omx.h

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

#ifndef _OMX_VIDEODEC_COMPONENT_H_
#define _OMX_VIDEODEC_COMPONENT_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <OMX_Types.h>
#include <OMX_Component.h>
#include <OMX_Core.h>
#include <string.h>
#include <bellagio/omx_base_filter.h>
#include "uiomux/uiomux.h"
#include "mcvdec.h"
#include "avcdec.h"
#include "shvpu5_common_uio.h"
#include "shvpu5_driver.h"
#include "shvpu5_decode.h"
#include "shvpu5_common_ipmmu.h"
#include "shvpu5_common_meram.h"

/* Specific include files */
#include <vpu5/OMX_VPU5Ext.h>


#define VIDEO_DEC_BASE_NAME "OMX.re.video_decoder"
#define VIDEO_DEC_MPEG4_NAME "OMX.re.video_decoder.mpeg4"
#define VIDEO_DEC_H264_NAME "OMX.re.video_decoder.avc"
#define VIDEO_DEC_VC1_NAME "OMX.re.video_decoder.vc1"
#define VIDEO_DEC_MPEG4_ROLE "video_decoder.mpeg4"
#define VIDEO_DEC_H264_ROLE "video_decoder.avc"
#define VIDEO_DEC_VC1_ROLE "video_decoder.vc1"

#define AVC_PROFILE_COUNT 3

#define BMI_ENTRIES_SIZE 300

typedef	struct {
	long			id;
	OMX_HANDLETYPE		hMarkTargetComponent;
	OMX_PTR			pMarkData;
	OMX_TICKS		nTimeStamp;
	OMX_U32			nFlags;
} buffer_avcdec_metainfo_t;

typedef struct {
	unsigned int	ce_firmware_size;	/* (1) size  of CE firmware */
	unsigned int	vlc_firmware_size;	/* (2) size of VLC firmware */
} shvpu_firmware_size_t;

typedef struct {
	unsigned long fmem_start;
	unsigned long fmem_len;
	pthread_mutex_t filled;
} shvpu_fmem_data;

typedef struct {
	void			*codec_params;
	long			wbuf_size;
	char 			*ce_firmware_name;
	char 			*vlc_firmware_name;
	struct codec_init_ops	*ops;
	MCVDEC_API_T 		*api_tbl;
	void 			*private_data;
} shvpu_codec_params_t;

typedef struct {
	shvpu_driver_t		*pDriver;

	/** @param mode for VPU5HG video decoder */
	long 			codecMode;
	long 			outMode;
	shvpu_codec_params_t	vpu_codec_params;
	MCVDEC_WORK_INFO_T	wbuf_dec;
	MCVDEC_FIRMWARE_INFO_T	fw;
	MCVDEC_CMN_PROPERTY_T	cprop;
	MCVDEC_IMD_INFO_T	imd_info;
	MCVDEC_IR_INFO_T	ir_info;
	MCVDEC_MV_INFO_T	mv_info;
	int			frameCount;
	int			bufferingCount;
	int			releaseBufCount;
	/** @param queue for stream info data */
	queue_t*		pSIQueue;
	buffer_avcdec_metainfo_t	BMIEntries[BMI_ENTRIES_SIZE];
	OMX_BOOL		enoughHeaders;
	OMX_BOOL		enoughPreprocess;
	pthread_cond_t		cond_buffering;
	pthread_mutex_t		mutex_buffering;
	int 			has_eos;
	shvpu_fmem_data		*fmem;
	shvpu_firmware_size_t	fw_size;
	MCVDEC_FMEM_INFO_T 	*fmem_info;
	int 			fmem_size;
	/** @param private data used for codec specific processing */
	struct input_parse_ops	*pops;
	void			*codec_priv;
} shvpu_decode_codec_t;

typedef struct {
	int 	native_buffer_enable;
} android_native_t;
/** Video Decoder component private structure.
  */
DERIVEDCLASS(shvpu_decode_PrivateType, omx_base_filter_PrivateType)
#define shvpu_decode_PrivateType_FIELDS omx_base_filter_PrivateType_FIELDS \
	/** @param avCodec pointer to the VPU5HG video decoder */	\
	shvpu_decode_codec_t *avCodec;						\
	/** @param avCodecContext pointer to VPU5HG decoder context  */ \
	MCVDEC_CONTEXT_T *avCodecContext;				\
	/** @param avPicInfo pointer to the VPU5HG current decoded picrure */ 	\
	MCVDEC_CMN_PICINFO_T *avPicInfo;				\
	/** @param counting semaphore for queued NAL units */		\
	tsem_t *pNalSem; 						\
	/** @param counting semaphore for queued picture data */	\
	tsem_t *pPicSem; 						\
	/** @param queue for NAL units */				\
	queue_t *pNalQueue; 						\
	/** @param queue for picture data */				\
	queue_t *pPicQueue; 						\
	/** @param pVideoMpeg4 Reference to OMX_VIDEO_PARAM_MPEG4TYPE structure*/ \
	OMX_VIDEO_PARAM_MPEG4TYPE pVideoMpeg4;				\
	/** @param pVideoAvc Reference to OMX_VIDEO_PARAM_AVCTYPE structure */ \
	OMX_VIDEO_PARAM_AVCTYPE pVideoAvc;				\
	/** @param avcodecReady boolean flag that is true when the video coded has been initialized */ \
	OMX_BOOL avcodecReady;						\
	/** @param minBufferLength Field that stores the minimum allowed size for FFmpeg decoder */ \
	OMX_BOOL hasEOSNAL;						\
	/** @param minBufferLength Field that stores the minimum allowed size for FFmpeg decoder */ \
	OMX_U16 minBufferLength;					\
	/** @param inputCurrBuffer Field that stores pointer of the current input buffer position */ \
	OMX_U8* inputCurrBuffer;					\
	/** @param inputCurrLength Field that stores current input buffer length in bytes */ \
	OMX_U32 inputCurrLength;					\
	/** @param isFirstBuffer Field that the buffer is the first buffer */ \
	OMX_S32 isFirstBuffer;						\
	/** @param isNewBuffer Field that indicate a new buffer has arrived*/ \
	OMX_S32 isNewBuffer;						\
	/** @param video_coding_type Field that indicate the supported video format of video decoder */ \
	OMX_U32 video_coding_type;					\
	/** @param eOutFramePixFmt Field that indicate output frame pixel format */ \
	OMX_U32 eOutFramePixFmt;					\
	/** @param extradata pointer to extradata*/			\
	OMX_U8* extradata;						\
	/** @param extradata_size extradata size*/			\
	OMX_U32 extradata_size;						\
	/** @param hdr_data array to hold pointers to AVC header data*/ \
	void **intrinsic; \
	/** @param pVideoProfile reference to supported profiles*/  \
	OMX_VIDEO_PARAM_PROFILELEVELTYPE pVideoProfile[AVC_PROFILE_COUNT]; \
	/** @param pVideoProfile reference to current profile*/  \
	OMX_VIDEO_PARAM_PROFILELEVELTYPE pVideoCurrentProfile;   \
	/** @param maxVideoParameters maximu video size/level to be decoded*/  \
	OMX_PARAM_REVPU5MAXPARAM maxVideoParameters;			\
	/** @param enable_sync enable SYNC mode for vpu decode*/	\
	OMX_BOOL                enable_sync;				\
	/** @param uio_start start address of the uio memory range*/	\
	void *                  uio_start;				\
	/** @param uio_size size of the uio memory range*/		\
	unsigned long           uio_size;				\
	unsigned long           uio_start_phys;				\
	shvpu_meram_t		meram_data;				\
	shvpu_ipmmui_t		*ipmmui_data;				\
	decode_features_t	features;				\
	android_native_t	android_native;
ENDCLASS(shvpu_decode_PrivateType)

/* Component private entry points declaration */
OMX_ERRORTYPE shvpu_decode_Constructor(OMX_COMPONENTTYPE *openmaxStandComp,OMX_STRING cComponentName);
OMX_ERRORTYPE shvpu_decode_Destructor(OMX_COMPONENTTYPE *openmaxStandComp);
OMX_ERRORTYPE shvpu_decode_Init(OMX_COMPONENTTYPE *openmaxStandComp);
OMX_ERRORTYPE shvpu_decode_Deinit(OMX_COMPONENTTYPE *openmaxStandComp);
OMX_ERRORTYPE shvpu_decode_MessageHandler(OMX_COMPONENTTYPE*,internalRequestMessageType*);

void shvpu_decode_DecodePicture(
	OMX_COMPONENTTYPE *pComponent,
	OMX_BUFFERHEADERTYPE* outputbuffer);

OMX_ERRORTYPE shvpu_decode_GetParameter(
	OMX_HANDLETYPE hComponent,
	OMX_INDEXTYPE nParamIndex,
	OMX_PTR ComponentParameterStructure);

OMX_ERRORTYPE shvpu_decode_SetParameter(
	OMX_HANDLETYPE hComponent,
	OMX_INDEXTYPE nParamIndex,
	OMX_PTR ComponentParameterStructure);

OMX_ERRORTYPE shvpu_decode_GetConfig(
	OMX_HANDLETYPE hComponent,
	OMX_INDEXTYPE nIndex,
	OMX_PTR pComponentConfigStructure);

OMX_ERRORTYPE shvpu_decode_ComponentRoleEnum(
	OMX_HANDLETYPE hComponent,
	OMX_U8 *cRole,
	OMX_U32 nIndex);

OMX_ERRORTYPE
shvpu_decode_GetExtensionIndex(OMX_HANDLETYPE hComponent,
				OMX_STRING cParameterName,
				OMX_INDEXTYPE *pIndexType);

OMX_ERRORTYPE shvpu_decode_SetConfig(
	OMX_HANDLETYPE hComponent,
	OMX_INDEXTYPE nIndex,
	OMX_PTR pComponentConfigStructure);


OMX_ERRORTYPE
shvpu_decode_port_AllocateOutBuffer(
  omx_base_PortType *pPort,
  OMX_BUFFERHEADERTYPE** pBuffer,
  OMX_U32 nPortIndex,
  OMX_PTR pAppPrivate,
  OMX_U32 nSizeBytes);

OMX_ERRORTYPE shvpu_decode_port_UseBuffer(
  omx_base_PortType *openmaxStandPort,
  OMX_BUFFERHEADERTYPE** ppBufferHdr,
  OMX_U32 nPortIndex,
  OMX_PTR pAppPrivate,
  OMX_U32 nSizeBytes,
  OMX_U8* pBuffer);

OMX_ERRORTYPE
shvpu_decode_port_FreeBuffer(
  omx_base_PortType *pPort,
  OMX_U32 nPortIndex,
  OMX_BUFFERHEADERTYPE* pBuffer);

OMX_ERRORTYPE
shvpu_decode_SendCommand(
  OMX_HANDLETYPE hComponent,
  OMX_COMMANDTYPE Cmd,
  OMX_U32 nParam,
  OMX_PTR pCmdData);

OMX_ERRORTYPE
shvpu_decode_FillThisBuffer(
  OMX_HANDLETYPE hComponent,
  OMX_BUFFERHEADERTYPE* pBuffer);

/*The following functions are not directly OMX related, but
 *take structures defined in this file as arguments.
 *They are placed here temporarily, but should be refactored
 *into shvpu5_decode.h when time permits*/
long
decode_init(shvpu_decode_PrivateType *shvpu_decode_Private);

void
decode_deinit(shvpu_decode_PrivateType *shvpu_decode_Private);
#endif
