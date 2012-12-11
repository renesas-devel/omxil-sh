/**
   src/vpu5/shvpu5_avcdec_omx.c

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

#include <bellagio/omxcore.h>
#include <bellagio/omx_base_video_port.h>
#include "shvpu5_avcdec_omx.h"
#include "shvpu5_common_ext.h"
#include "shvpu5_common_queue.h"
#include "shvpu5_common_2ddmac.h"
#include "shvpu5_common_log.h"
#include "shvpu5_parse_api.h"
#include <OMX_Video.h>
#define _GNU_SOURCE
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "ipmmuhelper.h"
#ifdef ANDROID_CUSTOM
#include "shvpu5_common_android_helper.h"
#endif

/** Maximum Number of Video Component Instance*/
#define MAX_COMPONENT_VIDEODEC 2
/** Counter of Video Component Instance*/
static OMX_U32 noVideoDecInstance = 0;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

/** The output decoded color format */
#define OUTPUT_DECODED_COLOR_FMT OMX_COLOR_FormatYUV420SemiPlanar
#ifdef ANDROID_CUSTOM
#define OUTPUT_ANDROID_DECODED_COLOR_FMT HAL_PIXEL_FORMAT_YV12
#endif

#define DEFAULT_WIDTH 128
#define DEFAULT_HEIGHT 96
/** define the minimum input buffer size */
#define DEFAULT_VIDEO_OUTPUT_BUF_SIZE					\
	(DEFAULT_WIDTH * DEFAULT_HEIGHT * 3 / 2)	// YUV subQCIF

#define INPUT_BUFFER_COUNT 6
#define INPUT_BUFFER_SIZE (1024 * 1024)

#define LOG2_TB_DEFAULT 5 /*log2 (block width) minimum value = 4*/
#define LOG2_VB_DEFAULT 5 /*log2 (block height)*/

#ifdef TL_TILE_WIDTH_LOG2
#define LOG2_TB TL_TILE_WIDTH_LOG2
#else
#define LOG2_TB LOG2_TB_DEFAULT
#endif

#ifdef TL_TILE_HEIGHT_LOG2
#define LOG2_VB TL_TILE_HEIGHT_LOG2
#else
#define LOG2_VB LOG2_VB_DEFAULT
#endif

/** The Constructor of the video decoder component
 * @param pComponent the component handle to be constructed
 * @param cComponentName is the name of the constructed component
 */
static OMX_PARAM_REVPU5MAXINSTANCE maxVPUInstances = {
	/* SYNC mode set if nInstances */
	.nInstances = 1
};

static void*
shvpu_avcdec_BufferMgmtFunction (void* param);
static void
SetInternalVideoParameters(OMX_COMPONENTTYPE * pComponent);

