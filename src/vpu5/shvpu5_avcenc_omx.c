/**
   src/vpu5/shvpu5_avcenc_omx.c

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
#include "shvpu5_avcenc_omx.h"
#include "shvpu5_avcenc.h"
#include "shvpu5_common_log.h"
#include <OMX_Video.h>

/** Maximum Number of Video Component Instance*/
#define MAX_COMPONENT_VIDEOENC 1

/** Counter of Video Component Instance*/
static OMX_U32 noVideoEncInstance = 0;

#define DEFAULT_WIDTH 128
#define DEFAULT_HEIGHT 96
/** define the minimum input buffer size */
#define DEFAULT_VIDEO_INPUT_BUF_SIZE					\
	(DEFAULT_WIDTH * DEFAULT_HEIGHT * 3 / 2)	// YUV subQCIF
#define DEFAULT_VIDEO_OUTPUT_BUF_SIZE	(1024 * 1024)	// 1MiB

#define INPUT_BUFFER_COUNT 2
#define INPUT_PICTURE_COLOR_FMT		OMX_COLOR_FormatYUV420SemiPlanar

//#undef DEBUG_LEVEL
//#define DEBUG_LEVEL DEB_ALL_MESS
static void *
shvpu_avcenc_BufferMgmtFunction(void *param);
static void
SetInternalVideoParameters(OMX_COMPONENTTYPE * pComponent);

/** The Constructor of the video encoder component
 * @param pComponent the component handle to be constructed
 * @param cComponentName is the name of the constructed component
 */