OMX_ERRORTYPE
shvpu_avcdec_Constructor(OMX_COMPONENTTYPE * pComponent,
			 OMX_STRING cComponentName)
{

	OMX_ERRORTYPE eError = OMX_ErrorNone;
	shvpu_decode_PrivateType *shvpu_decode_Private;
	omx_base_video_PortType *inPort, *outPort;
	OMX_U32 i;
	unsigned long reg;
	size_t memsz;

	pthread_mutex_lock(&initMutex);

	if (noVideoDecInstance > maxVPUInstances.nInstances)   {
		pthread_mutex_unlock(&initMutex);
		return OMX_ErrorInsufficientResources;
	}

	noVideoDecInstance++;
	pthread_mutex_unlock(&initMutex);

	/* initialize component private data */
	if (!pComponent->pComponentPrivate) {
		DEBUG(DEB_LEV_FUNCTION_NAME, "In %s, allocating component\n",
		      __func__);
		pComponent->pComponentPrivate =
			calloc(1, sizeof(shvpu_decode_PrivateType));
		if (pComponent->pComponentPrivate == NULL) {
			return OMX_ErrorInsufficientResources;
		}
	} else {
		DEBUG(DEB_LEV_FUNCTION_NAME,
		      "In %s, Error Component %x Already Allocated\n",
		      __func__, (int)pComponent->pComponentPrivate);
	}

	shvpu_decode_Private = pComponent->pComponentPrivate;
	shvpu_decode_Private->ports = NULL;

	/* construct base filter */
	eError = omx_base_filter_Constructor(pComponent, cComponentName);

	shvpu_decode_Private->
		sPortTypesParam[OMX_PortDomainVideo].nStartPortNumber = 0;
	shvpu_decode_Private->sPortTypesParam[OMX_PortDomainVideo].nPorts = 2;

	/** Allocate Ports and call port constructor. */
	if (shvpu_decode_Private->sPortTypesParam[OMX_PortDomainVideo].nPorts
	    && !shvpu_decode_Private->ports) {
		shvpu_decode_Private->ports =
			calloc(shvpu_decode_Private->sPortTypesParam
			       [OMX_PortDomainVideo].nPorts,
			       sizeof(omx_base_PortType *));
		if (!shvpu_decode_Private->ports) {
			return OMX_ErrorInsufficientResources;
		}
		for (i = 0;
		     i <
			     shvpu_decode_Private->
			     sPortTypesParam[OMX_PortDomainVideo].nPorts; i++) {
			shvpu_decode_Private->ports[i] =
				calloc(1, sizeof(omx_base_video_PortType));
			if (!shvpu_decode_Private->ports[i]) {
				return OMX_ErrorInsufficientResources;
			}
		}
	}

	base_video_port_Constructor(pComponent,
				    &shvpu_decode_Private->ports[0], 0,
				    OMX_TRUE);
	base_video_port_Constructor(pComponent,
				    &shvpu_decode_Private->ports[1], 1,
				    OMX_FALSE);

	/** here we can override whatever defaults the base_component
	    constructor set e.g. we can override the function pointers
	    in the private struct
	*/

	/** Domain specific section for the ports.
	 * first we set the parameter common to both formats
	 */
	//common parameters related to input port
	inPort =
		(omx_base_video_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	inPort->sPortParam.nBufferSize = INPUT_BUFFER_SIZE; //max NAL /2 *DHG*/
	inPort->sPortParam.nBufferCountMin = INPUT_BUFFER_COUNT;
	inPort->sPortParam.nBufferCountActual = INPUT_BUFFER_COUNT;
	inPort->sPortParam.format.video.xFramerate = 0;
	inPort->sPortParam.format.video.eCompressionFormat =
		OMX_VIDEO_CodingAVC;
	inPort->Port_FreeBuffer = shvpu_avcdec_port_FreeBuffer;

	//common parameters related to output port
	outPort =
		(omx_base_video_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	outPort->sPortParam.format.video.eColorFormat =
		OUTPUT_DECODED_COLOR_FMT;
	outPort->sPortParam.nBufferSize = DEFAULT_VIDEO_OUTPUT_BUF_SIZE;
	outPort->sPortParam.format.video.xFramerate = 0;

	/** settings of output port parameter definition */
	outPort->sVideoParam.eColorFormat = OUTPUT_DECODED_COLOR_FMT;
	outPort->sVideoParam.xFramerate = 0;

	/** now it's time to know the video coding type of the component */
	if (!strcmp(cComponentName, VIDEO_DEC_MPEG4_NAME)) {
		shvpu_decode_Private->video_coding_type =
			OMX_VIDEO_CodingMPEG4;
	} else if (!strcmp(cComponentName, VIDEO_DEC_H264_NAME)) {
		shvpu_decode_Private->video_coding_type = OMX_VIDEO_CodingAVC;
	} else if (!strcmp(cComponentName, VIDEO_DEC_BASE_NAME)) {
		shvpu_decode_Private->video_coding_type =
			OMX_VIDEO_CodingUnused;
	} else {
		// IL client specified an invalid component name
		return OMX_ErrorInvalidComponentName;
	}

	//Set up the output buffer allocation function
	outPort->Port_AllocateBuffer = shvpu_avcdec_port_AllocateOutBuffer;
	outPort->Port_UseBuffer = shvpu_avcdec_port_UseBuffer;
	outPort->Port_FreeBuffer = shvpu_avcdec_port_FreeBuffer;
	SetInternalVideoParameters(pComponent);

	/*OMX_PARAM_REVPU5MAXPARAM*/
		setHeader(&shvpu_decode_Private->maxVideoParameters,
			  sizeof(OMX_PARAM_REVPU5MAXPARAM));
		shvpu_decode_Private->maxVideoParameters.nWidth = 1920;
		shvpu_decode_Private->maxVideoParameters.nHeight = 1080;
		shvpu_decode_Private->maxVideoParameters.eVPU5AVCLevel = OMX_VPU5AVCLevel41;
		/*OMX_PARAM_REVPU5MAXINSTANCE*/
		setHeader(&maxVPUInstances,
			sizeof (OMX_PARAM_REVPU5MAXINSTANCE));

	shvpu_decode_Private->eOutFramePixFmt = 0;

	if (shvpu_decode_Private->video_coding_type ==
	    OMX_VIDEO_CodingMPEG4) {
		shvpu_decode_Private->
			ports[OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.format.
			video.eCompressionFormat = OMX_VIDEO_CodingMPEG4;
	} else {
		shvpu_decode_Private->
			ports[OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.format.
			video.eCompressionFormat = OMX_VIDEO_CodingAVC;
	}

	/** general configuration irrespective of any video formats
	 * setting other parameters of shvpu_avcdec_private
	 */
	shvpu_decode_Private->avCodec = NULL;
	shvpu_decode_Private->avCodecContext = NULL;
	shvpu_decode_Private->avcodecReady = OMX_FALSE;
	shvpu_decode_Private->extradata = NULL;
	shvpu_decode_Private->extradata_size = 0;

	/** initializing the codec context etc that was done earlier
	    by vpulibinit function */
	shvpu_decode_Private->BufferMgmtFunction =
		shvpu_avcdec_BufferMgmtFunction;
	shvpu_decode_Private->messageHandler = shvpu_avcdec_MessageHandler;
	shvpu_decode_Private->destructor = shvpu_avcdec_Destructor;
	pComponent->SetParameter = shvpu_avcdec_SetParameter;
	pComponent->GetParameter = shvpu_avcdec_GetParameter;
	pComponent->GetConfig = shvpu_avcdec_GetConfig;
	pComponent->ComponentRoleEnum = shvpu_avcdec_ComponentRoleEnum;
	pComponent->GetExtensionIndex = shvpu_avcdec_GetExtensionIndex;
	pComponent->SendCommand = shvpu_avcdec_SendCommand;

	shvpu_decode_Private->pPicQueue = calloc(1, sizeof(queue_t));
	queue_init(shvpu_decode_Private->pPicQueue);
	shvpu_decode_Private->pPicSem = calloc(1, sizeof(tsem_t));
	tsem_init(shvpu_decode_Private->pPicSem, 0);


	/* initialize a vpu uio */
	uio_init("VPU", &reg, &shvpu_decode_Private->uio_start_phys, &memsz);
	uio_get_virt_memory(&shvpu_decode_Private->uio_start,
			&shvpu_decode_Private->uio_size);

	loge("reg = %x, mem = %x, memsz = %d\n",
	     reg, shvpu_decode_Private->uio_start_phys, memsz);

#ifdef USE_BUFFER_MODE
	shvpu_decode_Private->features.use_buffer_mode = OMX_TRUE;
#ifdef DMAC_MODE
	shvpu_decode_Private->features.dmac_mode = OMX_TRUE;
#endif
#endif

#ifdef TL_CONV_ENABLE
	shvpu_decode_Private->features.tl_conv_mode = OMX_TRUE;
	shvpu_decode_Private->features.tl_conv_tbm = LOG2_TB;
	shvpu_decode_Private->features.tl_conv_vbm = LOG2_VB;
#endif

	/* initialize ippmui for buffers */
	if (!shvpu_decode_Private->features.use_buffer_mode)
		return eError;

	if (!shvpu_decode_Private->features.dmac_mode)
		return eError;

	if (ipmmui_buffer_init() < 0)
		return OMX_ErrorHardware;

	/* initialize 2D-DMAC for buffers */
	if (DMAC_init() < 0)
		return OMX_ErrorHardware;

	return eError;
}

/** The destructor of the video decoder component
 */
OMX_ERRORTYPE shvpu_avcdec_Destructor(OMX_COMPONENTTYPE * pComponent)
{
	shvpu_decode_PrivateType *shvpu_decode_Private =
		pComponent->pComponentPrivate;
	OMX_U32 i;

	shvpu_avcdec_Deinit(pComponent);

	if (shvpu_decode_Private->extradata) {
		free(shvpu_decode_Private->extradata);
		shvpu_decode_Private->extradata = NULL;
	}

	/* frees port/s */
	if (shvpu_decode_Private->ports) {
		for (i = 0;
		     i < shvpu_decode_Private->
			     sPortTypesParam[OMX_PortDomainVideo].nPorts; i++) {
			if (shvpu_decode_Private->ports[i])
				shvpu_decode_Private->
					ports[i]->PortDestructor
					(shvpu_decode_Private->ports[i]);
		}
		free(shvpu_decode_Private->ports);
		shvpu_decode_Private->ports = NULL;
	}

	DEBUG(DEB_LEV_FUNCTION_NAME,
	      "Destructor of video decoder component is called\n");

	/*remove any remaining picutre elements if they haven't
          already been remover (i.e. premature decode cancel)*/

	free(shvpu_decode_Private->pPicSem);
	free(shvpu_decode_Private->pPicQueue);

	omx_base_filter_Destructor(pComponent);
	noVideoDecInstance--;

	uio_deinit();

	if(shvpu_decode_Private->features.dmac_mode) {
		DMAC_deinit();
		ipmmui_buffer_deinit();
	}

	return OMX_ErrorNone;
}

/** It initializates the VPU framework, and opens an VPU videodecoder
    of type specified by IL client
*/
OMX_ERRORTYPE
shvpu_avcdec_vpuLibInit(shvpu_decode_PrivateType * shvpu_decode_Private)
{
	int ret;

	DEBUG(DEB_LEV_SIMPLE_SEQ, "VPU library/codec initialized\n");

	uiomux_lock_vpu();
	/* initialize the decoder middleware */
	ret = decode_init(shvpu_decode_Private);
	if (ret != MCVDEC_NML_END) {
		loge("decode_init() failed (%ld)\n", ret);
		return OMX_ErrorInsufficientResources;
	}
	uiomux_unlock_vpu();
	switch(shvpu_decode_Private->video_coding_type) {
	case OMX_VIDEO_CodingMPEG4:
	case OMX_VIDEO_CodingAVC:
		initAvcParser(shvpu_decode_Private);
		break;
	}
	return OMX_ErrorNone;
}

/** It Deinitializates the vpu framework, and close the vpu video
    decoder of selected coding type
*/
void
shvpu_avcdec_vpuLibDeInit(shvpu_decode_PrivateType *
			  shvpu_decode_Private)
{
	shvpu_driver_t *pDriver = shvpu_decode_Private->avCodec->pDriver;

	if (shvpu_decode_Private) {
		shvpu_avcdec_codec_t *pCodec = shvpu_decode_Private->avCodec;
		pCodec->pops->parserDeinit(shvpu_decode_Private);
		uiomux_lock_vpu();
		decode_deinit(shvpu_decode_Private);
		uiomux_unlock_vpu();
		/* decode_deinit() frees region of avCodec,
		   but pDriver still exists. */
		shvpu_driver_deinit(pDriver);
	}
}

/** internal function to set codec related parameters in the private
    type structure
*/
static void
SetInternalVideoParameters(OMX_COMPONENTTYPE * pComponent)
{

	shvpu_decode_PrivateType *shvpu_decode_Private;
	omx_base_video_PortType *inPort;

	shvpu_decode_Private = pComponent->pComponentPrivate;;

	if (shvpu_decode_Private->video_coding_type == OMX_VIDEO_CodingMPEG4) {
		strcpy(shvpu_decode_Private->ports
		       [OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.format.
		       video.cMIMEType, "video/mpeg4");
		shvpu_decode_Private->
			ports[OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.format.
			video.eCompressionFormat = OMX_VIDEO_CodingMPEG4;

		setHeader(&shvpu_decode_Private->pVideoMpeg4,
			  sizeof(OMX_VIDEO_PARAM_MPEG4TYPE));
		shvpu_decode_Private->pVideoMpeg4.nPortIndex = 0;
		shvpu_decode_Private->pVideoMpeg4.nSliceHeaderSpacing = 0;
		shvpu_decode_Private->pVideoMpeg4.bSVH = OMX_FALSE;
		shvpu_decode_Private->pVideoMpeg4.bGov = OMX_FALSE;
		shvpu_decode_Private->pVideoMpeg4.nPFrames = 0;

		shvpu_decode_Private->pVideoMpeg4.nBFrames = 0;
		shvpu_decode_Private->pVideoMpeg4.nIDCVLCThreshold = 0;
		shvpu_decode_Private->pVideoMpeg4.bACPred = OMX_FALSE;
		shvpu_decode_Private->pVideoMpeg4.nMaxPacketSize = 0;
		shvpu_decode_Private->pVideoMpeg4.nTimeIncRes = 0;
		shvpu_decode_Private->pVideoMpeg4.eProfile =
			OMX_VIDEO_MPEG4ProfileSimple;
		shvpu_decode_Private->pVideoMpeg4.eLevel =
			OMX_VIDEO_MPEG4Level0;
		shvpu_decode_Private->pVideoMpeg4.nAllowedPictureTypes = 0;
		shvpu_decode_Private->pVideoMpeg4.nHeaderExtension = 0;
		shvpu_decode_Private->pVideoMpeg4.bReversibleVLC = OMX_FALSE;

		inPort =
			(omx_base_video_PortType *)
			shvpu_decode_Private->ports
			[OMX_BASE_FILTER_INPUTPORT_INDEX];
		inPort->sVideoParam.eCompressionFormat =
			OMX_VIDEO_CodingMPEG4;

	} else if (shvpu_decode_Private->video_coding_type ==
		   OMX_VIDEO_CodingAVC) {
		strcpy(shvpu_decode_Private->ports
		       [OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.format.
		       video.cMIMEType, "video/avc(h264)");
		shvpu_decode_Private->
			ports[OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.format.
			video.eCompressionFormat = OMX_VIDEO_CodingAVC;

		setHeader(&shvpu_decode_Private->pVideoAvc,
			  sizeof(OMX_VIDEO_PARAM_AVCTYPE));
		shvpu_decode_Private->pVideoAvc.nPortIndex = 0;
		shvpu_decode_Private->pVideoAvc.nSliceHeaderSpacing = 0;
		shvpu_decode_Private->pVideoAvc.bUseHadamard = OMX_FALSE;
		shvpu_decode_Private->pVideoAvc.nRefFrames = 2;
		shvpu_decode_Private->pVideoAvc.nPFrames = 0;
		shvpu_decode_Private->pVideoAvc.nBFrames = 0;
		shvpu_decode_Private->pVideoAvc.bUseHadamard = OMX_FALSE;
		shvpu_decode_Private->pVideoAvc.nRefFrames = 2;
		shvpu_decode_Private->pVideoAvc.eProfile =
			OMX_VIDEO_AVCProfileBaseline;
		shvpu_decode_Private->pVideoAvc.eLevel = OMX_VIDEO_AVCLevel1;
		shvpu_decode_Private->pVideoAvc.nAllowedPictureTypes = 0;
		shvpu_decode_Private->pVideoAvc.bFrameMBsOnly = OMX_FALSE;
		shvpu_decode_Private->pVideoAvc.nRefIdx10ActiveMinus1 = 0;
		shvpu_decode_Private->pVideoAvc.nRefIdx11ActiveMinus1 = 0;
		shvpu_decode_Private->pVideoAvc.bEnableUEP = OMX_FALSE;
		shvpu_decode_Private->pVideoAvc.bEnableFMO = OMX_FALSE;
		shvpu_decode_Private->pVideoAvc.bEnableASO = OMX_FALSE;
		shvpu_decode_Private->pVideoAvc.bEnableRS = OMX_FALSE;

		shvpu_decode_Private->pVideoAvc.bMBAFF = OMX_FALSE;
		shvpu_decode_Private->pVideoAvc.bEntropyCodingCABAC =
			OMX_FALSE;
		shvpu_decode_Private->pVideoAvc.bWeightedPPrediction =
			OMX_FALSE;
		shvpu_decode_Private->pVideoAvc.nWeightedBipredicitonMode = 0;
		shvpu_decode_Private->pVideoAvc.bconstIpred = OMX_FALSE;
		shvpu_decode_Private->pVideoAvc.bDirect8x8Inference =
			OMX_FALSE;
		shvpu_decode_Private->pVideoAvc.bDirectSpatialTemporal =
			OMX_FALSE;
		shvpu_decode_Private->pVideoAvc.nCabacInitIdc = 0;
		shvpu_decode_Private->pVideoAvc.eLoopFilterMode =
			OMX_VIDEO_AVCLoopFilterDisable;

	/*OMX_VIDEO_PARAM_PROFILELEVELTYPE*/
		setHeader(&shvpu_decode_Private->pVideoProfile[0],
			  sizeof(OMX_VIDEO_PARAM_PROFILELEVELTYPE));
		shvpu_decode_Private->pVideoProfile[0].eProfile =
			OMX_VIDEO_AVCProfileBaseline;
		shvpu_decode_Private->pVideoProfile[0].eLevel =
			OMX_VIDEO_AVCLevel3;
		shvpu_decode_Private->pVideoProfile[0].nProfileIndex = 0;

		setHeader(&shvpu_decode_Private->pVideoProfile[1],
			  sizeof(OMX_VIDEO_PARAM_PROFILELEVELTYPE));
		shvpu_decode_Private->pVideoProfile[1].eProfile =
			OMX_VIDEO_AVCProfileMain;
		shvpu_decode_Private->pVideoProfile[1].eLevel =
			OMX_VIDEO_AVCLevel41;
		shvpu_decode_Private->pVideoProfile[1].nProfileIndex = 1;

		setHeader(&shvpu_decode_Private->pVideoProfile[2],
			  sizeof(OMX_VIDEO_PARAM_PROFILELEVELTYPE));
		shvpu_decode_Private->pVideoProfile[2].eProfile =
			OMX_VIDEO_AVCProfileHigh;
		shvpu_decode_Private->pVideoProfile[2].eLevel =
			OMX_VIDEO_AVCLevel31;
		shvpu_decode_Private->pVideoProfile[2].nProfileIndex = 2;

		memcpy(&shvpu_decode_Private->pVideoCurrentProfile,
			&shvpu_decode_Private->pVideoProfile[0],
			sizeof (OMX_VIDEO_PARAM_PROFILELEVELTYPE));

		inPort =
			(omx_base_video_PortType *)
			shvpu_decode_Private->ports
			[OMX_BASE_FILTER_INPUTPORT_INDEX];
		inPort->sVideoParam.eCompressionFormat = OMX_VIDEO_CodingAVC;
	}
}

/** The Initialization function of the video decoder
 */
OMX_ERRORTYPE
shvpu_avcdec_Init(OMX_COMPONENTTYPE * pComponent)
{

	shvpu_decode_PrivateType *shvpu_decode_Private =
		pComponent->pComponentPrivate;
	OMX_ERRORTYPE eError = OMX_ErrorNone;

	/** Temporary First Output buffer size */
	shvpu_decode_Private->inputCurrBuffer = NULL;
	shvpu_decode_Private->inputCurrLength = 0;
	shvpu_decode_Private->isFirstBuffer = OMX_TRUE;
	shvpu_decode_Private->isNewBuffer = 1;

	return eError;
}

/** The Deinitialization function of the video decoder
 */
OMX_ERRORTYPE
shvpu_avcdec_Deinit(OMX_COMPONENTTYPE * pComponent)
{

	shvpu_decode_PrivateType *shvpu_decode_Private =
		pComponent->pComponentPrivate;
	OMX_ERRORTYPE eError = OMX_ErrorNone;

	if (shvpu_decode_Private->avcodecReady) {
		shvpu_avcdec_vpuLibDeInit(shvpu_decode_Private);
		shvpu_decode_Private->avcodecReady = OMX_FALSE;
	}

	return eError;
}

/** Executes all the required steps after an output buffer
    frame-size has changed.
*/
static inline void
UpdateFrameSize(OMX_COMPONENTTYPE * pComponent)
{
	int pg_mask;
	shvpu_decode_PrivateType *shvpu_decode_Private =
		pComponent->pComponentPrivate;
	omx_base_video_PortType *outPort =
		(omx_base_video_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	omx_base_video_PortType *inPort =
		(omx_base_video_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	if (shvpu_decode_Private->features.tl_conv_mode) {
		ROUND_NEXT_POW2(outPort->sPortParam.format.video.nStride,
			inPort->sPortParam.format.video.nFrameWidth);
	} else {
		outPort->sPortParam.format.video.nStride =
			ROUND_2POW(outPort->sPortParam.format.video.nFrameWidth,32);
	}
	outPort->sPortParam.format.video.nFrameWidth =
		ALIGN_STRIDE(inPort->sPortParam.format.video.nFrameWidth);
	outPort->sPortParam.format.video.nFrameHeight =
		inPort->sPortParam.format.video.nFrameHeight;
	outPort->sPortParam.format.video.nSliceHeight =
		ROUND_2POW(inPort->sPortParam.format.video.nFrameHeight,16);
	if (shvpu_decode_Private->features.use_buffer_mode) {
		outPort->sPortParam.nBufferSize =
			outPort->sPortParam.format.video.nFrameWidth *
			outPort->sPortParam.format.video.nFrameHeight * 3 / 2;
		pg_mask = getpagesize() - 1;
		outPort->sPortParam.nBufferSize += pg_mask;
		outPort->sPortParam.nBufferSize &= ~pg_mask;
	} else {
		outPort->sPortParam.nBufferSize =
			shvpu_decode_Private->uio_size;
	}
#if 0
	switch (outPort->sVideoParam.eColorFormat) {
	case OMX_COLOR_FormatYUV420Planar:
		if (outPort->sPortParam.format.video.nFrameWidth
		    && outPort->sPortParam.format.video.nFrameHeight) {
			outPort->sPortParam.nBufferSize =
				outPort->sPortParam.format.video.nFrameWidth *
				outPort->sPortParam.format.video.nFrameHeight *
				3 / 2;
		}
		break;
	default:
		if (outPort->sPortParam.format.video.nFrameWidth
		    && outPort->sPortParam.format.video.nFrameHeight) {
			outPort->sPortParam.nBufferSize =
				outPort->sPortParam.format.video.nFrameWidth *
				outPort->sPortParam.format.video.nFrameHeight * 3;
		}
		break;
	}
#endif
}

static inline void
handle_buffer_flush(shvpu_decode_PrivateType *shvpu_decode_Private,
		    OMX_BOOL *pIsInBufferNeeded,
		    OMX_BOOL *pIsOutBufferNeeded,
		    int *pInBufExchanged, int *pOutBufExchanged,
		    OMX_BUFFERHEADERTYPE *pInBuffer[],
		    OMX_BUFFERHEADERTYPE **ppOutBuffer,
		    queue_t *pInBufQueue)
{
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	tsem_t *pInputSem = pInPort->pBufferSem;
	tsem_t *pOutputSem = pOutPort->pBufferSem;
	shvpu_avcdec_codec_t *pCodec = shvpu_decode_Private->avCodec;
	buffer_avcdec_metainfo_t *pBMI;

	pthread_mutex_lock(&shvpu_decode_Private->flush_mutex);
	while (PORT_IS_BEING_FLUSHED(pInPort) ||
	       PORT_IS_BEING_FLUSHED(pOutPort)) {
		pthread_mutex_unlock
			(&shvpu_decode_Private->flush_mutex);

		DEBUG(DEB_LEV_FULL_SEQ,
		      "In %s 1 signalling flush all cond iE=%d,"
		      "iF=%d,oE=%d,oF=%d iSemVal=%d,oSemval=%d\n",
		      __func__, *pInBufExchanged, *pIsInBufferNeeded,
		      *pOutBufExchanged, *pIsOutBufferNeeded,
		      pInputSem->semval, pOutputSem->semval);

		if (*pIsOutBufferNeeded == OMX_FALSE
		    && PORT_IS_BEING_FLUSHED(pOutPort)) {
			pOutPort->ReturnBufferFunction(pOutPort,
						       *ppOutBuffer);
			(*pOutBufExchanged)--;
			*ppOutBuffer = NULL;
			*pIsOutBufferNeeded = OMX_TRUE;
			DEBUG(DEB_LEV_FULL_SEQ,
			      "Ports are flushing,so returning "
			      "output buffer\n");
		}

		if (PORT_IS_BEING_FLUSHED(pInPort)) {

			pInBuffer[0] = pInBuffer[1] = NULL;

			OMX_BUFFERHEADERTYPE *pFlushInBuffer;
			int n;
			for (n = *pInBufExchanged; n > 0; n--) {
				pFlushInBuffer = dequeue(pInBufQueue);
				logd("Flushing Buffer(%p,%08x)\n",
				     pFlushInBuffer, pFlushInBuffer->nFlags);
				pInPort->ReturnBufferFunction(pInPort,
					pFlushInBuffer);
				(*pInBufExchanged)--;
			}
			*pIsInBufferNeeded = OMX_TRUE;
			DEBUG(DEB_LEV_FULL_SEQ,
			      "Ports are flushing,so returning "
			      "input buffer\n");


			/*Flush out Pic and Nal queues*/
			pCodec->pops->parserFlush(shvpu_decode_Private);

			logd("Resetting play mode");
			/*Flush buffers inside VPU5*/
			mcvdec_set_play_mode(
				shvpu_decode_Private->avCodecContext,
				MCVDEC_PLAY_FORWARD, 0, 0);

			mcvdec_flush_buff(shvpu_decode_Private->avCodecContext,
				MCVDEC_FLMODE_CLEAR);

			pCodec->releaseBufCount = pCodec->bufferingCount = 0;

			if (pCodec->codecMode == MCVDEC_MODE_MAIN ) {
				pCodec->enoughHeaders = OMX_FALSE;
				pCodec->enoughPreprocess = OMX_FALSE;
				pCodec->codecMode = MCVDEC_MODE_BUFFERING;
			}

			shvpu_decode_Private->isFirstBuffer = OMX_TRUE;
		}

		DEBUG(DEB_LEV_FULL_SEQ,
		      "In %s 2 signalling flush all cond iE=%d,"
		      "iF=%d,oE=%d,oF=%d iSemVal=%d,oSemval=%d\n",
		      __func__, *pInBufExchanged, *pIsInBufferNeeded,
		      *pOutBufExchanged, *pIsOutBufferNeeded,
		      pInputSem->semval, pOutputSem->semval);

		tsem_up(shvpu_decode_Private->flush_all_condition);
		tsem_down(shvpu_decode_Private->flush_condition);
		pthread_mutex_lock(&shvpu_decode_Private->
				   flush_mutex);
	}
	pthread_mutex_unlock(&shvpu_decode_Private->flush_mutex);

	return;
}

static inline int
waitBuffers(shvpu_decode_PrivateType *shvpu_decode_Private,
	     OMX_BOOL isInBufferNeeded, OMX_BOOL isOutBufferNeeded)
{
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	tsem_t *pPicSem = shvpu_decode_Private->pPicSem;
	tsem_t *pInputSem = pInPort->pBufferSem;
	tsem_t *pOutputSem = pOutPort->pBufferSem;

	if ((isInBufferNeeded == OMX_TRUE &&
	     pPicSem->semval == 0 && pInputSem->semval == 0) &&
	    (shvpu_decode_Private->state != OMX_StateLoaded &&
	     shvpu_decode_Private->state != OMX_StateInvalid)) {
		//Signalled from EmptyThisBuffer or
		//FillThisBuffer or some thing else
		DEBUG(DEB_LEV_FULL_SEQ,
		      "Waiting for next input/output buffer\n");
		tsem_down(shvpu_decode_Private->bMgmtSem);

	}

	if (shvpu_decode_Private->state == OMX_StateLoaded
	    || shvpu_decode_Private->state == OMX_StateInvalid) {
		DEBUG(DEB_LEV_SIMPLE_SEQ,
		      "In %s Buffer Management Thread is exiting\n",
		      __func__);
		return -1;
	}

	if ((isOutBufferNeeded == OMX_TRUE &&
	     pOutputSem->semval == 0) &&
	    (shvpu_decode_Private->state != OMX_StateLoaded &&
	     shvpu_decode_Private->state != OMX_StateInvalid) &&
	    !(PORT_IS_BEING_FLUSHED(pInPort) ||
	      PORT_IS_BEING_FLUSHED(pOutPort))) {
		//Signalled from EmptyThisBuffer or
		//FillThisBuffer or some thing else
		DEBUG(DEB_LEV_FULL_SEQ,
		      "Waiting for next input/output buffer\n");
		tsem_down(shvpu_decode_Private->bMgmtSem);

	}

	if (shvpu_decode_Private->state == OMX_StateLoaded ||
	    shvpu_decode_Private->state == OMX_StateInvalid) {
		DEBUG(DEB_LEV_SIMPLE_SEQ,
		      "In %s Buffer Management Thread is exiting\n",
		      __func__);
		return -1;
	}

	return 0;
}

static inline OMX_BOOL
getInBuffer(shvpu_decode_PrivateType *shvpu_decode_Private,
	    OMX_BUFFERHEADERTYPE **ppInBuffer,
	    int *pInBufExchanged, queue_t *pProcessInBufQueue)
{
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	tsem_t *pInputSem = pInPort->pBufferSem;
	queue_t *pInputQueue = pInPort->pBufferQueue;

	DEBUG(DEB_LEV_FULL_SEQ,
	      "Waiting for input buffer semval=%d \n", pInputSem->semval);

	tsem_down(pInputSem);
	if (pInputQueue->nelem == 0) {
		DEBUG(DEB_LEV_ERR, "inconsistent semaphore\n");
		return OMX_TRUE;
	}

	*ppInBuffer = dequeue(pInputQueue);
	if (*ppInBuffer == NULL) {
		DEBUG(DEB_LEV_ERR, "Had NULL input buffer!!\n");
		return OMX_TRUE;
	}
	if (((*ppInBuffer)->hMarkTargetComponent != NULL) ||
	    ((*ppInBuffer)->pMarkData != NULL) ||
	    ((*ppInBuffer)->nTimeStamp != 0) ||
	    ((*ppInBuffer)->nFlags != 0)) {
		logd("%s: hMarkTargetComponent = %p\n",
		     __FUNCTION__, (*ppInBuffer)->hMarkTargetComponent);
		logd("%s: pMarkData = %p\n", __FUNCTION__,
		     (*ppInBuffer)->pMarkData);
		logd("%s: nTimeStamp = %d\n", __FUNCTION__,
		     (*ppInBuffer)->nTimeStamp);
		logd("%s: nFlags = %08x\n", __FUNCTION__,
		     (*ppInBuffer)->nFlags);
	}

	(*pInBufExchanged)++;
	queue(pProcessInBufQueue, *ppInBuffer);

	return OMX_FALSE;
}

static inline OMX_BOOL
takeOutBuffer(shvpu_decode_PrivateType *shvpu_decode_Private,
	      OMX_BUFFERHEADERTYPE **ppOutBuffer,
	      int *pOutBufExchanged)
{
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	tsem_t *pOutputSem = pOutPort->pBufferSem;
	queue_t *pOutputQueue = pOutPort->pBufferQueue;

	tsem_down(pOutputSem);
	if (pOutputQueue->nelem == 0) {
		DEBUG(DEB_LEV_ERR,
		      "Had NULL output buffer!! op is=%d,iq=%d\n",
		      pOutputSem->semval, pOutputQueue->nelem);
		return OMX_TRUE;
	}

	logd("dequeue(Output)\n");
	*ppOutBuffer = dequeue(pOutputQueue);
	if (*ppOutBuffer == NULL) {
		DEBUG(DEB_LEV_ERR,
		      "Had NULL output buffer!! op is=%d,iq=%d\n",
		      pOutputSem->semval, pOutputQueue->nelem);
		return OMX_TRUE;
	}
	(*pOutBufExchanged)++;

	return OMX_FALSE;
}

static inline void
handleEventMark(OMX_COMPONENTTYPE *pComponent,
	     OMX_BUFFERHEADERTYPE *pInBuffer)
{
	shvpu_decode_PrivateType *shvpu_decode_Private =
		(shvpu_decode_PrivateType *) pComponent->pComponentPrivate;

	if ((OMX_COMPONENTTYPE *)pInBuffer->hMarkTargetComponent ==
	    (OMX_COMPONENTTYPE *)pComponent) {
		/*Clear the mark and generate an event */
		(*(shvpu_decode_Private->callbacks->EventHandler))
			(pComponent, shvpu_decode_Private->callbackData,
			 OMX_EventMark,	/* The command was completed */
			 1,		/* The commands was a
				   	   OMX_CommandStateSet */
			 0,		/* The state has been changed
					   in message->messageParam2 */
			 pInBuffer->pMarkData);
	} else {
		/*If this is not the target component then pass the mark */
		shvpu_decode_Private->pMark.hMarkTargetComponent =
			pInBuffer->hMarkTargetComponent;
		shvpu_decode_Private->pMark.pMarkData =
			pInBuffer->pMarkData;
	}
	pInBuffer->hMarkTargetComponent = NULL;

	return;
}

static inline void
checkFillDone(OMX_COMPONENTTYPE * pComponent,
		OMX_BUFFERHEADERTYPE **ppOutBuffer,
		int *pOutBufExchanged,
		OMX_BOOL *pIsOutBufferNeeded)
{
	shvpu_decode_PrivateType *shvpu_decode_Private =
		pComponent->pComponentPrivate;
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];

	/*If EOS and Input buffer Filled Len Zero
	  then Return output buffer immediately */
	if (((*ppOutBuffer)->nFilledLen == 0) &&
	    !((*ppOutBuffer)->nFlags & OMX_BUFFERFLAG_EOS))
		return;

	if ((*ppOutBuffer)->nFlags & OMX_BUFFERFLAG_EOS) {
	        (*(shvpu_decode_Private->callbacks->EventHandler))
			(pComponent,
			 shvpu_decode_Private->callbackData,
			 OMX_EventBufferFlag, /* The command was completed */
			 1, /* The commands was a OMX_CommandStateSet */
			 (*ppOutBuffer)->nFlags,
			 /* The state has been changed
			    in message->messageParam2 */
			 NULL);
	}
	logd("send FillBufferDone(%d)\n", (*ppOutBuffer)->nFilledLen);
	pOutPort->ReturnBufferFunction(pOutPort, *ppOutBuffer);
	(*pOutBufExchanged)--;
	*ppOutBuffer = NULL;
	*pIsOutBufferNeeded = OMX_TRUE;
}

static inline void
checkEmptyDone(shvpu_decode_PrivateType *shvpu_decode_Private,
	       queue_t *pInBufQueue, int *pInBufExchanged)
{
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	OMX_BUFFERHEADERTYPE *pInBuffer;
	int n;

	/* Input Buffer has been completely consumed.
	  So,return input buffer */
	for (n = *pInBufExchanged; n > 0; n--) {
		pInBuffer = dequeue(pInBufQueue);
		if (pInBuffer->nFilledLen > 0) {
			queue(pInBufQueue, pInBuffer);
			continue;
		}

		/* FIXME: It should be confirmed who should
		   reset the nOffset value, component or client. */
		pInBuffer->nOffset = 0;

		logd("send EmptyBufferDone(%p,%08x)\n",
		     pInBuffer, pInBuffer->nFlags);
		pInPort->ReturnBufferFunction(pInPort, pInBuffer);
		(*pInBufExchanged)--;
	}
}

/** This is the central function for component processing. It
 * is executed in a separate thread, is synchronized with
 * semaphores at each port, those are released each time a new buffer
 * is available on the given port.
 */
static void *
shvpu_avcdec_BufferMgmtFunction(void *param)
{
	OMX_COMPONENTTYPE *pComponent = (OMX_COMPONENTTYPE *) param;
	shvpu_decode_PrivateType *shvpu_decode_Private =
		(shvpu_decode_PrivateType *) pComponent->pComponentPrivate;
	shvpu_avcdec_codec_t *pCodec = shvpu_decode_Private->avCodec;
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_decode_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
 	tsem_t *pInputSem = pInPort->pBufferSem;
	tsem_t *pOutputSem = pOutPort->pBufferSem;
	OMX_BUFFERHEADERTYPE *pOutBuffer = NULL;
	OMX_BUFFERHEADERTYPE *pInBuffer[2] = { NULL, NULL };
	OMX_BOOL isInBufferNeeded = OMX_TRUE,
		isOutBufferNeeded = OMX_TRUE;
	int inBufExchanged = 0, outBufExchanged = 0;
	tsem_t *pPicSem = shvpu_decode_Private->pPicSem;
	queue_t processInBufQueue;
	pic_t *pPic = NULL;
	int ret;

	shvpu_decode_Private->bellagioThreads->nThreadBufferMngtID =
		(long int)syscall(__NR_gettid);
	DEBUG(DEB_LEV_FUNCTION_NAME, "In %s of component %x\n", __func__,
	      (int)pComponent);
	DEBUG(DEB_LEV_SIMPLE_SEQ, "In %s the thread ID is %i\n", __func__,
	      (int)shvpu_decode_Private->bellagioThreads->
	      nThreadBufferMngtID);

	queue_init(&processInBufQueue);

	DEBUG(DEB_LEV_FUNCTION_NAME, "In %s\n", __func__);
	while (shvpu_decode_Private->state == OMX_StateIdle
	       || shvpu_decode_Private->state == OMX_StateExecuting
	       || shvpu_decode_Private->state == OMX_StatePause
	       || shvpu_decode_Private->transientState ==
	       OMX_TransStateLoadedToIdle) {

		/*Wait till the ports are being flushed */
		handle_buffer_flush(shvpu_decode_Private,
				    &isInBufferNeeded,
				    &isOutBufferNeeded,
				    &inBufExchanged, &outBufExchanged,
				    pInBuffer, &pOutBuffer, &processInBufQueue);

		/*No buffer to process. So wait here */
		ret = waitBuffers(shvpu_decode_Private,
				   isInBufferNeeded,
				   isOutBufferNeeded);
		if (ret < 0)
			break;

		if ((isInBufferNeeded == OMX_TRUE) &&
		    (pInputSem->semval > 0)) {
			getInBuffer(shvpu_decode_Private,
				    &pInBuffer[0],
				    &inBufExchanged, &processInBufQueue);
			isInBufferNeeded = OMX_FALSE;
		}

		if ((pInBuffer[0]) &&
		    (pInBuffer[0]->hMarkTargetComponent != NULL))
			handleEventMark(pComponent, pInBuffer[0]);

		/* Split the input buffer into pictures */
		if ((pPicSem->semval == 0) && (isInBufferNeeded == OMX_FALSE) &&
		    (shvpu_decode_Private->bIsEOSReached == OMX_FALSE)) {
			OMX_BOOL pic_done;
			if (!pPic)
				pPic = calloc(1, sizeof (pic_t));
			pic_done = pCodec->pops->parseBuffer(shvpu_decode_Private,
				   pInBuffer[0],
				   (pInBuffer[0]->nFlags & OMX_BUFFERFLAG_EOS),
				   pPic,
				   &isInBufferNeeded);
			if (pic_done) {
				queue(shvpu_decode_Private->pPicQueue, pPic);
				tsem_up(pPicSem);
				pPic = NULL;
			} else if (pInBuffer[0]->nFlags & OMX_BUFFERFLAG_EOS) {
			/* EOS flag and no further pictures indicates the last
			   buffer */
				shvpu_decode_Private->bIsEOSReached = OMX_TRUE;
			}
		}

		/*When we have input buffer to process then get
		  one output buffer */
		if ((isOutBufferNeeded == OMX_TRUE) &&
		    (pOutputSem->semval > 0))
			isOutBufferNeeded =
				takeOutBuffer(shvpu_decode_Private,
					      &pOutBuffer,
					      &outBufExchanged);

		if (((pPicSem->semval > 0) ||
		     shvpu_decode_Private->bIsEOSReached) &&
		    (isOutBufferNeeded == OMX_FALSE)) {

			if (shvpu_decode_Private->state ==
			    OMX_StateExecuting) {
				shvpu_avcdec_DecodePicture(pComponent,
							   pOutBuffer);
			}
			else if (!(PORT_IS_BEING_FLUSHED(pInPort) ||
				   PORT_IS_BEING_FLUSHED(pOutPort))) {
				DEBUG(DEB_LEV_ERR,
				      "In %s Received Buffer in non-"
				      "Executing State(%x)\n",
				      __func__,
				      (int)shvpu_decode_Private->state);
			} else if (pInBuffer[0]) {
				pInBuffer[0]->nFilledLen = 0;
			}

			if (shvpu_decode_Private->state == OMX_StatePause
			    && !(PORT_IS_BEING_FLUSHED(pInPort)
				 || PORT_IS_BEING_FLUSHED(pOutPort))) {
				/*Waiting at paused state */
				tsem_wait(shvpu_decode_Private->bStateSem);
			}

			checkFillDone(pComponent,
					&pOutBuffer,
					&outBufExchanged,
					&isOutBufferNeeded);
		}

		if (shvpu_decode_Private->state == OMX_StatePause
		    && !(PORT_IS_BEING_FLUSHED(pInPort)
			 || PORT_IS_BEING_FLUSHED(pOutPort))) {
			/*Waiting at paused state */
			tsem_wait(shvpu_decode_Private->bStateSem);
		}

		if (inBufExchanged > 0)
			checkEmptyDone(shvpu_decode_Private,
				       &processInBufQueue,
				       &inBufExchanged);
	}

	DEBUG(DEB_LEV_FUNCTION_NAME, "Out of %s of component %x\n", __func__,
	      (int)pComponent);
	return NULL;
}

static int
show_error(void *context)
{
	MCVDEC_ERROR_INFO_T errinfo;
	int ret;

	ret = mcvdec_get_error_info(context, &errinfo);

	logd("mcvdec_get_error_info() = %d\n", ret);
	logd("errinfo.dec_status = %ld\n", errinfo.dec_status);
	logd("errinfo.refs_status = %ld\n", errinfo.refs_status);
	logd("errinfo.hdr_err_erc = %ld\n", errinfo.hdr_err_erc);
	logd("errinfo.hdr_err_elvl = %ld\n", errinfo.hdr_err_elvl);
	logd("errinfo.hdr_err_strm_idx = %ld\n", errinfo.hdr_err_strm_idx);
	logd("errinfo.hdr_err_strm_ofs = %ld\n", errinfo.hdr_err_strm_ofs);
	logd("errinfo.vlc_err_esrc = %lx\n", errinfo.vlc_err_esrc);
	logd("errinfo.vlc_err_elvl = %lx\n", errinfo.vlc_err_elvl);
	logd("errinfo.vlc_err_sn = %lx\n", errinfo.vlc_err_sn);
	logd("errinfo.vlc_err_mbh = %lx\n", errinfo.vlc_err_mbh);
	logd("errinfo.vlc_err_mbv = %lx\n", errinfo.vlc_err_mbv);
	logd("errinfo.vlc_err_erc = %lx\n", errinfo.vlc_err_erc);
	logd("errinfo.vlc_err_sbcv = %lx\n", errinfo.vlc_err_sbcv);
	logd("errinfo.ce_err_erc = %lx\n", errinfo.ce_err_erc);
	logd("errinfo.ce_err_epy = %lx\n", errinfo.ce_err_epy);
	logd("errinfo.ce_err_epx = %lx\n", errinfo.ce_err_epx);

	return ret;
}

static inline void
wait_vlc_buffering(shvpu_avcdec_codec_t *pCodec)
{
	pthread_mutex_lock(&pCodec->mutex_buffering);
	while (!pCodec->enoughPreprocess) {
		pthread_cond_wait(&pCodec->cond_buffering,
				  &pCodec->mutex_buffering);
	}
	pthread_mutex_unlock(&pCodec->mutex_buffering);

	return;
}

/** This function is used to process the input buffer and
    provide one output buffer
*/
void
shvpu_avcdec_DecodePicture(OMX_COMPONENTTYPE * pComponent,
			   OMX_BUFFERHEADERTYPE * pOutBuffer)
{
	MCVDEC_CMN_PICINFO_T *pic_infos[2];
	MCVDEC_FMEM_INFO_T *frame;
	shvpu_decode_PrivateType *shvpu_decode_Private;
	shvpu_avcdec_codec_t *pCodec;
	MCVDEC_CONTEXT_T *pCodecContext;
	OMX_ERRORTYPE err = OMX_ErrorNone;
	long ret, hdr_ready;

	shvpu_decode_Private = pComponent->pComponentPrivate;
	hdr_ready = MCVDEC_ON;
	pCodec = shvpu_decode_Private->avCodec;
	pCodecContext = shvpu_decode_Private->avCodecContext;

	if (shvpu_decode_Private->bIsEOSReached &&
	    (pCodec->bufferingCount <= 0)) {
		logd("finalize\n");
		shvpu_decode_Private->bIsEOSReached = OMX_FALSE;
		pOutBuffer->nFlags |= OMX_BUFFERFLAG_EOS;
		return;
	}

	logd("----- invoke mcvdec_decode_picture() -----\n");
	uiomux_lock_vpu();
	ret = mcvdec_decode_picture(pCodecContext,
				    &shvpu_decode_Private->avPicInfo,
				    pCodec->codecMode,
				    &hdr_ready);
	uiomux_unlock_vpu();
	logd("----- resume from mcvdec_decode_picture() = %d -----\n", ret);
	logd("hdr_ready = %s\n", (hdr_ready == MCVDEC_ON) ?
	     "MCVDEC_ON" : "MCVDEC_OFF");
	if ((pCodec->codecMode == MCVDEC_MODE_BUFFERING) &&
	    (pCodec->enoughPreprocess == OMX_FALSE) &&
	    ((pCodec->bufferingCount - pCodec->releaseBufCount) > 5)) {
		loge("count = %d, vlc_status = %ld",
		     pCodec->bufferingCount - pCodec->releaseBufCount,
		     mciph_vlc_status(pCodec->pDriver->pDrvInfo));
		while (mciph_vlc_status(pCodec->pDriver->pDrvInfo) != 0);
	}

	switch (ret) {
	case MCVDEC_CAUTION:
		break;
	case MCVDEC_CONCEALED_1:
	case MCVDEC_CONCEALED_2:
		loge("Warning: a recoverable error (%d) "
		     "for frame-%d\n", ret,
		     shvpu_decode_Private->avPicInfo->strm_id);
		show_error(pCodecContext);
		break;
	case MCVDEC_UNSUPPORT:
		err = OMX_ErrorFormatNotDetected;
	case MCVDEC_ERR_STRM:
		err = OMX_ErrorStreamCorrupt;
		show_error(pCodecContext);
		break;
	default:
		loge("terminating because of an error(%d)\n", ret);
		return;
	case MCVDEC_NO_STRM:
	case MCVDEC_INPUT_END:
		if (!shvpu_decode_Private->bIsEOSReached) {
			err = OMX_ErrorUnderflow;
			loge("nothing to decode (%d)\n", ret);
		}
		break;
	case MCVDEC_RESOURCE_LACK:
		loge("MCVDEC_RESOURCE_LACK: hdr_ready = %d, enoughPreprocess = %d, "
		     "bufferingCount = %d, vlc_status = %ld",
		     hdr_ready, pCodec->enoughPreprocess,
		     pCodec->bufferingCount - pCodec->releaseBufCount,
		     mciph_vlc_status(pCodec->pDriver->pDrvInfo));
		if (pCodec->codecMode == MCVDEC_MODE_BUFFERING) {
			if ((hdr_ready != MCVDEC_ON) && !pCodec->enoughPreprocess &&
			    ((pCodec->bufferingCount - pCodec->releaseBufCount) == 144) &&
			    (mciph_vlc_status(pCodec->pDriver->pDrvInfo) == 0)) {
				loge("URGENT: The stagefright has been terminated!!");
				exit(1);
			}
			if ((hdr_ready == MCVDEC_ON) && !pCodec->enoughPreprocess) {
				loge("wait for filling the intermediate buffer enough");
				wait_vlc_buffering(pCodec);
			}
			loge("switching mode from BUFFERING to MAIN");
			pCodec->codecMode = MCVDEC_MODE_MAIN;
		}
		break;
	case MCVDEC_NO_FMEM_TO_WRITE:
		logd("Warning: all frame memory slots for output "
		     "have been occupied.\n");
		break;
	case MCVDEC_ERR_FMEM:
		err = OMX_ErrorInsufficientResources;
		break;
	case MCVDEC_NML_END:
		break;
	}

	if (err != OMX_ErrorNone) {
		(*(shvpu_decode_Private->callbacks->EventHandler))
			(pComponent, shvpu_decode_Private->callbackData,
			OMX_EventError, // An error occured
			err, 		// Error code
			0, NULL);
		if (err == OMX_ErrorInvalidState)
			shvpu_decode_Private->state = OMX_StateInvalid;
	}

	if ((err == OMX_ErrorNone) && shvpu_decode_Private->avPicInfo) {
		/* update port status */
		omx_base_video_PortType *inPort =
			(omx_base_video_PortType *)
			shvpu_decode_Private->
			ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
		unsigned long xpic, ypic;
		xpic = shvpu_decode_Private->avPicInfo->xpic_size -
			shvpu_decode_Private->avPicInfo->
			frame_crop[MCVDEC_CROP_LEFT] -
			shvpu_decode_Private->avPicInfo->
			frame_crop[MCVDEC_CROP_RIGHT];
		ypic = shvpu_decode_Private->avPicInfo->ypic_size -
			shvpu_decode_Private->avPicInfo->
			frame_crop[MCVDEC_CROP_TOP] -
			shvpu_decode_Private->avPicInfo->
			frame_crop[MCVDEC_CROP_BOTTOM];
		if((inPort->sPortParam.format.video.nFrameWidth != xpic) ||
		   (inPort->sPortParam.format.video.nFrameHeight != ypic)) {
			if ((xpic > shvpu_decode_Private->
					maxVideoParameters.nWidth) || (ypic >
					shvpu_decode_Private->
					maxVideoParameters.nHeight) ) {

			    (*(shvpu_decode_Private->callbacks->EventHandler))
			    (pComponent, shvpu_decode_Private->callbackData,
			    OMX_EventError, // An error occured
			    OMX_ErrorStreamCorrupt, // Error code
			    0, NULL);
			}

			DEBUG(DEB_LEV_SIMPLE_SEQ, "Sending Port Settings Change Event in video decoder\n");

			switch(shvpu_decode_Private->video_coding_type) {
			case OMX_VIDEO_CodingMPEG4 :
			case OMX_VIDEO_CodingAVC :
				inPort->sPortParam.format.video.nFrameWidth =
					xpic;
				inPort->sPortParam.format.video.nFrameHeight =
					ypic;
				break;
			default :
				shvpu_decode_Private->state = OMX_StateInvalid;
				DEBUG(DEB_LEV_ERR, "Video formats other than MPEG-4 AVC not supported\nCodec not found\n");
				err = OMX_ErrorFormatNotDetected;
				break;
			}

			UpdateFrameSize (pComponent);

			/** Send Port Settings changed call back */
			(*(shvpu_decode_Private->callbacks->EventHandler))
				(pComponent,
				 shvpu_decode_Private->callbackData,
				 OMX_EventPortSettingsChanged, // The command was completed
				 0,  //to adjust the file pointer to resume the correct decode process
				 0, // This is the input port index
				 NULL);
		}
	}

	if (pCodec->codecMode == MCVDEC_MODE_BUFFERING) {
		if (hdr_ready == MCVDEC_ON) {
			pCodec->enoughHeaders = OMX_TRUE;
			if (pCodec->enoughPreprocess) {
				if (shvpu_decode_Private->enable_sync) {
					pCodec->codecMode = MCVDEC_MODE_SYNC;
					pCodec->outMode = MCVDEC_OUTMODE_PULL;
				} else {
					pCodec->codecMode = MCVDEC_MODE_MAIN;
				}
			}
		}
		return;
	}

	logd("----- invoke mcvdec_get_output_picture() -----\n");
	logd("pCodec->bufferingCount = %d\n", pCodec->bufferingCount);
	ret = mcvdec_get_output_picture(pCodecContext,
					pic_infos, &frame,
					pCodec->outMode);
	logd("----- resume from mcvdec_get_output_picture() = %d "
	     "-----\n", ret);
	switch (ret) {
		case MCVDEC_NML_END:
		case MCVDEC_NO_PICTURE:
			break;
		default:
			err = OMX_ErrorHardware;
	}

	if (err != OMX_ErrorNone) {
		(*(shvpu_decode_Private->callbacks->EventHandler))
			(pComponent, shvpu_decode_Private->callbackData,
			OMX_EventError, // An error occured
			err, 		// Error code
			0, NULL);
		if (err == OMX_ErrorInvalidState)
			shvpu_decode_Private->state = OMX_StateInvalid;
	}

	if ((ret == MCVDEC_NML_END) && pic_infos[0] && frame) {
		void *vaddr;
		size_t pic_size;
		int i;
		unsigned long real_phys;
		unsigned long index;
		buffer_avcdec_metainfo_t *pBMI;

		logd("pic_infos[0]->frame_cnt = %d\n",
		     pic_infos[0]->frame_cnt);
		logd("pic_infos[0]->fmem_index = %d\n",
		     pic_infos[0]->fmem_index);
		logd("pic_infos[0]->strm_id = %d\n",
		     pic_infos[0]->strm_id);
		//last_decoded_frame = pic_infos[0]->strm_id;
		logd("pic_infos[0]->xpic_size = %d\n",
		     pic_infos[0]->xpic_size);
		logd("pic_infos[0]->ypic_size = %d\n",
		     pic_infos[0]->ypic_size);
		for (i=0; i<4; i++)
			logd("pic_infos[0]->frame_crop[%d] = %d\n",
			     i, pic_infos[0]->frame_crop[i]);

		pic_size = pic_infos[0]->xpic_size *
			(pic_infos[0]->ypic_size -
			 pic_infos[0]->frame_crop[MCVDEC_CROP_BOTTOM]);

		real_phys = ipmmui_to_phys(shvpu_decode_Private->ipmmui_data,
				frame->Ypic_addr,
				shvpu_decode_Private->uio_start_phys);
		vaddr = uio_phys_to_virt(real_phys);
		if ((pic_size / 2 * 3) > pOutBuffer->nAllocLen) {
			loge("WARNING: shrink output size %d to %d\n",
			     pic_size / 2 * 3, pOutBuffer->nAllocLen);
			pic_size = pOutBuffer->nAllocLen / 3 * 2;
		}
		if (shvpu_decode_Private->features.use_buffer_mode) {
			if (shvpu_decode_Private->features.dmac_mode) {
				pOutBuffer->nOffset = 0;
				DMAC_copy_buffer((unsigned long)
						pOutBuffer->pPlatformPrivate,
					frame->Ypic_addr);
			} else {
				size_t copy_size;
				size_t pitch;
				OMX_U8 *out_buffer;
				pitch = ROUND_2POW(pic_infos[0]->xpic_size, 32);
				copy_size = pitch * (pic_infos[0]->ypic_size -
					pic_infos[0]->frame_crop
						[MCVDEC_CROP_BOTTOM]);
				pOutBuffer->nOffset = 0;
				memcpy(pOutBuffer->pBuffer, vaddr, copy_size);
				memcpy(pOutBuffer->pBuffer + copy_size,
					vaddr + pitch * pic_infos[0]->ypic_size,
					copy_size / 2);
			}
		} else {
			if ((unsigned long) vaddr < (unsigned long)
				shvpu_decode_Private->uio_start)
				pOutBuffer->nOffset = 0;
			else
				pOutBuffer->nOffset = ((uint8_t *)vaddr)
				- (uint8_t *)shvpu_decode_Private->uio_start;

			if (shvpu_decode_Private->features.tl_conv_mode) {
				pOutBuffer->pPlatformPrivate = (void *)
					phys_to_ipmmui(
					shvpu_decode_Private->ipmmui_data,
					frame->Ypic_addr);
			} else {
				pOutBuffer->pPlatformPrivate = vaddr;
			}

		}
		pOutBuffer->nFilledLen += pic_size + pic_size / 2;

		/* receive an appropriate metadata */
		index = pic_infos[0]->strm_id;
		pBMI = &pCodec->BMIEntries[index % BMI_ENTRIES_SIZE];
		if (pBMI->id == pic_infos[0]->strm_id) {
			pOutBuffer->nTimeStamp = pBMI->nTimeStamp;
			pOutBuffer->nFlags = pBMI->nFlags;
		} else {
			loge("Warning: invalid hash on BMI (%d)"
			     "for frame-%d.\n", pBMI->id, index);
		}
		pCodec->bufferingCount--;
	} else {
		logd("get_output_picture return error ret = %d\n, "
		     "pic_infos[0] = %p, frame = %p", ret,
		     pic_infos[0],frame);
	}
}

OMX_ERRORTYPE
shvpu_avcdec_SetParameter(OMX_HANDLETYPE hComponent,
			  OMX_INDEXTYPE nParamIndex,
			  OMX_PTR ComponentParameterStructure)
{

	OMX_ERRORTYPE eError = OMX_ErrorNone;
	OMX_U32 portIndex;

	/* Check which structure we are being fed and
	   make control its header */
	OMX_COMPONENTTYPE *pComponent = hComponent;
	shvpu_decode_PrivateType *shvpu_decode_Private =
		pComponent->pComponentPrivate;
	omx_base_video_PortType *port;
	if (ComponentParameterStructure == NULL) {
		return OMX_ErrorBadParameter;
	}

	DEBUG(DEB_LEV_SIMPLE_SEQ, "   Setting parameter %i\n", nParamIndex);
	switch (nParamIndex) {
	case OMX_IndexParamPortDefinition:
	{
		eError =
			omx_base_component_SetParameter(hComponent,
							nParamIndex,
							ComponentParameterStructure);
		if (eError == OMX_ErrorNone) {
			OMX_PARAM_PORTDEFINITIONTYPE *pPortDef =
				(OMX_PARAM_PORTDEFINITIONTYPE *)
				ComponentParameterStructure;
			UpdateFrameSize(pComponent);
			portIndex = pPortDef->nPortIndex;
			port = (omx_base_video_PortType *)
				shvpu_decode_Private->ports[portIndex];
			port->sVideoParam.eColorFormat =
				port->sPortParam.format.video.
				eColorFormat;
		}
		break;
	}
	case OMX_IndexParamVideoPortFormat:
	{
		OMX_VIDEO_PARAM_PORTFORMATTYPE *pVideoPortFormat;
		pVideoPortFormat = ComponentParameterStructure;
		portIndex = pVideoPortFormat->nPortIndex;
		/*Check Structure Header and verify component state */
		eError =
			omx_base_component_ParameterSanityCheck
			(hComponent, portIndex, pVideoPortFormat,
			 sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
		if (eError != OMX_ErrorNone) {
			DEBUG(DEB_LEV_ERR,
			      "In %s Parameter Check Error=%x\n",
			      __func__, eError);
			break;
		}
		if (portIndex <= 1) {
			port = (omx_base_video_PortType *)
				shvpu_decode_Private->ports[portIndex];
			memcpy(&port->sVideoParam, pVideoPortFormat,
			       sizeof
			       (OMX_VIDEO_PARAM_PORTFORMATTYPE));
			shvpu_decode_Private->
				ports[portIndex]->sPortParam.format.video.
				eColorFormat =
				port->sVideoParam.eColorFormat;

			if (portIndex == 1) {
				switch (port->sVideoParam.
					eColorFormat) {
				case OMX_COLOR_Format24bitRGB888:
					shvpu_decode_Private->eOutFramePixFmt = 0;
					break;
				case OMX_COLOR_Format24bitBGR888:
					shvpu_decode_Private->eOutFramePixFmt = 1;
					break;
				case OMX_COLOR_Format32bitBGRA8888:
					shvpu_decode_Private->eOutFramePixFmt = 2;
					break;
				case OMX_COLOR_Format32bitARGB8888:
					shvpu_decode_Private->eOutFramePixFmt = 3;
					break;
				case OMX_COLOR_Format16bitARGB1555:
					shvpu_decode_Private->eOutFramePixFmt = 4;
					break;
				case OMX_COLOR_Format16bitRGB565:
					shvpu_decode_Private->eOutFramePixFmt = 5;
					break;
				case OMX_COLOR_Format16bitBGR565:
					shvpu_decode_Private->eOutFramePixFmt = 6;
					break;
				default:
					shvpu_decode_Private->eOutFramePixFmt = 7;
					break;
				}
				UpdateFrameSize(pComponent);
			}
		} else {
			return OMX_ErrorBadPortIndex;
		}
		break;
	}
	case OMX_IndexParamVideoAvc:
	{
		OMX_VIDEO_PARAM_AVCTYPE *pVideoAvc;
		pVideoAvc = ComponentParameterStructure;
		portIndex = pVideoAvc->nPortIndex;
		eError =
			omx_base_component_ParameterSanityCheck
			(hComponent, portIndex, pVideoAvc,
			 sizeof(OMX_VIDEO_PARAM_AVCTYPE));
		if (eError != OMX_ErrorNone) {
			DEBUG(DEB_LEV_ERR,
			      "In %s Parameter Check Error=%x\n",
			      __func__, eError);
			break;
		}
		memcpy(&shvpu_decode_Private->pVideoAvc, pVideoAvc,
		       sizeof(OMX_VIDEO_PARAM_AVCTYPE));
		break;
	}
	case OMX_IndexParamStandardComponentRole:
	{
		OMX_PARAM_COMPONENTROLETYPE *pComponentRole;
		pComponentRole = ComponentParameterStructure;
		if (shvpu_decode_Private->state != OMX_StateLoaded
		    && shvpu_decode_Private->state !=
		    OMX_StateWaitForResources) {
			DEBUG(DEB_LEV_ERR,
			      "In %s Incorrect State=%x lineno=%d\n",
			      __func__, shvpu_decode_Private->state,
			      __LINE__);
			return OMX_ErrorIncorrectStateOperation;
		}

		if ((eError =
		     checkHeader(ComponentParameterStructure,
				 sizeof(OMX_PARAM_COMPONENTROLETYPE)))
		    != OMX_ErrorNone) {
			break;
		}

		if (!strcmp
		    ((char *)pComponentRole->cRole,
		     VIDEO_DEC_MPEG4_ROLE)) {
			shvpu_decode_Private->video_coding_type =
				OMX_VIDEO_CodingMPEG4;
		} else
			if (!strcmp
			    ((char *)pComponentRole->cRole,
			     VIDEO_DEC_H264_ROLE)) {
				shvpu_decode_Private->video_coding_type =
					OMX_VIDEO_CodingAVC;
			} else {
				return OMX_ErrorBadParameter;
			}
		SetInternalVideoParameters(pComponent);
		break;
	}
	case OMX_IndexParamVideoMpeg4:
	{
		OMX_VIDEO_PARAM_MPEG4TYPE *pVideoMpeg4;
		pVideoMpeg4 = ComponentParameterStructure;
		portIndex = pVideoMpeg4->nPortIndex;
		eError =
			omx_base_component_ParameterSanityCheck
			(hComponent, portIndex, pVideoMpeg4,
			 sizeof(OMX_VIDEO_PARAM_MPEG4TYPE));
		if (eError != OMX_ErrorNone) {
			DEBUG(DEB_LEV_ERR,
			      "In %s Parameter Check Error=%x\n",
			      __func__, eError);
			break;
		}
		if (pVideoMpeg4->nPortIndex == 0) {
			memcpy(&shvpu_decode_Private->pVideoMpeg4,
			       pVideoMpeg4,
			       sizeof(OMX_VIDEO_PARAM_MPEG4TYPE));
		} else {
			return OMX_ErrorBadPortIndex;
		}
		break;
	}
	default:
		switch ((OMX_REVPU5INDEXTYPE)nParamIndex) {
		case OMX_IndexParamVPUMaxOutputSetting:
		{
			OMX_PARAM_REVPU5MAXPARAM *pMaxVals;
			if (shvpu_decode_Private->state != OMX_StateLoaded)
				return OMX_ErrorIncorrectStateOperation;
			pMaxVals = ComponentParameterStructure;
			eError = checkHeader(pMaxVals,
					     sizeof
					     (OMX_PARAM_REVPU5MAXPARAM));
			if (eError != OMX_ErrorNone)
				break;

			memcpy (&shvpu_decode_Private->maxVideoParameters,
				pMaxVals,
				sizeof(OMX_PARAM_REVPU5MAXPARAM));
			break;
		}
		case OMX_IndexParamVPUMaxInstance:
		{
			OMX_PARAM_REVPU5MAXINSTANCE *pMaxInst;
			if (shvpu_decode_Private->state != OMX_StateLoaded)
				return OMX_ErrorIncorrectStateOperation;
			pMaxInst = ComponentParameterStructure;
			eError = checkHeader(pMaxInst,
					     sizeof
					     (OMX_PARAM_REVPU5MAXINSTANCE));
			if (eError != OMX_ErrorNone)
				break;
			if (pMaxInst->nInstances <= MAX_COMPONENT_VIDEODEC) {
#ifdef VPU_VERSION_5
				if (pMaxInst->nInstances > 1)
					shvpu_decode_Private->
						enable_sync = OMX_TRUE;
#endif
				memcpy (&maxVPUInstances,
					pMaxInst,
					sizeof(OMX_PARAM_REVPU5MAXINSTANCE));
				break;
			} else {
				return OMX_ErrorBadParameter;
			}
		}
		case OMX_IndexParamSoftwareRenderMode:
		{
#ifdef TL_CONV_ENABLE
			shvpu_decode_Private->features.tl_conv_mode =
				!(*(OMX_BOOL *)ComponentParameterStructure);
#endif
#ifdef DMAC_MODE
			shvpu_decode_Private->features.dmac_mode =
				!(*(OMX_BOOL *)ComponentParameterStructure);
#endif
			shvpu_decode_Private->enable_sync = OMX_TRUE;

			logd("Switching software readable output mode %s\n",
			     (*(OMX_BOOL *)ComponentParameterStructure ==
			      OMX_FALSE) ?  "off" : "on");

			if (*(OMX_BOOL *)ComponentParameterStructure) {
				OMX_U32 *AvcLevel;
				AvcLevel = &shvpu_decode_Private->
					maxVideoParameters.eVPU5AVCLevel;
				*AvcLevel = *AvcLevel > OMX_VPU5AVCLevel4 ?
					OMX_VPU5AVCLevel4 : *AvcLevel;
			}
			break;
		}
#ifdef ANDROID_CUSTOM
		case OMX_IndexAndroidNativeEnable:
		{
			OMX_BOOL enable;
			omx_base_video_PortType *outPort;
			outPort = (omx_base_video_PortType *)
				shvpu_decode_Private->ports[
					OMX_BASE_FILTER_OUTPUTPORT_INDEX];

			eError = shvpu_avcdec_AndroidNativeBufferEnable(
				shvpu_decode_Private,
				ComponentParameterStructure);

			if (eError)
				break;

			enable = shvpu_decode_Private->
					android_native.native_buffer_enable;

			logd("Switching android native mode %s\n",
				enable? "on" : "off");
			if (enable)
				outPort->sPortParam.format.video.eColorFormat =
					OUTPUT_ANDROID_DECODED_COLOR_FMT;
			else
				outPort->sPortParam.format.video.eColorFormat =
					OUTPUT_DECODED_COLOR_FMT;
			break;
		}
		case OMX_IndexAndroidUseNativeBuffer:
		{
			if (shvpu_decode_Private->state != OMX_StateLoaded
				&& shvpu_decode_Private->state !=
				OMX_StateWaitForResources) {
				DEBUG(DEB_LEV_ERR,
					"In %s Incorrect State=%x lineno=%d\n",
					__func__, shvpu_decode_Private->state,
					__LINE__);
				return OMX_ErrorIncorrectStateOperation;
			}
			eError = shvpu_avcdec_UseAndroidNativeBuffer(
				shvpu_decode_Private,
				ComponentParameterStructure);
			break;
		}
#endif
		default:
			/*Call the base component function */
			return omx_base_component_SetParameter(hComponent,
							       nParamIndex,
							       ComponentParameterStructure);
		}
	}
	return eError;
}

OMX_ERRORTYPE
shvpu_avcdec_GetParameter(OMX_HANDLETYPE hComponent,
			  OMX_INDEXTYPE nParamIndex,
			  OMX_PTR ComponentParameterStructure)
{

	omx_base_video_PortType *port;
	OMX_ERRORTYPE eError = OMX_ErrorNone;

	OMX_COMPONENTTYPE *pComponent = hComponent;
	shvpu_decode_PrivateType *shvpu_decode_Private =
		pComponent->pComponentPrivate;
	if (ComponentParameterStructure == NULL) {
		return OMX_ErrorBadParameter;
	}
	DEBUG(DEB_LEV_SIMPLE_SEQ, "   Getting parameter %i\n", nParamIndex);
	/* Check which structure we are being fed and fill its header */
	switch (nParamIndex) {
	case OMX_IndexParamVideoInit:
		if ((eError =
		     checkHeader(ComponentParameterStructure,
				 sizeof(OMX_PORT_PARAM_TYPE))) !=
		    OMX_ErrorNone) {
			break;
		}
		memcpy(ComponentParameterStructure,
		       &shvpu_decode_Private->sPortTypesParam
		       [OMX_PortDomainVideo], sizeof(OMX_PORT_PARAM_TYPE));
		break;
	case OMX_IndexParamVideoPortFormat:
	{
		OMX_VIDEO_PARAM_PORTFORMATTYPE *pVideoPortFormat;
		pVideoPortFormat = ComponentParameterStructure;
		if ((eError =
		     checkHeader(ComponentParameterStructure,
				 sizeof
				 (OMX_VIDEO_PARAM_PORTFORMATTYPE))) !=
		    OMX_ErrorNone) {
			break;
		}
		if (pVideoPortFormat->nPortIndex <= 1) {
			if (pVideoPortFormat->nIndex > 0)
				return OMX_ErrorNoMore;
			port = (omx_base_video_PortType *)
				shvpu_decode_Private->ports
				[pVideoPortFormat->nPortIndex];
			memcpy(pVideoPortFormat, &port->sVideoParam,
			       sizeof
			       (OMX_VIDEO_PARAM_PORTFORMATTYPE));
		} else {
			return OMX_ErrorBadPortIndex;
		}
		break;
	}
	case OMX_IndexParamVideoAvc:
	{
		OMX_VIDEO_PARAM_AVCTYPE *pVideoAvc;
		pVideoAvc = ComponentParameterStructure;
		if (pVideoAvc->nPortIndex != 0) {
			return OMX_ErrorBadPortIndex;
		}
		if ((eError =
		     checkHeader(ComponentParameterStructure,
				 sizeof(OMX_VIDEO_PARAM_AVCTYPE))) !=
		    OMX_ErrorNone) {
			break;
		}
		memcpy(pVideoAvc, &shvpu_decode_Private->pVideoAvc,
		       sizeof(OMX_VIDEO_PARAM_AVCTYPE));
		break;
	}
	case OMX_IndexParamVideoProfileLevelQuerySupported:
	{
		OMX_VIDEO_PARAM_PROFILELEVELTYPE *pAVCProfile;
		pAVCProfile = ComponentParameterStructure;
		if ((eError = checkHeader(pAVCProfile,
			sizeof(OMX_VIDEO_PARAM_PROFILELEVELTYPE))) !=
				OMX_ErrorNone) {
			break;
		}
		if (pAVCProfile->nPortIndex != 0) {
			return OMX_ErrorBadPortIndex;
		}
		if (pAVCProfile->nProfileIndex < AVC_PROFILE_COUNT) {
			memcpy(pAVCProfile,
				&shvpu_decode_Private->pVideoProfile[pAVCProfile->nProfileIndex],
				sizeof (OMX_VIDEO_PARAM_PROFILELEVELTYPE));
		} else {
			return OMX_ErrorNoMore;
		}
		break;
	}
	case OMX_IndexParamVideoProfileLevelCurrent:
	{
		OMX_VIDEO_PARAM_PROFILELEVELTYPE *pAVCProfile;
		pAVCProfile = ComponentParameterStructure;
		if ((eError = checkHeader(pAVCProfile,
			sizeof(OMX_VIDEO_PARAM_PROFILELEVELTYPE))) !=
				OMX_ErrorNone) {
			break;
		}
		if (pAVCProfile->nPortIndex != 0) {
			return OMX_ErrorBadPortIndex;
		}
		memcpy(pAVCProfile,
			&shvpu_decode_Private->pVideoCurrentProfile,
				sizeof (OMX_VIDEO_PARAM_PROFILELEVELTYPE));
		break;
	}
	case OMX_IndexParamStandardComponentRole:
	{
		OMX_PARAM_COMPONENTROLETYPE *pComponentRole;
		pComponentRole = ComponentParameterStructure;
		if ((eError =
		     checkHeader(ComponentParameterStructure,
				 sizeof(OMX_PARAM_COMPONENTROLETYPE)))
		    != OMX_ErrorNone) {
			break;
		}
		if (shvpu_decode_Private->video_coding_type ==
		    OMX_VIDEO_CodingMPEG4) {
			strcpy((char *)pComponentRole->cRole,
			       VIDEO_DEC_MPEG4_ROLE);
		} else if (shvpu_decode_Private->video_coding_type ==
			   OMX_VIDEO_CodingAVC) {
			strcpy((char *)pComponentRole->cRole,
			       VIDEO_DEC_H264_ROLE);
		} else {
			strcpy((char *)pComponentRole->cRole, "\0");
		}
		break;
	}
	default:
		switch ((OMX_REVPU5INDEXTYPE)nParamIndex) {
		case OMX_IndexParamVPUMaxOutputSetting:
		{
			OMX_PARAM_REVPU5MAXPARAM *pMaxVals;
			pMaxVals = ComponentParameterStructure;
			eError = checkHeader(pMaxVals,
					     sizeof
					     (OMX_PARAM_REVPU5MAXPARAM));
			if (eError != OMX_ErrorNone)
				break;

			memcpy (pMaxVals,
				&shvpu_decode_Private->maxVideoParameters,
				sizeof(OMX_PARAM_REVPU5MAXPARAM));
			break;
		}
		case OMX_IndexParamVPUMaxInstance:
		{
			OMX_PARAM_REVPU5MAXINSTANCE *pMaxInst;
			pMaxInst = ComponentParameterStructure;
			eError = checkHeader(pMaxInst,
					     sizeof
					     (OMX_PARAM_REVPU5MAXINSTANCE));
			if (eError != OMX_ErrorNone)
				break;

			memcpy (pMaxInst, &maxVPUInstances,
				sizeof(OMX_PARAM_REVPU5MAXINSTANCE));
			break;
		}
		case OMX_IndexParamQueryIPMMUEnable:
		{
			OMX_PARAM_REVPU5IPMMUSTATUS *pIpmmuEnable;
			pIpmmuEnable = ComponentParameterStructure;
			eError = checkHeader(pIpmmuEnable,
					     sizeof
					     (OMX_PARAM_REVPU5IPMMUSTATUS));
			if (eError != OMX_ErrorNone)
				break;
#ifdef TL_CONV_ENABLE
			pIpmmuEnable->bIpmmuEnable = OMX_TRUE;
#else
			pIpmmuEnable->bIpmmuEnable = OMX_FALSE;
#endif
			break;
		}
		case OMX_IndexParamSoftwareRenderMode:
		{
			*(OMX_BOOL *)ComponentParameterStructure =
				!shvpu_decode_Private->features.tl_conv_mode;
			break;
		}
#ifdef ANDROID_CUSTOM
		case OMX_IndexAndroidGetNativeBufferUsage:
		{
			eError = shvpu_avcdec_GetNativeBufferUsage(
				shvpu_decode_Private,
				ComponentParameterStructure);
			break;
		}
#endif
		default:
		/*Call the base component function */
		return omx_base_component_GetParameter(hComponent,
						       nParamIndex,
						       ComponentParameterStructure);
		}
	}
	return eError;
}

/** GetConfig
  * Right now we don't support any configuration, so return
  * OMX_ErrBadParameter for any request that we get
  */
OMX_ERRORTYPE
shvpu_avcdec_GetConfig(OMX_HANDLETYPE hComponent,
		       OMX_INDEXTYPE nIndex,
		       OMX_PTR pComponentConfigStructure)  {
	return OMX_ErrorBadParameter;
}

OMX_ERRORTYPE
shvpu_avcdec_GetExtensionIndex(OMX_HANDLETYPE hComponent,
				OMX_STRING cParameterName,
				OMX_INDEXTYPE *pIndexType) {
	if (!cParameterName || !pIndexType)
		return OMX_ErrorBadParameter;
	return lookup_ExtensionIndex(cParameterName, pIndexType);
}

OMX_ERRORTYPE
shvpu_avcdec_MessageHandler(OMX_COMPONENTTYPE * pComponent,
			    internalRequestMessageType * message)
{
	shvpu_decode_PrivateType *shvpu_decode_Private =
		(shvpu_decode_PrivateType *) pComponent->pComponentPrivate;
	OMX_ERRORTYPE err;

	DEBUG(DEB_LEV_FUNCTION_NAME, "In %s\n", __func__);

	if (message->messageType == OMX_CommandStateSet) {
		switch(shvpu_decode_Private->state) {
		case OMX_StateIdle:
			if (message->messageParam == OMX_StateExecuting) {
				shvpu_decode_Private->isFirstBuffer = OMX_TRUE;
			} else if (message->messageParam == OMX_StateLoaded) {
				err = shvpu_avcdec_Deinit(pComponent);
				if (err != OMX_ErrorNone) {
					DEBUG(DEB_LEV_ERR,
						"In %s Video Decoder Deinit"
						"Failed Error=%x\n",
						__func__, err);
					return err;
				}
			}
			break;
		case OMX_StateLoaded:
			if (message->messageParam == OMX_StateIdle) {
				err = omx_base_component_MessageHandler
					(pComponent, message);
				if (err != OMX_ErrorNone)
					return err;
				err = shvpu_avcdec_Init(pComponent);
				if (err != OMX_ErrorNone) {
					DEBUG(DEB_LEV_ERR,
						"In %s Video Decoder Init"
						"Failed Error=%x\n",
						__func__, err);
					return err;
				}
				shvpu_decode_Private->avcodecReady = OMX_TRUE;
				return err;
			}
			break;
		default:
			break;
		}
	}
	// Execute the base message handling
	err = omx_base_component_MessageHandler(pComponent, message);

	return err;
}

OMX_ERRORTYPE
shvpu_avcdec_ComponentRoleEnum(OMX_HANDLETYPE hComponent, OMX_U8 * cRole,
			       OMX_U32 nIndex)
{

	if (nIndex == 0) {
		strcpy((char *)cRole, VIDEO_DEC_MPEG4_ROLE);
	} else if (nIndex == 1) {
		strcpy((char *)cRole, VIDEO_DEC_H264_ROLE);
	} else {
		return OMX_ErrorUnsupportedIndex;
	}
	return OMX_ErrorNone;
}
OMX_ERRORTYPE
shvpu_avcdec_SendCommand(
  OMX_HANDLETYPE hComponent,
  OMX_COMMANDTYPE Cmd,
  OMX_U32 nParam,
  OMX_PTR pCmdData) {
  OMX_ERRORTYPE err;
  OMX_COMPONENTTYPE* pComponent = (OMX_COMPONENTTYPE*)hComponent;
  shvpu_decode_PrivateType* shvpu_decode_Private =
		pComponent->pComponentPrivate;
  if ((Cmd == OMX_CommandStateSet) && (nParam == OMX_StateIdle) &&
      (shvpu_decode_Private->state == OMX_StateLoaded)) {
    err = shvpu_avcdec_vpuLibInit(shvpu_decode_Private);
    if (err != OMX_ErrorNone) {
	DEBUG(DEB_LEV_ERR, "In %s shvpu_avcdec_vpuLibInit Failed\n", __func__);
        return err;
    }
  }
  if ((Cmd == OMX_CommandStateSet) && (nParam == OMX_StateExecuting) &&
      (shvpu_decode_Private->state == OMX_StateIdle) &&
      (shvpu_decode_Private->features.dmac_mode)) {

    /* Input port holds the dimensions of the input data stream, while the
       output port has its size adjusted to meet requirements of downstream
       components/devices */
    omx_base_video_PortType *inPort =
               (omx_base_video_PortType *)
               shvpu_decode_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
    DMAC_setup_buffers(inPort->sPortParam.format.video.nFrameWidth,
	   inPort->sPortParam.format.video.nFrameHeight,
	   shvpu_decode_Private->features.tl_conv_mode);
  }
  return omx_base_component_SendCommand(hComponent, Cmd, nParam, pCmdData);
}

OMX_ERRORTYPE
shvpu_avcdec_port_AllocateOutBuffer(
  omx_base_PortType *pPort,
  OMX_BUFFERHEADERTYPE** pBuffer,
  OMX_U32 nPortIndex,
  OMX_PTR pAppPrivate,
  OMX_U32 nSizeBytes) {

  unsigned int i;
  OMX_COMPONENTTYPE* pComponent = pPort->standCompContainer;
  shvpu_decode_PrivateType* shvpu_decode_Private =
		pComponent->pComponentPrivate;
  DEBUG(DEB_LEV_FUNCTION_NAME, "In %s for port %x\n", __func__, (int)pPort);

  if (nPortIndex != pPort->sPortParam.nPortIndex) {
    return OMX_ErrorBadPortIndex;
  }

  if (shvpu_decode_Private->transientState != OMX_TransStateLoadedToIdle){
    if (!pPort->bIsTransientToEnabled) {
      DEBUG(DEB_LEV_ERR, "In %s: The port is not allowed to receive buffers\n",
		__func__);
      return OMX_ErrorIncorrectStateTransition;
    }
  }

  for(i=0; i < pPort->sPortParam.nBufferCountActual; i++){
    if (pPort->bBufferStateAllocated[i] == BUFFER_FREE) {
      pPort->pInternalBufferStorage[i] = calloc(1,sizeof(OMX_BUFFERHEADERTYPE));
      if (!pPort->pInternalBufferStorage[i]) {
        return OMX_ErrorInsufficientResources;
      }
      setHeader(pPort->pInternalBufferStorage[i], sizeof(OMX_BUFFERHEADERTYPE));
      /* allocate the buffer */
      pPort->pInternalBufferStorage[i]->pBuffer =
		shvpu_decode_Private->uio_start;
     if(pPort->pInternalBufferStorage[i]->pBuffer == NULL) {
        return OMX_ErrorInsufficientResources;
      }
      pPort->pInternalBufferStorage[i]->nAllocLen =
		shvpu_decode_Private->uio_size;
      pPort->pInternalBufferStorage[i]->pPlatformPrivate =
		(void *)shvpu_decode_Private->uio_start_phys;
      pPort->pInternalBufferStorage[i]->pAppPrivate = pAppPrivate;
      *pBuffer = pPort->pInternalBufferStorage[i];
      pPort->bBufferStateAllocated[i] = BUFFER_ALLOCATED;
      pPort->bBufferStateAllocated[i] |= HEADER_ALLOCATED;
      pPort->pInternalBufferStorage[i]->nOutputPortIndex =
		pPort->sPortParam.nPortIndex;
      pPort->nNumAssignedBuffers++;
      DEBUG(DEB_LEV_PARAMS, "pPort->nNumAssignedBuffers %i\n",
		(int)pPort->nNumAssignedBuffers);

      if (pPort->sPortParam.nBufferCountActual == pPort->nNumAssignedBuffers) {
        pPort->sPortParam.bPopulated = OMX_TRUE;
        pPort->bIsFullOfBuffers = OMX_TRUE;
        DEBUG(DEB_LEV_SIMPLE_SEQ, "In %s nPortIndex=%d\n",__func__,
		(int)nPortIndex);
        tsem_up(pPort->pAllocSem);
      }
      DEBUG(DEB_LEV_FUNCTION_NAME, "Out of %s for port %x\n", __func__,
		(int)pPort);
      return OMX_ErrorNone;
    }
  }
  DEBUG(DEB_LEV_ERR, "Out of %s for port %x. Error: no available buffers\n",
		__func__, (int)pPort);
  return OMX_ErrorInsufficientResources;
}

OMX_ERRORTYPE shvpu_avcdec_port_UseBuffer(
  omx_base_PortType *outPort,
  OMX_BUFFERHEADERTYPE** ppBufferHdr,
  OMX_U32 nPortIndex,
  OMX_PTR pAppPrivate,
  OMX_U32 nSizeBytes,
  OMX_U8* pBuffer) {

  OMX_ERRORTYPE ret;

 unsigned int i;
  OMX_COMPONENTTYPE* omxComponent = outPort->standCompContainer;
  shvpu_decode_PrivateType* shvpu_decode_Private =
		omxComponent->pComponentPrivate;

  DEBUG(DEB_LEV_FUNCTION_NAME, "In %s for port %x\n", __func__,
	(int)outPort);

  if (nPortIndex != outPort->sPortParam.nPortIndex) {
    return OMX_ErrorBadPortIndex;
  }

  if (shvpu_decode_Private->transientState != OMX_TransStateLoadedToIdle) {
    if (!outPort->bIsTransientToEnabled) {
      DEBUG(DEB_LEV_ERR, "In %s: The port of Comp %s is not allowed to"
		" receive buffers\n", __func__,shvpu_decode_Private->name);
      return OMX_ErrorIncorrectStateTransition;
    }
  }
  if(nSizeBytes < outPort->sPortParam.nBufferSize) {
    DEBUG(DEB_LEV_ERR, "In %s: Port %d Given Buffer Size %u is less than"
	" Minimum Buffer Size %u\n", __func__, (int)nPortIndex,
	(int)nSizeBytes, (int)outPort->sPortParam.nBufferSize);
    return OMX_ErrorBadParameter;
  }
 for(i=0; i < outPort->sPortParam.nBufferCountActual; i++){
    if (outPort->bBufferStateAllocated[i] == BUFFER_FREE) {
      outPort->pInternalBufferStorage[i] =
		calloc(1,sizeof(OMX_BUFFERHEADERTYPE));
      if (!outPort->pInternalBufferStorage[i]) {
        return OMX_ErrorInsufficientResources;
      }

      outPort->bIsEmptyOfBuffers = OMX_FALSE;
      setHeader(outPort->pInternalBufferStorage[i],
		sizeof(OMX_BUFFERHEADERTYPE));

      outPort->pInternalBufferStorage[i]->pBuffer = pBuffer;
      outPort->pInternalBufferStorage[i]->nAllocLen = nSizeBytes;
      if (shvpu_decode_Private->features.dmac_mode) {
          ipmmui_buffer_map_vaddr(pBuffer, nSizeBytes,
		(unsigned long *)&outPort->pInternalBufferStorage[i]->
		pPlatformPrivate);
      }

      outPort->pInternalBufferStorage[i]->pAppPrivate = pAppPrivate;
      outPort->bBufferStateAllocated[i] = BUFFER_ASSIGNED;
      outPort->bBufferStateAllocated[i] |= HEADER_ALLOCATED;
      if (outPort->sPortParam.eDir == OMX_DirInput) {
        outPort->pInternalBufferStorage[i]->nInputPortIndex =
		outPort->sPortParam.nPortIndex;
      } else {
        outPort->pInternalBufferStorage[i]->nOutputPortIndex =
		outPort->sPortParam.nPortIndex;
      }
      *ppBufferHdr = outPort->pInternalBufferStorage[i];
      outPort->nNumAssignedBuffers++;
      DEBUG(DEB_LEV_PARAMS, "outPort->nNumAssignedBuffers %i\n",
		(int)outPort->nNumAssignedBuffers);

      if (outPort->sPortParam.nBufferCountActual ==
		outPort->nNumAssignedBuffers) {
        outPort->sPortParam.bPopulated = OMX_TRUE;
        outPort->bIsFullOfBuffers = OMX_TRUE;
        tsem_up(outPort->pAllocSem);
      }
      DEBUG(DEB_LEV_FUNCTION_NAME, "Out of %s for port %x\n", __func__,
	(int)outPort);
      return OMX_ErrorNone;
    }
  }
  return OMX_ErrorInsufficientResources;
}

/*Unlike the base port, we will free the specific buffer requested
  even though we allocated it ourselves*/
OMX_ERRORTYPE
shvpu_avcdec_port_FreeBuffer(
  omx_base_PortType *pPort,
  OMX_U32 nPortIndex,
  OMX_BUFFERHEADERTYPE* pBuffer) {
  unsigned int i;
  OMX_COMPONENTTYPE* omxComponent = pPort->standCompContainer;
  shvpu_decode_PrivateType* shvpu_decode_Private = (shvpu_decode_PrivateType*)omxComponent->pComponentPrivate;
  DEBUG(DEB_LEV_FUNCTION_NAME, "In %s for port %x\n", __func__, (int)pPort);

  if (nPortIndex != pPort->sPortParam.nPortIndex) {
    return OMX_ErrorBadPortIndex;
  }

  if (shvpu_decode_Private->transientState != OMX_TransStateIdleToLoaded) {
    if (!pPort->bIsTransientToDisabled) {
      DEBUG(DEB_LEV_FULL_SEQ, "In %s: The port is not allowed to free the buffers\n", __func__);
      (*(shvpu_decode_Private->callbacks->EventHandler))
        (omxComponent,
        shvpu_decode_Private->callbackData,
        OMX_EventError, /* The command was completed */
        OMX_ErrorPortUnpopulated, /* The commands was a OMX_CommandStateSet */
        nPortIndex, /* The state has been changed in message->messageParam2 */
        NULL);
    }
  }
  for(i=0; i < pPort->sPortParam.nBufferCountActual; i++){
    if(pPort->pInternalBufferStorage[i] == pBuffer) {
      pPort->bIsFullOfBuffers = OMX_FALSE;
      if(pPort->bBufferStateAllocated[i] & BUFFER_ALLOCATED)
        pBuffer->pBuffer = NULL; /* we don't actually allocate anything */
      else if (pPort->bBufferStateAllocated[i] & BUFFER_ASSIGNED)
	if (shvpu_decode_Private->features.dmac_mode)
	    ipmmui_buffer_unmap_vaddr(pBuffer->pBuffer);
      if(pPort->bBufferStateAllocated[i] & HEADER_ALLOCATED) {
        free(pPort->pInternalBufferStorage[i]);
        pPort->bBufferStateAllocated[i] = BUFFER_FREE;
      }
      pPort->nNumAssignedBuffers--;

      if (pPort->nNumAssignedBuffers == 0) {
        pPort->sPortParam.bPopulated = OMX_FALSE;
        pPort->bIsEmptyOfBuffers = OMX_TRUE;
        tsem_up(pPort->pAllocSem);
      }
      return OMX_ErrorNone;
    }
  }
  return OMX_ErrorInsufficientResources;
}