OMX_ERRORTYPE
shvpu_avcenc_Constructor(OMX_COMPONENTTYPE * pComponent,
			 OMX_STRING cComponentName)
{

	OMX_ERRORTYPE eError = OMX_ErrorNone;
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private;
	omx_base_video_PortType *inPort, *outPort;
	OMX_U32 i;

	/*
	 * initialize component private data
	 */
	if (!pComponent->pComponentPrivate) {
		DEBUG(DEB_LEV_FUNCTION_NAME, "In %s, allocating component\n",
		      __func__);
		pComponent->pComponentPrivate =
			calloc(1, sizeof(shvpu_avcenc_PrivateType));
		if (pComponent->pComponentPrivate == NULL) {
			return OMX_ErrorInsufficientResources;
		}
	} else {
		DEBUG(DEB_LEV_FUNCTION_NAME,
		      "In %s, Error Component %x Already Allocated\n",
		      __func__, (int)pComponent->pComponentPrivate);
	}

	shvpu_avcenc_Private = pComponent->pComponentPrivate;
	shvpu_avcenc_Private->ports = NULL;

	/*
	 * construct base filter
	 */
	eError = omx_base_filter_Constructor(pComponent, cComponentName);

	shvpu_avcenc_Private->
		sPortTypesParam[OMX_PortDomainVideo].nStartPortNumber = 0;
	shvpu_avcenc_Private->sPortTypesParam[OMX_PortDomainVideo].nPorts = 2;

	/*
	 * Allocate Ports and call port constructor.
	 */
	if (shvpu_avcenc_Private->sPortTypesParam[OMX_PortDomainVideo].nPorts
	    && !shvpu_avcenc_Private->ports) {
		shvpu_avcenc_Private->ports =
			calloc(shvpu_avcenc_Private->sPortTypesParam
			       [OMX_PortDomainVideo].nPorts,
			       sizeof(omx_base_PortType *));
		if (!shvpu_avcenc_Private->ports) {
			return OMX_ErrorInsufficientResources;
		}
		for (i = 0;
		     i <shvpu_avcenc_Private->sPortTypesParam
			     [OMX_PortDomainVideo].nPorts; i++) {
			shvpu_avcenc_Private->ports[i] =
				calloc(1, sizeof(omx_base_video_PortType));
			if (!shvpu_avcenc_Private->ports[i]) {
				return OMX_ErrorInsufficientResources;
			}
		}
	}

	base_video_port_Constructor(pComponent,
				    &shvpu_avcenc_Private->ports[0], 0,
				    OMX_TRUE);
	base_video_port_Constructor(pComponent,
				    &shvpu_avcenc_Private->ports[1], 1,
				    OMX_FALSE);

	/*
	 * set the parameter common to input/output ports
	 */
	inPort = (omx_base_video_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	inPort->sPortParam.format.video.nFrameWidth = DEFAULT_WIDTH;
	inPort->sPortParam.format.video.nFrameHeight = DEFAULT_HEIGHT;
	inPort->sPortParam.nBufferSize = DEFAULT_VIDEO_INPUT_BUF_SIZE;
	inPort->sPortParam.nBufferCountMin = INPUT_BUFFER_COUNT;
	inPort->sPortParam.nBufferCountActual = INPUT_BUFFER_COUNT;
	inPort->sPortParam.format.video.xFramerate = 0;
	inPort->sPortParam.format.video.nBitrate = 0;
	inPort->sPortParam.format.video.eColorFormat =
		INPUT_PICTURE_COLOR_FMT;
	inPort->sPortParam.format.video.eCompressionFormat =
		OMX_VIDEO_CodingAVC;

	inPort->sVideoParam.eColorFormat = INPUT_PICTURE_COLOR_FMT;

	outPort = (omx_base_video_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	outPort->sPortParam.nBufferSize = DEFAULT_VIDEO_OUTPUT_BUF_SIZE;
	outPort->sPortParam.format.video.xFramerate = 0;
	outPort->sVideoParam.eCompressionFormat = OMX_VIDEO_CodingAVC;

	SetInternalVideoParameters(pComponent);

	/*
	 * general configuration irrespective of any video formats
	 * setting other parameters of shvpu_avcenc_private
	 */
	shvpu_avcenc_Private->avcodecReady = OMX_FALSE;

	/* initializing the queue for buffer metadata */
	shvpu_avcenc_Private->pBMIQueue =
		(queue_t *)calloc(1, sizeof(queue_t));
	if (shvpu_avcenc_Private->pBMIQueue == (queue_t *)NULL)
			return OMX_ErrorInsufficientResources;
	shvpu_queue_init(shvpu_avcenc_Private->pBMIQueue);

	/** initializing the codec context etc that was done earlier
	    by vpulibinit function */
	shvpu_avcenc_Private->BufferMgmtFunction =
		shvpu_avcenc_BufferMgmtFunction;
	shvpu_avcenc_Private->messageHandler = shvpu_avcenc_MessageHandler;
	shvpu_avcenc_Private->destructor = shvpu_avcenc_Destructor;
	pComponent->SetParameter = shvpu_avcenc_SetParameter;
	pComponent->GetParameter = shvpu_avcenc_GetParameter;
	pComponent->ComponentRoleEnum = shvpu_avcenc_ComponentRoleEnum;
	//pComponent->GetExtensionIndex = shvpu_avcenc_GetExtensionIndex;

	/* set up default encodeing parameters */
	shvpu_avcenc_Private->avCodec = encode_new();
	if (!shvpu_avcenc_Private->avCodec)
		return OMX_ErrorInsufficientResources;

	/* set a private buffer allocator for input buffer */
	inPort->Port_AllocateBuffer = shvpu_avcenc_AllocateBuffer;
	inPort->Port_FreeBuffer = shvpu_avcenc_FreeBuffer;

	noVideoEncInstance++;

	if (noVideoEncInstance > MAX_COMPONENT_VIDEOENC) {
		return OMX_ErrorInsufficientResources;
	}

	/* initialize a vpu uio */
	unsigned long reg, mem;
	size_t memsz;
	uio_init("VPU", &reg, &mem, &memsz);
	loge("reg = %x, mem = %x, memsz = %d\n",
	     reg, mem, memsz);

	return eError;
}

/** The destructor of the video encoder component
 */
OMX_ERRORTYPE
shvpu_avcenc_Destructor(OMX_COMPONENTTYPE * pComponent)
{
	omx_base_filter_Destructor(pComponent);
	noVideoEncInstance--;
	uio_deinit();

	return OMX_ErrorNone;
}

/** It initializates the VPU framework, and opens an VPU videodecoder
    of type specified by IL client
*/
OMX_ERRORTYPE
shvpu_avcenc_vpuLibInit(shvpu_avcenc_PrivateType * shvpu_avcenc_Private)
{
	omx_base_video_PortType *inPort;
	shvpu_codec_t *pCodec = shvpu_avcenc_Private->avCodec;
	int ret, i;
	void *vaddr;

	DEBUG(DEB_LEV_SIMPLE_SEQ, "VPU library/codec initializing..\n");
	inPort = (omx_base_video_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
#if 0
	width = inPort->sPortParam.format.video.nFrameWidth;
	height = inPort->sPortParam.format.video.nFrameHeight;
	framerate = inPort->sPortParam.format.video.xFramerate;
	bitrate = inPort->sPortParam.format.video.nBitrate;
#endif

        /* initialize the encoder middleware */
	ret = encode_init(pCodec);
	if (ret != MCVENC_NML_END) {
		loge("encode_init() failed (%ld)\n", ret);
		return OMX_ErrorInsufficientResources;
	}
	shvpu_avcenc_Private->avCodec = pCodec;
	pCodec->pDriver->lastOutput = -1;

	/* set Q matrix if High profile selected */
	if (shvpu_avcenc_Private->
	    avcType.eProfile == OMX_VIDEO_AVCProfileHigh) {
		encode_setqmatrix(pCodec->pContext);
	}

	/* prepare output buffers for VPU M/W */
	for (i=0; i<SHVPU_AVCENC_OUTBUF_NUM; i++) {
		vaddr = pmem_alloc(SHVPU_AVCENC_OUTBUF_SIZE, 256, NULL);
		if (vaddr == NULL) {
			printf("%s: pmem_alloc failed\n", __FUNCTION__);
			return OMX_ErrorInsufficientResources;
		}
		pCodec->streamBuffer[i].bufferInfo.buff_addr = vaddr;
       		pCodec->streamBuffer[i].bufferInfo.buff_size =
			SHVPU_AVCENC_OUTBUF_SIZE;
		pCodec->streamBuffer[i].bufferInfo.strm_size = 0;
		pCodec->streamBuffer[i].status = SHVPU_BUFFER_STATUS_READY;
	}

	return OMX_ErrorNone;
}

/** It Deinitializates the vpu framework, and close the vpu video
    decoder of selected coding type
*/
void
shvpu_avcenc_vpuLibDeInit(shvpu_avcenc_PrivateType *
			  shvpu_avcenc_Private)
{
	shvpu_codec_t *pCodec = shvpu_avcenc_Private->avCodec;

	encode_deinit(pCodec);
	shvpu_driver_deinit(pCodec->pDriver);
	pCodec->pDriver = NULL;

	DEBUG(DEB_LEV_SIMPLE_SEQ, "VPU library/codec de-initialized\n");
}

/** internal function to set codec related parameters in the private
    type structure
*/
static void
SetInternalVideoParameters(OMX_COMPONENTTYPE * pComponent)
{
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private;
	OMX_VIDEO_PARAM_AVCTYPE *pAvcType;
	OMX_VIDEO_PARAM_BITRATETYPE *pBRType;

	shvpu_avcenc_Private = pComponent->pComponentPrivate;;

	strcpy(shvpu_avcenc_Private->ports
	       [OMX_BASE_FILTER_OUTPUTPORT_INDEX]->sPortParam.format.
	       video.cMIMEType, "video/avc(h264)");
	shvpu_avcenc_Private->
		ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX]->sPortParam.format.
		video.eCompressionFormat = OMX_VIDEO_CodingAVC;

	setHeader(&shvpu_avcenc_Private->avcType,
		  sizeof(OMX_VIDEO_PARAM_AVCTYPE));
	pAvcType = &shvpu_avcenc_Private->avcType;
	pAvcType->nPortIndex = 0;
	pAvcType->nSliceHeaderSpacing = 0;
	pAvcType->nPFrames = 0;
	pAvcType->nBFrames = 0;
	pAvcType->bUseHadamard = OMX_FALSE;
	pAvcType->nRefFrames = 2;
	pAvcType->eProfile = OMX_VIDEO_AVCProfileBaseline;
	pAvcType->eLevel = OMX_VIDEO_AVCLevel1;
	pAvcType->nAllowedPictureTypes = 0;
	pAvcType->bFrameMBsOnly = OMX_FALSE;
	pAvcType->nRefIdx10ActiveMinus1 = 0;
	pAvcType->nRefIdx11ActiveMinus1 = 0;
	pAvcType->bEnableUEP = OMX_FALSE;
	pAvcType->bEnableFMO = OMX_FALSE;
	pAvcType->bEnableASO = OMX_FALSE;
	pAvcType->bEnableRS = OMX_FALSE;

	pAvcType->bMBAFF = OMX_FALSE;
	pAvcType->bEntropyCodingCABAC =	OMX_FALSE;
	pAvcType->bWeightedPPrediction = OMX_FALSE;
	pAvcType->nWeightedBipredicitonMode = 0;
	pAvcType->bconstIpred = OMX_FALSE;
	pAvcType->bDirect8x8Inference =	OMX_FALSE;
	pAvcType->bDirectSpatialTemporal = OMX_FALSE;
	pAvcType->nCabacInitIdc = 0;
	pAvcType->eLoopFilterMode = OMX_VIDEO_AVCLoopFilterDisable;

	setHeader(&shvpu_avcenc_Private->bitrateType,
		  sizeof(OMX_VIDEO_PARAM_BITRATETYPE));
	pBRType = &shvpu_avcenc_Private->bitrateType;
	pBRType->eControlRate = OMX_Video_ControlRateConstant;
	pBRType->nTargetBitrate = 0;
}

/** The Initialization function of the video encoder
 */
OMX_ERRORTYPE
shvpu_avcenc_Init(OMX_COMPONENTTYPE * pComponent)
{

	shvpu_avcenc_PrivateType *shvpu_avcenc_Private =
		pComponent->pComponentPrivate;
	OMX_ERRORTYPE eError = OMX_ErrorNone;

	/** Temporary First Output buffer size */
	shvpu_avcenc_Private->isFirstBuffer = OMX_TRUE;
	shvpu_avcenc_Private->isNewBuffer = 1;

	return eError;
}

/** The Deinitialization function of the video encoder
 */
OMX_ERRORTYPE
shvpu_avcenc_Deinit(OMX_COMPONENTTYPE * pComponent)
{

	shvpu_avcenc_PrivateType *shvpu_avcenc_Private =
		pComponent->pComponentPrivate;
	OMX_ERRORTYPE eError = OMX_ErrorNone;

	if (shvpu_avcenc_Private->avcodecReady) {
		shvpu_avcenc_vpuLibDeInit(shvpu_avcenc_Private);
		shvpu_avcenc_Private->avcodecReady = OMX_FALSE;
	}

	return eError;
}

static OMX_ERRORTYPE
shvpu_avcenc_checkParameters(shvpu_codec_t *pCodec) {
	if (pCodec->cmnProp.B_pic_mode) {
		if (pCodec->avcOpt.sps_profile_idc == AVCENC_BASELINE)
			return OMX_ErrorUnsupportedSetting;
	}
	return OMX_ErrorNone;
}


static inline OMX_ERRORTYPE
handleState_IdletoExecuting(OMX_COMPONENTTYPE * pComponent)
{
	OMX_ERRORTYPE err;
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private =
		(shvpu_avcenc_PrivateType *) pComponent->pComponentPrivate;
	err = shvpu_avcenc_checkParameters(shvpu_avcenc_Private->avCodec);
	if (err != OMX_ErrorNone)
		return err;

	shvpu_avcenc_Private->isFirstBuffer = OMX_TRUE;
	return OMX_ErrorNone;
}

static inline OMX_ERRORTYPE
handleState_IdletoLoaded(OMX_COMPONENTTYPE * pComponent)
{
	OMX_ERRORTYPE err;

	err = shvpu_avcenc_Deinit(pComponent);
	if (err != OMX_ErrorNone) {
		DEBUG(DEB_LEV_ERR,
		      "In %s Video Encoder DeinitFailed Error=%x\n",
		      __func__, err);
	}

	return err;
}

static inline OMX_ERRORTYPE
handleState_LoadedtoIdle(OMX_COMPONENTTYPE * pComponent,
			internalRequestMessageType * message)
{
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private =
		(shvpu_avcenc_PrivateType *) pComponent->pComponentPrivate;
	OMX_ERRORTYPE err;

	err = shvpu_avcenc_vpuLibInit(shvpu_avcenc_Private);
	if (err != OMX_ErrorNone) {
		DEBUG(DEB_LEV_ERR,
		      "In %s shvpu_avcenc_vpuLibInit Failed\n",
		      __func__);
		return err;
	}

	err = shvpu_avcenc_Init(pComponent);
	if (err != OMX_ErrorNone) {
		DEBUG(DEB_LEV_ERR,
		      "In %s Video Encoder Init"
		      "Failed Error=%x\n",
		      __func__, err);
		return err;
	}

	shvpu_avcenc_Private->avcodecReady = OMX_TRUE;

	return err;
}

OMX_ERRORTYPE
shvpu_avcenc_MessageHandler(OMX_COMPONENTTYPE * pComponent,
			    internalRequestMessageType * message)
{
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private =
		(shvpu_avcenc_PrivateType *) pComponent->pComponentPrivate;
	OMX_ERRORTYPE err;

	DEBUG(DEB_LEV_FUNCTION_NAME, "In %s\n", __func__);

	if (message->messageType == OMX_CommandStateSet) {
		if ((shvpu_avcenc_Private->state == OMX_StateIdle) &&
		    (message->messageParam == OMX_StateExecuting)) {
			err = handleState_IdletoExecuting(pComponent);
		} else if ((shvpu_avcenc_Private->state == OMX_StateIdle) &&
			   (message->messageParam == OMX_StateLoaded)) {
			err = handleState_IdletoLoaded(pComponent);
			if (err != OMX_ErrorNone)
				return err;
		} if ((shvpu_avcenc_Private->state == OMX_StateLoaded) &&
		      (message->messageParam == OMX_StateIdle)) {
			err = handleState_LoadedtoIdle(pComponent, message);
			if (err != OMX_ErrorNone)
				return err;
		}
	}

	// Execute the base message handling
	err = omx_base_component_MessageHandler(pComponent, message);

	return err;
}

/** Executes all the required steps after an input buffer
    frame-size has changed.
*/
static inline void
UpdateFrameSize(OMX_COMPONENTTYPE *pComponent) {
	shvpu_avcenc_PrivateType* shvpu_avcenc_Private =
		pComponent->pComponentPrivate;
	omx_base_video_PortType *inPort =
		(omx_base_video_PortType *)
		shvpu_avcenc_Private->
		ports[OMX_BASE_FILTER_INPUTPORT_INDEX];

	switch(inPort->sPortParam.format.video.eColorFormat) {
	case OMX_COLOR_FormatYUV420Planar:
	case OMX_COLOR_FormatYUV420SemiPlanar:
		inPort->sPortParam.nBufferSize =
			inPort->sPortParam.format.video.nFrameWidth *
			inPort->sPortParam.format.video.nFrameHeight *
			3/2;
		break;
	default:
		inPort->sPortParam.nBufferSize =
			inPort->sPortParam.format.video.nFrameWidth *
			inPort->sPortParam.format.video.nFrameHeight *
			2;
		break;
	}
}

static OMX_ERRORTYPE
shvpu_avcenc_SetProfileLevel(shvpu_avcenc_PrivateType *
			     shvpu_avcenc_Private,
			     OMX_VIDEO_AVCPROFILETYPE eProfile,
			     OMX_VIDEO_AVCLEVELTYPE eLevel)
{
	const struct {
		OMX_VIDEO_AVCPROFILETYPE eProfile;
		int			 id;
	} profile_idx[] = {
		{ OMX_VIDEO_AVCProfileBaseline, 66 },
		{ OMX_VIDEO_AVCProfileMain, 77 },
		{ OMX_VIDEO_AVCProfileHigh, 100 },
		{ 0, 0 },
	};
	const struct {
		OMX_VIDEO_AVCLEVELTYPE eLevel;
		int			 id;
	} level_idx[] = {
		{ OMX_VIDEO_AVCLevel1, 10 },
		{ OMX_VIDEO_AVCLevel1b, 10 },
		{ OMX_VIDEO_AVCLevel11, 11 },
		{ OMX_VIDEO_AVCLevel12, 12 },
		{ OMX_VIDEO_AVCLevel13, 13 },
		{ OMX_VIDEO_AVCLevel2, 20 },
		{ OMX_VIDEO_AVCLevel21, 21 },
		{ OMX_VIDEO_AVCLevel22, 22 },
		{ OMX_VIDEO_AVCLevel3, 30 },
		{ OMX_VIDEO_AVCLevel31, 31 },
		{ OMX_VIDEO_AVCLevel32, 32 },
		{ OMX_VIDEO_AVCLevel4, 40 },
		{ OMX_VIDEO_AVCLevel41, 41 },
		{ 0, 0 },
	};
	shvpu_codec_t *pCodec = shvpu_avcenc_Private->avCodec;
	int i, ret;

	/* profile */
	for (i=0; profile_idx[i].id > 0; i++)
		if (profile_idx[i].eProfile == eProfile)
			break;
	if (profile_idx[i].id == 0)
		return OMX_ErrorUnsupportedSetting;
	ret = encode_set_profile(pCodec, profile_idx[i].id);
	if (ret)
		return OMX_ErrorUnsupportedSetting;

	/* level */
	for (i=0; level_idx[i].id > 0; i++)
		if (level_idx[i].eLevel == eLevel)
			break;
	if (level_idx[i].id == 0)
		return OMX_ErrorUnsupportedSetting;
	ret = encode_set_level(pCodec, level_idx[i].id,
			       eLevel == OMX_VIDEO_AVCLevel1b);
	if (ret)
		return OMX_ErrorUnsupportedSetting;

	shvpu_avcenc_Private->avcType.eProfile = eProfile;
	shvpu_avcenc_Private->avcType.eLevel = eLevel;

	return OMX_ErrorNone;
}

static OMX_ERRORTYPE
shvpu_avcenc_SetAvcTypeParameters(shvpu_avcenc_PrivateType *
				  shvpu_avcenc_Private,
				  OMX_VIDEO_PARAM_AVCTYPE *pAvcType)
{
	shvpu_codec_t *pCodec = shvpu_avcenc_Private->avCodec;
	OMX_ERRORTYPE err;
	int ret;

	ret = encode_set_options(pCodec, pAvcType->nRefFrames,
				 pAvcType->nPFrames + pAvcType->nBFrames,
				 pAvcType->nBFrames / pAvcType->nPFrames,
				 pAvcType->bEntropyCodingCABAC == OMX_TRUE,
				 pAvcType->nCabacInitIdc);
	if (ret)
		return OMX_ErrorUnsupportedSetting;

	shvpu_avcenc_Private->avcType.nRefFrames = pAvcType->nRefFrames;
	shvpu_avcenc_Private->avcType.nPFrames = pAvcType->nPFrames;
	shvpu_avcenc_Private->avcType.nBFrames = pAvcType->nBFrames;
	shvpu_avcenc_Private->avcType.bEntropyCodingCABAC =
		pAvcType->bEntropyCodingCABAC;
	shvpu_avcenc_Private->avcType.nCabacInitIdc =
		pAvcType->nCabacInitIdc;

	err = shvpu_avcenc_SetProfileLevel(shvpu_avcenc_Private,
					   pAvcType->eProfile,
					   pAvcType->eLevel);

	return err;
}

static OMX_ERRORTYPE
shvpu_avcenc_SetBitrateParameters(shvpu_codec_t *pCodec,
				  OMX_VIDEO_PARAM_BITRATETYPE *pBRType)
{
	const struct {
		OMX_VIDEO_CONTROLRATETYPE eControlRate;
		char id;
	} ratecontrols[] = {
		{ OMX_Video_ControlRateVariable, 'v' },
		{ OMX_Video_ControlRateConstant, 'c' },
		{ OMX_Video_ControlRateConstantSkipFrames, 'S' },
		{ 0, '\0' },
	};
	int i, ret;

	for (i=0; ratecontrols[i].id != '\0'; i++)
		if (ratecontrols[i].eControlRate == pBRType->eControlRate)
			break;

	if (ratecontrols[i].id == '\0')
		return OMX_ErrorUnsupportedSetting;

	ret = encode_set_bitrate(pCodec, pBRType->nTargetBitrate,
				 ratecontrols[i].id);
	if (ret)
		return OMX_ErrorUnsupportedSetting;

	return OMX_ErrorNone;
}

OMX_ERRORTYPE
shvpu_avcenc_SetParameter(OMX_HANDLETYPE hComponent,
			  OMX_INDEXTYPE nParamIndex,
			  OMX_PTR ComponentParameterStructure)
{

	OMX_ERRORTYPE eError = OMX_ErrorNone;
	OMX_U32 portIndex;
	int ret;

	/* Check which structure we are being fed and
	   make control its header */
	OMX_COMPONENTTYPE *pComponent = hComponent;
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private =
		pComponent->pComponentPrivate;
	omx_base_video_PortType *port;
	if (ComponentParameterStructure == NULL) {
		return OMX_ErrorBadParameter;
	}

	DEBUG(DEB_LEV_SIMPLE_SEQ, "   Setting parameter %i\n", nParamIndex);
	switch (nParamIndex) {
	case OMX_IndexParamPortDefinition:
	{
		OMX_PARAM_PORTDEFINITIONTYPE *pPortDef =
			(OMX_PARAM_PORTDEFINITIONTYPE *)
			ComponentParameterStructure;

		eError = omx_base_component_SetParameter(
			hComponent, nParamIndex,
			ComponentParameterStructure);
		if (eError != OMX_ErrorNone)
			break;

		UpdateFrameSize(pComponent);
		portIndex = pPortDef->nPortIndex;
		port = (omx_base_video_PortType *)
			shvpu_avcenc_Private->ports[portIndex];
		port->sPortParam.format.video.nBitrate =
			pPortDef->format.video.nBitrate;
		port->sVideoParam.eColorFormat =
			port->sPortParam.format.video.eColorFormat;

		if (port->sPortParam.eDir == OMX_DirInput) {
			ret = encode_set_propaties(
				shvpu_avcenc_Private->avCodec,
				pPortDef->format.video.nFrameWidth,
				pPortDef->format.video.nFrameHeight,
				pPortDef->format.video.xFramerate,
				pPortDef->format.video.nBitrate,
				' ' /* rate control not specified */);
		} else {
			ret = encode_set_bitrate(
				shvpu_avcenc_Private->avCodec,
				pPortDef->format.video.nBitrate,
				' ' /* rate control not specified */);
		}
		if (ret)
			eError = OMX_ErrorNone;
		break;
	}
	case OMX_IndexParamVideoBitrate:
	{
		OMX_VIDEO_PARAM_BITRATETYPE *pBRType;
		pBRType = ComponentParameterStructure;
		eError = omx_base_component_ParameterSanityCheck(
			hComponent, pBRType->nPortIndex, pBRType,
			sizeof (OMX_VIDEO_PARAM_BITRATETYPE));
		if(eError!=OMX_ErrorNone) {
			DEBUG(DEB_LEV_ERR,
			      "In %s Parameter Check Error=%x\n",
			      __func__,eError);
			break;
		}
		eError = shvpu_avcenc_SetBitrateParameters(
			shvpu_avcenc_Private->avCodec, pBRType);
		if (eError == OMX_ErrorNone) {
			shvpu_avcenc_Private->bitrateType.eControlRate =
				pBRType->eControlRate;
			shvpu_avcenc_Private->bitrateType.nTargetBitrate =
				pBRType->nTargetBitrate;
		}
		break;
	}
	case OMX_IndexParamStandardComponentRole:
	{
		OMX_PARAM_COMPONENTROLETYPE *pComponentRole;
		pComponentRole = ComponentParameterStructure;
		if (shvpu_avcenc_Private->state != OMX_StateLoaded
		    && shvpu_avcenc_Private->state !=
		    OMX_StateWaitForResources) {
			DEBUG(DEB_LEV_ERR,
			      "In %s Incorrect State=%x lineno=%d\n",
			      __func__, shvpu_avcenc_Private->state,
			      __LINE__);
			return OMX_ErrorIncorrectStateOperation;
		}

		if ((eError =
		     checkHeader(ComponentParameterStructure,
				 sizeof(OMX_PARAM_COMPONENTROLETYPE)))
		    != OMX_ErrorNone) {
			break;
		}

		if (strcmp((char *)pComponentRole->cRole,
			   VIDEO_ENC_H264_ROLE))
			return OMX_ErrorBadParameter;
		SetInternalVideoParameters(pComponent);
		break;
	}
	case OMX_IndexParamVideoPortFormat:
	{
		OMX_VIDEO_PARAM_PORTFORMATTYPE *pVideoPortFormat;
		pVideoPortFormat = ComponentParameterStructure;
		portIndex = pVideoPortFormat->nPortIndex;
		/* Check Structure Header and verify component state */
		eError = omx_base_component_ParameterSanityCheck
			(hComponent, portIndex, pVideoPortFormat,
			 sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
		if (eError != OMX_ErrorNone) {
			DEBUG(DEB_LEV_ERR,
			      "In %s Parameter Check Error=%x\n",
			      __func__, eError);
			break;
		}
		if (portIndex > 1)
			return OMX_ErrorBadPortIndex;
		port = (omx_base_video_PortType *)
			shvpu_avcenc_Private->ports[portIndex];
		memcpy(&port->sVideoParam, pVideoPortFormat,
		       sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
		break;
	}
	case OMX_IndexParamVideoAvc:
	{
		OMX_VIDEO_PARAM_AVCTYPE *pAvcType;
		pAvcType = ComponentParameterStructure;
		eError = omx_base_component_ParameterSanityCheck(
			hComponent, pAvcType->nPortIndex, pAvcType,
			sizeof (OMX_VIDEO_PARAM_AVCTYPE));
		if(eError!=OMX_ErrorNone) {
			DEBUG(DEB_LEV_ERR,
			      "In %s Parameter Check Error=%x\n",
			      __func__,eError);
			break;
		}
		eError = shvpu_avcenc_SetAvcTypeParameters(
			shvpu_avcenc_Private, pAvcType);
		break;
	}
	case OMX_IndexParamVideoProfileLevelCurrent:
	{
		OMX_VIDEO_PARAM_PROFILELEVELTYPE *pProfType;
		pProfType = ComponentParameterStructure;
		eError = omx_base_component_ParameterSanityCheck(
			hComponent, pProfType->nPortIndex, pProfType,
			sizeof (OMX_VIDEO_PARAM_PROFILELEVELTYPE));
		if(eError!=OMX_ErrorNone) {
			DEBUG(DEB_LEV_ERR,
			      "In %s Parameter Check Error=%x\n",
			      __func__,eError);
			break;
		}
		eError = shvpu_avcenc_SetProfileLevel(
			shvpu_avcenc_Private,
			pProfType->eProfile, pProfType->eLevel);
		break;
	}
	default:		/*Call the base component function */
		eError = omx_base_component_SetParameter(
			hComponent, nParamIndex,
			ComponentParameterStructure);
	}
	return eError;
}

OMX_ERRORTYPE
shvpu_avcenc_GetParameter(OMX_HANDLETYPE hComponent,
			  OMX_INDEXTYPE nParamIndex,
			  OMX_PTR ComponentParameterStructure)
{

	OMX_ERRORTYPE eError = OMX_ErrorNone;

	OMX_COMPONENTTYPE *pComponent = hComponent;
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private =
		pComponent->pComponentPrivate;
	if (ComponentParameterStructure == NULL) {
		return OMX_ErrorBadParameter;
	}
	DEBUG(DEB_LEV_SIMPLE_SEQ, "   Getting parameter %i\n", nParamIndex);
	/* Check which structure we are being fed and fill its header */
	switch (nParamIndex) {
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
		strcpy((char *)pComponentRole->cRole, VIDEO_ENC_H264_ROLE);
		break;
	}
	case OMX_IndexParamVideoPortFormat:
	{
		OMX_VIDEO_PARAM_PORTFORMATTYPE *pVideoPortFormat;
		omx_base_video_PortType *port;

		pVideoPortFormat = ComponentParameterStructure;
		if ((eError =
		     checkHeader(ComponentParameterStructure,
				 sizeof
				 (OMX_VIDEO_PARAM_PORTFORMATTYPE))) !=
		    OMX_ErrorNone) {
			break;
		}
		if (pVideoPortFormat->nPortIndex > 1)
			return OMX_ErrorBadPortIndex;
		if (pVideoPortFormat->nIndex > 0)
			return OMX_ErrorNoMore;
		port = (omx_base_video_PortType *)
			shvpu_avcenc_Private->ports
			[pVideoPortFormat->nPortIndex];
		memcpy(pVideoPortFormat, &port->sVideoParam,
		       sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
		break;
	}
	case OMX_IndexParamVideoAvc:
	{
		OMX_VIDEO_PARAM_AVCTYPE *pVideoAvc;
		pVideoAvc = ComponentParameterStructure;
		if (pVideoAvc->nPortIndex > 1) {
			return OMX_ErrorBadPortIndex;
		}
		if ((eError =
		     checkHeader(ComponentParameterStructure,
				 sizeof(OMX_VIDEO_PARAM_AVCTYPE))) !=
		    OMX_ErrorNone) {
			break;
		}
		memcpy(pVideoAvc, &shvpu_avcenc_Private->avcType,
		       sizeof(OMX_VIDEO_PARAM_AVCTYPE));
		break;
	}
	case OMX_IndexParamVideoBitrate:
	{
		OMX_VIDEO_PARAM_BITRATETYPE *pBRType;
		pBRType = ComponentParameterStructure;
		pBRType->eControlRate =
			shvpu_avcenc_Private->bitrateType.eControlRate;
		pBRType->nTargetBitrate =
			shvpu_avcenc_Private->bitrateType.nTargetBitrate;
		break;
	}
	case OMX_IndexParamVideoProfileLevelQuerySupported:
	{
		OMX_VIDEO_PARAM_PROFILELEVELTYPE *pProfType;
		pProfType = ComponentParameterStructure;
		pProfType->eProfile = shvpu_avcenc_Private->avcType.eProfile;
		pProfType->eLevel = shvpu_avcenc_Private->avcType.eProfile;
		break;
	}
	default:		/*Call the base component function */
		eError = omx_base_component_GetParameter
			(hComponent, nParamIndex,
			 ComponentParameterStructure);
	}
	return eError;
}

OMX_ERRORTYPE
shvpu_avcenc_AllocateBuffer(omx_base_PortType *pPort,
			    OMX_BUFFERHEADERTYPE** pBuffer,
			    OMX_U32 nPortIndex, OMX_PTR pAppPrivate,
			    OMX_U32 nSizeBytes)
{
	unsigned int i;
	OMX_COMPONENTTYPE* pComponent = pPort->standCompContainer;
	shvpu_avcenc_PrivateType* shvpu_avcenc_Private =
		(shvpu_avcenc_PrivateType*)pComponent->pComponentPrivate;

	DEBUG(DEB_LEV_FUNCTION_NAME,
	      "In %s for port %x\n", __func__, (int)pPort);

	if (nPortIndex != pPort->sPortParam.nPortIndex) {
		return OMX_ErrorBadPortIndex;
	}
	if (PORT_IS_TUNNELED_N_BUFFER_SUPPLIER(pPort)) {
		return OMX_ErrorBadPortIndex;
	}

	if (shvpu_avcenc_Private->transientState !=
	    OMX_TransStateLoadedToIdle) {
		if (!pPort->bIsTransientToEnabled) {
			DEBUG(DEB_LEV_ERR,
			      "In %s: The port is not allowed to "
			      "receive buffers\n", __func__);
			return OMX_ErrorIncorrectStateTransition;
		}
	}

	if(nSizeBytes < pPort->sPortParam.nBufferSize) {
		DEBUG(DEB_LEV_ERR, "In %s: Requested Buffer Size "
		      "%lu is less than Minimum Buffer Size %lu\n",
		      __func__, nSizeBytes, pPort->sPortParam.nBufferSize);
		return OMX_ErrorIncorrectStateTransition;
	}

	for(i=0; i < pPort->sPortParam.nBufferCountActual; i++){
		if (pPort->bBufferStateAllocated[i] == BUFFER_FREE) {
			pPort->pInternalBufferStorage[i] =
				calloc(1,sizeof(OMX_BUFFERHEADERTYPE));
			if (!pPort->pInternalBufferStorage[i]) {
				return OMX_ErrorInsufficientResources;
			}
			setHeader(pPort->pInternalBufferStorage[i],
				  sizeof(OMX_BUFFERHEADERTYPE));
			/* allocate the buffer */
			unsigned long phys;
			pPort->pInternalBufferStorage[i]->pBuffer =
				pmem_alloc(nSizeBytes, 32, &phys);
			logd("pmem_alloc(%d, 32)\n", nSizeBytes);
			if(pPort->pInternalBufferStorage[i]->pBuffer==NULL) {
				return OMX_ErrorInsufficientResources;
			}
			pPort->pInternalBufferStorage[i]->nAllocLen =
				nSizeBytes;
			pPort->pInternalBufferStorage[i]->pPlatformPrivate =
				(OMX_PTR)phys;
			pPort->pInternalBufferStorage[i]->pAppPrivate =
				pAppPrivate;
			*pBuffer = pPort->pInternalBufferStorage[i];
			pPort->bBufferStateAllocated[i] = BUFFER_ALLOCATED;
			pPort->bBufferStateAllocated[i] |= HEADER_ALLOCATED;
			if (pPort->sPortParam.eDir == OMX_DirInput) {
				pPort->pInternalBufferStorage[i]->
					nInputPortIndex =
					pPort->sPortParam.nPortIndex;
			} else {
				pPort->pInternalBufferStorage[i]->
					nOutputPortIndex =
					pPort->sPortParam.nPortIndex;
			}
			pPort->nNumAssignedBuffers++;
			DEBUG(DEB_LEV_PARAMS,
			      "pPort->nNumAssignedBuffers %i\n",
			      (int)pPort->nNumAssignedBuffers);

			if (pPort->sPortParam.nBufferCountActual ==
			    pPort->nNumAssignedBuffers) {
				pPort->sPortParam.bPopulated = OMX_TRUE;
				pPort->bIsFullOfBuffers = OMX_TRUE;
				DEBUG(DEB_LEV_SIMPLE_SEQ,
				      "In %s nPortIndex=%d\n",
				      __func__,(int)nPortIndex);
				tsem_up(pPort->pAllocSem);
			}
			DEBUG(DEB_LEV_FUNCTION_NAME,
			      "Out of %s for port %x\n",
			      __func__, (int)pPort);
			return OMX_ErrorNone;
		}
	}
	DEBUG(DEB_LEV_ERR, "Out of %s for port %x. Error: "
	      "no available buffers\n",__func__, (int)pPort);
	return OMX_ErrorInsufficientResources;
}

OMX_ERRORTYPE
shvpu_avcenc_FreeBuffer(omx_base_PortType *pPort,
			OMX_U32 nPortIndex, OMX_BUFFERHEADERTYPE* pBuffer)
{
	unsigned int i;
	OMX_COMPONENTTYPE* pComponent = pPort->standCompContainer;
	shvpu_avcenc_PrivateType* shvpu_avcenc_Private =
		(shvpu_avcenc_PrivateType*)pComponent->pComponentPrivate;

	DEBUG(DEB_LEV_FUNCTION_NAME,
	      "In %s for port %x\n", __func__, (int)pPort);

	if (nPortIndex != pPort->sPortParam.nPortIndex) {
		return OMX_ErrorBadPortIndex;
	}
	if (PORT_IS_TUNNELED_N_BUFFER_SUPPLIER(pPort)) {
		return OMX_ErrorBadPortIndex;
	}

	if (shvpu_avcenc_Private->transientState !=
	    OMX_TransStateIdleToLoaded) {
		if (!pPort->bIsTransientToDisabled) {
			DEBUG(DEB_LEV_FULL_SEQ,
			      "In %s: The port is not allowed "
			      "to free the buffers\n", __func__);
			(*(shvpu_avcenc_Private->callbacks->EventHandler))
				(pComponent,
				 shvpu_avcenc_Private->callbackData,
				 OMX_EventError,
				 /* The command was completed */
				 OMX_ErrorPortUnpopulated,
				 /* The commands was a OMX_CommandStateSet */
				 nPortIndex,
				/* The state has been changed
				   in message->messageParam2 */
				 NULL);
		}
	}

	for(i=0; i < pPort->sPortParam.nBufferCountActual; i++){
		if (pPort->bBufferStateAllocated[i] & BUFFER_ALLOCATED) {
			pPort->bIsFullOfBuffers = OMX_FALSE;
			if(pPort->pInternalBufferStorage[i]->pBuffer){
				DEBUG(DEB_LEV_PARAMS,
				      "In %s freeing %i pBuffer=%x\n",
				      __func__, (int)i,
				      (int)pPort->
				      pInternalBufferStorage[i]->
				      pBuffer);
				pmem_free(pPort->
				     pInternalBufferStorage[i]->
					  pBuffer,
					  pPort->
					  pInternalBufferStorage[i]->
					  nAllocLen);
				pPort->pInternalBufferStorage[i]->
					pBuffer=NULL;
			}
			if(pPort->bBufferStateAllocated[i] &
			   HEADER_ALLOCATED) {
				free(pPort->pInternalBufferStorage[i]);
				pPort->pInternalBufferStorage[i]=NULL;
			}

			pPort->bBufferStateAllocated[i] = BUFFER_FREE;

			pPort->nNumAssignedBuffers--;
			DEBUG(DEB_LEV_PARAMS,
			      "pPort->nNumAssignedBuffers %i\n",
			      (int)pPort->nNumAssignedBuffers);

			if (pPort->nNumAssignedBuffers == 0) {
				pPort->sPortParam.bPopulated = OMX_FALSE;
				pPort->bIsEmptyOfBuffers = OMX_TRUE;
				tsem_up(pPort->pAllocSem);
			}
			DEBUG(DEB_LEV_FUNCTION_NAME,
			      "Out of %s for port %x\n",
			      __func__, (int)pPort);
			return OMX_ErrorNone;
		}
	}
	DEBUG(DEB_LEV_ERR, "Out of %s for port %x "
	      "with OMX_ErrorInsufficientResources\n",
	      __func__, (int)pPort);
	return OMX_ErrorInsufficientResources;
}

OMX_ERRORTYPE
shvpu_avcenc_ComponentRoleEnum(OMX_HANDLETYPE hComponent,
			       OMX_U8 * cRole, OMX_U32 nIndex)
{

	if (nIndex == 0) {
		strcpy((char *)cRole, VIDEO_ENC_H264_ROLE);
	} else {
		return OMX_ErrorUnsupportedIndex;
	}
	return OMX_ErrorNone;
}

static inline void
handle_buffer_flush(shvpu_avcenc_PrivateType *shvpu_avcenc_Private,
		    OMX_BOOL *pIsInBufferNeeded,
		    OMX_BOOL *pIsOutBufferNeeded,
		    int *pInBufExchanged, int *pOutBufExchanged,
		    OMX_BUFFERHEADERTYPE **ppInBuffer,
		    OMX_BUFFERHEADERTYPE **ppOutBuffer)
{
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	tsem_t *pInputSem = pInPort->pBufferSem;
	tsem_t *pOutputSem = pOutPort->pBufferSem;

	pthread_mutex_lock(&shvpu_avcenc_Private->flush_mutex);
	while (PORT_IS_BEING_FLUSHED(pInPort) ||
	       PORT_IS_BEING_FLUSHED(pOutPort)) {
		pthread_mutex_unlock
			(&shvpu_avcenc_Private->flush_mutex);

		DEBUG(DEB_LEV_FULL_SEQ,
		      "In %s 1 signalling flush all cond iE=%d,"
		      "iF=%d,oE=%d,oF=%d iSemVal=%d,oSemval=%d\n",
		      __func__, *pInBufExchanged, *pIsInBufferNeeded,
		      *pOutBufExchanged, *pIsOutBufferNeeded,
		      pInputSem->semval, pOutputSem->semval);

		if (*ppOutBuffer && PORT_IS_BEING_FLUSHED(pOutPort)) {
			pOutPort->ReturnBufferFunction(pOutPort,
						       *ppOutBuffer);
			(*pOutBufExchanged)--;
			*ppOutBuffer = NULL;
			*pIsOutBufferNeeded = OMX_TRUE;
			DEBUG(DEB_LEV_FULL_SEQ,
			      "Ports are flushing,so returning "
			      "output buffer\n");
		}

		if (*ppInBuffer && PORT_IS_BEING_FLUSHED(pInPort)) {
			pInPort->ReturnBufferFunction(pInPort, *ppInBuffer);
			*ppInBuffer = NULL;
			(*pInBufExchanged)--;
			*pIsInBufferNeeded = OMX_TRUE;
			DEBUG(DEB_LEV_FULL_SEQ,
			      "Ports are flushing,so returning "
			      "input buffer\n");
		}

		DEBUG(DEB_LEV_FULL_SEQ,
		      "In %s 2 signalling flush all cond iE=%d,"
		      "iF=%d,oE=%d,oF=%d iSemVal=%d,oSemval=%d\n",
		      __func__, *pInBufExchanged, *pIsInBufferNeeded,
		      *pOutBufExchanged, *pIsOutBufferNeeded,
		      pInputSem->semval, pOutputSem->semval);

		tsem_up(shvpu_avcenc_Private->flush_all_condition);
		tsem_down(shvpu_avcenc_Private->flush_condition);
		pthread_mutex_lock(&shvpu_avcenc_Private->
				   flush_mutex);
	}
	pthread_mutex_unlock(&shvpu_avcenc_Private->flush_mutex);

	return;
}

static inline int
waitBuffers(shvpu_avcenc_PrivateType *shvpu_avcenc_Private,
	     OMX_BOOL isInBufferNeeded, OMX_BOOL isOutBufferNeeded)
{
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	tsem_t *pInputSem = pInPort->pBufferSem;
	tsem_t *pOutputSem = pOutPort->pBufferSem;

	if ((isInBufferNeeded == OMX_TRUE &&
	     pInputSem->semval == 0) &&
	    (shvpu_avcenc_Private->state != OMX_StateLoaded &&
	     shvpu_avcenc_Private->state != OMX_StateInvalid)) {
		//Signalled from EmptyThisBuffer or
		//FillThisBuffer or some thing else
		DEBUG(DEB_LEV_FULL_SEQ,
		      "Waiting for next input/output buffer\n");
		tsem_down(shvpu_avcenc_Private->bMgmtSem);

	}

	if (shvpu_avcenc_Private->state == OMX_StateLoaded
	    || shvpu_avcenc_Private->state == OMX_StateInvalid) {
		DEBUG(DEB_LEV_SIMPLE_SEQ,
		      "In %s Buffer Management Thread is exiting\n",
		      __func__);
		return -1;
	}

	if ((isOutBufferNeeded == OMX_TRUE &&
	     pOutputSem->semval == 0) &&
	    (shvpu_avcenc_Private->state != OMX_StateLoaded &&
	     shvpu_avcenc_Private->state != OMX_StateInvalid) &&
	    !(PORT_IS_BEING_FLUSHED(pInPort) ||
	      PORT_IS_BEING_FLUSHED(pOutPort))) {
		//Signalled from EmptyThisBuffer or
		//FillThisBuffer or some thing else
		DEBUG(DEB_LEV_FULL_SEQ,
		      "Waiting for next input/output buffer\n");
		tsem_down(shvpu_avcenc_Private->bMgmtSem);

	}

	if (shvpu_avcenc_Private->state == OMX_StateLoaded ||
	    shvpu_avcenc_Private->state == OMX_StateInvalid) {
		DEBUG(DEB_LEV_SIMPLE_SEQ,
		      "In %s Buffer Management Thread is exiting\n",
		      __func__);
		return -1;
	}

	return 0;
}

static inline OMX_BOOL
getInBuffer(shvpu_avcenc_PrivateType *shvpu_avcenc_Private,
	    OMX_BUFFERHEADERTYPE **ppInBuffer,
	    int *pInBufExchanged, queue_t *pProcessInBufQueue)
{
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
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
updateFilledLen(queue_t *pProcessInBufQueue, void *pConsumed)
{
	OMX_BUFFERHEADERTYPE *pBuffer;
	OMX_BOOL ret = OMX_FALSE;
	int nProcBuffers;

	nProcBuffers = pProcessInBufQueue->nelem;
	while (nProcBuffers-- > 0) {
		pBuffer = dequeue(pProcessInBufQueue);
		if (pConsumed == (pBuffer->pBuffer + pBuffer->nOffset)) {
			pBuffer->nFilledLen = 0;
			ret = OMX_TRUE;
		}
		queue(pProcessInBufQueue, pBuffer);
	}

	return ret;
}

static inline OMX_BOOL
takeOutBuffer(shvpu_avcenc_PrivateType *shvpu_avcenc_Private,
	      OMX_BUFFERHEADERTYPE **ppOutBuffer,
	      int *pOutBufExchanged)
{
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	tsem_t *pOutputSem = pOutPort->pBufferSem;
	queue_t *pOutputQueue = pOutPort->pBufferQueue;

	tsem_down(pOutputSem);
	if (pOutputQueue->nelem == 0) {
		DEBUG(DEB_LEV_ERR,
		      "Had NULL output buffer!! op is=%d,iq=%d\n",
		      pOutputSem->semval, pOutputQueue->nelem);
		return OMX_TRUE;
	}

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
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private =
		(shvpu_avcenc_PrivateType *) pComponent->pComponentPrivate;

	if ((OMX_COMPONENTTYPE *)pInBuffer->hMarkTargetComponent ==
	    (OMX_COMPONENTTYPE *)pComponent) {
		/*Clear the mark and generate an event */
		(*(shvpu_avcenc_Private->callbacks->EventHandler))
			(pComponent, shvpu_avcenc_Private->callbackData,
			 OMX_EventMark,	/* The command was completed */
			 1,		/* The commands was a
				   	   OMX_CommandStateSet */
			 0,		/* The state has been changed
					   in message->messageParam2 */
			 pInBuffer->pMarkData);
	} else {
		/*If this is not the target component then pass the mark */
		shvpu_avcenc_Private->pMark.hMarkTargetComponent =
			pInBuffer->hMarkTargetComponent;
		shvpu_avcenc_Private->pMark.pMarkData =
			pInBuffer->pMarkData;
	}
	pInBuffer->hMarkTargetComponent = NULL;

	return;
}

static inline void
generateHeader(OMX_COMPONENTTYPE * pComponent,
	       OMX_BUFFERHEADERTYPE *pOutBuffer)
{
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private =
		pComponent->pComponentPrivate;
	shvpu_codec_t *pCodec = shvpu_avcenc_Private->avCodec;
	size_t nFilledLen;

	/* put the stream header if this is the first output */
	nFilledLen = encode_header(shvpu_avcenc_Private->avCodec->pContext,
				   pOutBuffer->pBuffer,
				   pOutBuffer->nAllocLen);
	if (nFilledLen < 0) {
		loge("header failed\n");
		return;
	}
	pOutBuffer->nFilledLen += nFilledLen;
	logd("%d bytes header output\n", nFilledLen);
	pOutBuffer->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;

	shvpu_avcenc_Private->isFirstBuffer = OMX_FALSE;
	return;
}

static inline void
saveBufferMetadata(queue_t *pBMIQueue, int id,
		   OMX_BUFFERHEADERTYPE *pInBuffer)
{
	buffer_metainfo_t *pBMI;

	pBMI = calloc(1, sizeof(buffer_metainfo_t));
	if (pBMI == NULL) {
		loge("calloc for buffer_metainfo failed.\n");
		return;
	}
	pBMI->id = id;
	pBMI->hMarkTargetComponent = pInBuffer->hMarkTargetComponent;
	pInBuffer->hMarkTargetComponent = NULL;
	pBMI->pMarkData = pInBuffer->pMarkData;
	pInBuffer->pMarkData = NULL;
	pBMI->nTimeStamp = pInBuffer->nTimeStamp;
	pInBuffer->nTimeStamp = 0;
	pBMI->nFlags = pInBuffer->nFlags;
	pInBuffer->nFlags = 0;
	shvpu_queue(pBMIQueue, pBMI);

	return;
}

static inline void
applyBufferMetadata(queue_t *pBMIQueue, int id,
		    OMX_BUFFERHEADERTYPE *pOutBuffer)
{
	buffer_metainfo_t *pBMI;

	while ((pBMI = (buffer_metainfo_t *)shvpu_peek(pBMIQueue)) != NULL) {
		if (pBMI->id > id)
			break;

		pBMI = (buffer_metainfo_t *)shvpu_dequeue(pBMIQueue);
		if (pBMI->id == id) {
			pOutBuffer->nTimeStamp = pBMI->nTimeStamp;
			pOutBuffer->nFlags = pBMI->nFlags;
			free(pBMI);
			break;
		}
		loge("Warning: timestamp and flags for "
		     "frame-%d were dropped.\n", pBMI->id);
		free(pBMI);
	}

	return;
}

static inline void
fillOutBuffer(OMX_COMPONENTTYPE * pComponent,
	      OMX_BUFFERHEADERTYPE *pOutBuffer)
{
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private =
		pComponent->pComponentPrivate;
	shvpu_codec_t *pCodec = shvpu_avcenc_Private->avCodec;
	shvpu_avcenc_outbuf_t *pStreamBuffer;
	size_t nAvailLen, nFilledLen;
	OMX_U8 *pBuffer;
	int i;

	if (shvpu_avcenc_Private->bIsEOSReached) {
		pOutBuffer->nFilledLen = 0;
		pOutBuffer->nFlags = OMX_BUFFERFLAG_EOS;
		return;
	}

	nAvailLen = pOutBuffer->nAllocLen - pOutBuffer->nFilledLen;
	pBuffer = pOutBuffer->pBuffer + pOutBuffer->nFilledLen;

	/* check the VPU's output buffers and
	   copy the output stream data if available */
	for (i=0; i<SHVPU_AVCENC_OUTBUF_NUM; i++) {
		/* check */
		pStreamBuffer =	&shvpu_avcenc_Private->
			avCodec->streamBuffer[i];
		if ((pStreamBuffer->status == SHVPU_BUFFER_STATUS_FILL) &&
		    ((pCodec->pDriver->lastOutput + 1) ==
		     pStreamBuffer->frameId)) {
			/* copy stream data */
			nFilledLen = pStreamBuffer->bufferInfo.strm_size;
			logd("%d bytes data output\n", nFilledLen);
			if (nAvailLen < nFilledLen) {
				loge("too small buffer(%d) available\n",
				     nAvailLen);
				break;
			}
			memcpy(pBuffer, pStreamBuffer->
			       bufferInfo.buff_addr, nFilledLen);
			nAvailLen -= nFilledLen;
			pBuffer += nFilledLen;
			pOutBuffer->nFilledLen += nFilledLen;
			pStreamBuffer->status =	SHVPU_BUFFER_STATUS_READY;
			pCodec->pDriver->lastOutput = pStreamBuffer->frameId;
			/* copy metadata */
			applyBufferMetadata(shvpu_avcenc_Private->pBMIQueue,
					    pStreamBuffer->frameId,
					    pOutBuffer);
		}
	}

	/* check the end of stream */
	if (pCodec->pDriver->isEndInput &&
	    ((pCodec->nEncoded - 1) <= pCodec->pDriver->lastOutput)) {
		/* put the end code (EOSeq and EOStr) */
		nFilledLen = encode_endcode(pCodec->pContext,
					    pBuffer, nAvailLen);
		if (nFilledLen > 0) {
			nAvailLen -= nFilledLen;
			pBuffer += nFilledLen;
			pOutBuffer->nFilledLen += nFilledLen;
		} else {
			printf("cannot put end code!\n");
		}

		/* finalize the encoder */
		encode_finalize(pCodec->pContext);

		shvpu_avcenc_Private->bIsEOSReached = OMX_TRUE;
	}

	return;
}

static inline void
checkFillDone(OMX_COMPONENTTYPE * pComponent,
		OMX_BUFFERHEADERTYPE **ppOutBuffer,
		int *pOutBufExchanged,
		OMX_BOOL *pIsOutBufferNeeded)
{
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private =
		pComponent->pComponentPrivate;
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	/* If EOS and Input buffer Filled Len Zero
	   then Return output buffer immediately */
	if (((*ppOutBuffer)->nFilledLen == 0) &&
	    !((*ppOutBuffer)->nFlags & OMX_BUFFERFLAG_EOS))
		return;

	if ((*ppOutBuffer)->nFlags & OMX_BUFFERFLAG_EOS) {
	        (*(shvpu_avcenc_Private->callbacks->EventHandler))
			(pComponent,
			 shvpu_avcenc_Private->callbackData,
			 OMX_EventBufferFlag, /* The command was completed */
			 1, /* The commands was a OMX_CommandStateSet */
			 (*ppOutBuffer)->nFlags,
			 /* The state has been changed
			    in message->messageParam2 */
			 NULL);
	} else {
		*pIsOutBufferNeeded = OMX_TRUE;
	}

	pOutPort->ReturnBufferFunction(pOutPort, *ppOutBuffer);
	(*pOutBufExchanged)--;
	*ppOutBuffer = NULL;
}

static inline void
checkEmptyDone(shvpu_avcenc_PrivateType *shvpu_avcenc_Private,
	       OMX_BUFFERHEADERTYPE **ppInBuffer,
	       queue_t *pInBufQueue, int *pInBufExchanged,
	       OMX_BOOL *pIsInBufferNeeded)
{
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	OMX_BUFFERHEADERTYPE *pBuffer;
	int n;

	/* Input Buffer has been completely consumed.
	  So,return input buffer */
	for (n = *pInBufExchanged; n > 0; n--) {
		pBuffer = dequeue(pInBufQueue);
		if (pBuffer->nFilledLen > 0) {
			queue(pInBufQueue, pBuffer);
			continue;
		}

		/* FIXME: It should be confirmed who should
		   reset the nOffset value, component or client. */
		pBuffer->nOffset = 0;

		pInPort->ReturnBufferFunction(pInPort, pBuffer);
		(*pInBufExchanged)--;

		if (pBuffer == *ppInBuffer)
			*ppInBuffer = NULL;
	}

	if (shvpu_avcenc_Private->avCodec->pDriver->isEndInput)
		*pIsInBufferNeeded = OMX_FALSE;
}

/** This function is used to process the input buffer and
    provide one output buffer
*/
static void
encodePicture(OMX_COMPONENTTYPE * pComponent,
	      OMX_BUFFERHEADERTYPE * pInBuffer,
	      queue_t *pProcessInBufQueue)
{
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private;
	omx_base_video_PortType *inPort;
	shvpu_codec_t *pCodec;
	void *pConsumed;
	OMX_ERRORTYPE err = OMX_ErrorNone;
	unsigned long width, height;
	int ret;

	shvpu_avcenc_Private = pComponent->pComponentPrivate;
	inPort = (omx_base_video_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	width = inPort->sPortParam.format.video.nFrameWidth;
	height = inPort->sPortParam.format.video.nFrameHeight;
	pCodec = shvpu_avcenc_Private->avCodec;

	if ((pInBuffer->nFilledLen == 0) &&
	    (pInBuffer->nFlags & OMX_BUFFERFLAG_EOS)) {
		pCodec->pDriver->isEndInput = 1;
		pCodec->pDriver->frameId -= 1;
		pInBuffer->nFlags &= ~OMX_BUFFERFLAG_EOS;
		return;
	}

	if (pInBuffer->nFilledLen < (width * height * 3 / 2)) {
		loge("data too small (%d < %d)\n",
		     pInBuffer->nFilledLen, (width * height * 3 / 2));
		err = OMX_ErrorStreamCorrupt;
	} else {
		pConsumed = NULL;
		ret = encode_main(pCodec->pContext, pCodec->pDriver->frameId,
				  pInBuffer->pBuffer, width, height,
				  &pConsumed);

		switch (ret) {
		case 0: /* encoded the picture */
			pCodec->nEncoded += 1;
		case 1: /* keep the picture */
		case 2: /* skip the picture */
			if (pConsumed)
				updateFilledLen(pProcessInBufQueue,
						pConsumed);
			pCodec->pDriver->frameId += 1;
			err = OMX_ErrorNone;
			break;
		default:
			//err = OMX_HardwareError;
			loge ("encode_main error: %d\n", ret);
			break;
		}
	}

	/* invoke an event handler if an error occurs */
	if (err != OMX_ErrorNone) {
		(*(shvpu_avcenc_Private->callbacks->EventHandler))
			(pComponent, shvpu_avcenc_Private->callbackData,
			 OMX_EventError, // An error occured
			 err,            // Error code
			 0, NULL);
        }

	/* save buffer metadata (timestamp, flags, etc.) */
	saveBufferMetadata(shvpu_avcenc_Private->pBMIQueue,
			   pCodec->pDriver->frameId, pInBuffer);

	return;
}

/** This is the central function for component processing. It
 * is executed in a separate thread, is synchronized with
 * semaphores at each port, those are released each time a new buffer
 * is available on the given port.
 */
static void *
shvpu_avcenc_BufferMgmtFunction(void *param)
{
	OMX_COMPONENTTYPE *pComponent = (OMX_COMPONENTTYPE *) param;
	shvpu_avcenc_PrivateType *shvpu_avcenc_Private =
		(shvpu_avcenc_PrivateType *) pComponent->pComponentPrivate;
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_avcenc_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
 	tsem_t *pInputSem = pInPort->pBufferSem;
	tsem_t *pOutputSem = pOutPort->pBufferSem;
	OMX_BUFFERHEADERTYPE *pOutBuffer = NULL;
	OMX_BUFFERHEADERTYPE *pInBuffer = NULL;
	OMX_BOOL isInBufferNeeded = OMX_TRUE,
		isOutBufferNeeded = OMX_TRUE;
	int inBufExchanged = 0, outBufExchanged = 0;
	queue_t processInBufQueue;
	int ret;

	shvpu_avcenc_Private->bellagioThreads->nThreadBufferMngtID =
		(long int)syscall(__NR_gettid);
	DEBUG(DEB_LEV_FUNCTION_NAME, "In %s of component %x\n", __func__,
	      (int)pComponent);
	DEBUG(DEB_LEV_SIMPLE_SEQ, "In %s the thread ID is %i\n", __func__,
	      (int)shvpu_avcenc_Private->bellagioThreads->
	      nThreadBufferMngtID);

	/* initialize a queue to keep processing input buffers */  
	queue_init(&processInBufQueue);

	DEBUG(DEB_LEV_FUNCTION_NAME, "In %s\n", __func__);
	while (shvpu_avcenc_Private->state == OMX_StateIdle
	       || shvpu_avcenc_Private->state == OMX_StateExecuting
	       || shvpu_avcenc_Private->state == OMX_StatePause
	       || shvpu_avcenc_Private->transientState ==
	       OMX_TransStateLoadedToIdle) {

		/*Wait till the ports are being flushed */
		handle_buffer_flush(shvpu_avcenc_Private,
				    &isInBufferNeeded,
				    &isOutBufferNeeded,
				    &inBufExchanged, &outBufExchanged,
				    &pInBuffer, &pOutBuffer);

		/*No buffer to process. So wait here */
		ret = waitBuffers(shvpu_avcenc_Private,
				   isInBufferNeeded,
				   isOutBufferNeeded);
		if (ret < 0)
			break;

		if (isInBufferNeeded == OMX_TRUE) {
			getInBuffer(shvpu_avcenc_Private,
				    &pInBuffer,
				    &inBufExchanged, &processInBufQueue);
		}

		/* invoke a mark event callback if required */
		if (pInBuffer &&
		    (pInBuffer->hMarkTargetComponent != NULL))
			handleEventMark(pComponent, pInBuffer);

		/* get an output buffer */
		if (isOutBufferNeeded == OMX_TRUE)
			isOutBufferNeeded =
				takeOutBuffer(shvpu_avcenc_Private,
					      &pOutBuffer,
					      &outBufExchanged);

		/* do encoding if a pair of
		   input and output buffers are ensured */
		if (shvpu_avcenc_Private->state == OMX_StateExecuting) {
			if (shvpu_avcenc_Private->isFirstBuffer) {
				generateHeader(pComponent, pOutBuffer);
			} else {
				if (pInBuffer)
					encodePicture(pComponent, pInBuffer,
						      &processInBufQueue);
				if (pOutBuffer) {
					fillOutBuffer(pComponent, pOutBuffer);
					checkFillDone(pComponent, &pOutBuffer,
						      &outBufExchanged,
						      &isOutBufferNeeded);
				}
			}
		} else if (!(PORT_IS_BEING_FLUSHED(pInPort) ||
			     PORT_IS_BEING_FLUSHED(pOutPort)) &&
			   (pInBuffer || pOutBuffer)) {
			DEBUG(DEB_LEV_ERR,
			      "In %s Received Buffer in non-"
			      "Executing State(%x)\n",
			      __func__,
			      (int)shvpu_avcenc_Private->state);
		}

		if (shvpu_avcenc_Private->state == OMX_StatePause
		    && !(PORT_IS_BEING_FLUSHED(pInPort)
			 || PORT_IS_BEING_FLUSHED(pOutPort))) {
			/*Waiting at paused state */
			tsem_wait(shvpu_avcenc_Private->bStateSem);
		}

		if (inBufExchanged > 0) {
			checkEmptyDone(shvpu_avcenc_Private,
				       &pInBuffer,
				       &processInBufQueue,
				       &inBufExchanged,
				       &isInBufferNeeded);
		}

	}

	DEBUG(DEB_LEV_FUNCTION_NAME, "Out of %s of component %x\n", __func__,
	      (int)pComponent);

	return NULL;
}
