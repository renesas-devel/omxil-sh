/**
   src/vpu/shvpu_avcdec_omx.c

   This component implements H.264 / MPEG-4 AVC video decoder.
   The H.264 / MPEG-4 AVC Video decoder is implemented on the
   Renesas's VPU5HG middleware library.

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
#include "shvpu_avcdec_omx.h"
#include <OMX_Video.h>

/** Maximum Number of Video Component Instance*/
#define MAX_COMPONENT_VIDEODEC 2
/** Counter of Video Component Instance*/
static OMX_U32 noVideoDecInstance = 0;

/** The output decoded color format */
#define OUTPUT_DECODED_COLOR_FMT OMX_COLOR_FormatYUV420Planar

#define DEFAULT_WIDTH 128
#define DEFAULT_HEIGHT 96
/** define the minimum input buffer size */
#define DEFAULT_VIDEO_OUTPUT_BUF_SIZE					\
	(DEFAULT_WIDTH * DEFAULT_HEIGHT * 3 / 2)	// YUV subQCIF

#define INPUT_BUFFER_COUNT 2
#define INPUT_BUFFER_SIZE 256000
/** The Constructor of the video decoder component
 * @param pComponent the component handle to be constructed
 * @param cComponentName is the name of the constructed component
 */

static OMX_PARAM_REVPU5MAXINSTANCE maxVPUInstances = {
	.nInstances = 1
};

OMX_ERRORTYPE
shvpu_avcdec_Constructor(OMX_COMPONENTTYPE * pComponent,
			 OMX_STRING cComponentName)
{

	OMX_ERRORTYPE eError = OMX_ErrorNone;
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private;
	omx_base_video_PortType *inPort, *outPort;
	OMX_U32 i;

	/* initialize component private data */
	if (!pComponent->pComponentPrivate) {
		DEBUG(DEB_LEV_FUNCTION_NAME, "In %s, allocating component\n",
		      __func__);
		pComponent->pComponentPrivate =
			calloc(1, sizeof(shvpu_avcdec_PrivateType));
		if (pComponent->pComponentPrivate == NULL) {
			return OMX_ErrorInsufficientResources;
		}
	} else {
		DEBUG(DEB_LEV_FUNCTION_NAME,
		      "In %s, Error Component %x Already Allocated\n",
		      __func__, (int)pComponent->pComponentPrivate);
	}

	shvpu_avcdec_Private = pComponent->pComponentPrivate;
	shvpu_avcdec_Private->ports = NULL;

	/* construct base filter */
	eError = omx_base_filter_Constructor(pComponent, cComponentName);

	shvpu_avcdec_Private->
		sPortTypesParam[OMX_PortDomainVideo].nStartPortNumber = 0;
	shvpu_avcdec_Private->sPortTypesParam[OMX_PortDomainVideo].nPorts = 2;

	/** Allocate Ports and call port constructor. */
	if (shvpu_avcdec_Private->sPortTypesParam[OMX_PortDomainVideo].nPorts
	    && !shvpu_avcdec_Private->ports) {
		shvpu_avcdec_Private->ports =
			calloc(shvpu_avcdec_Private->sPortTypesParam
			       [OMX_PortDomainVideo].nPorts,
			       sizeof(omx_base_PortType *));
		if (!shvpu_avcdec_Private->ports) {
			return OMX_ErrorInsufficientResources;
		}
		for (i = 0;
		     i <
			     shvpu_avcdec_Private->
			     sPortTypesParam[OMX_PortDomainVideo].nPorts; i++) {
			shvpu_avcdec_Private->ports[i] =
				calloc(1, sizeof(omx_base_video_PortType));
			if (!shvpu_avcdec_Private->ports[i]) {
				return OMX_ErrorInsufficientResources;
			}
		}
	}

	base_video_port_Constructor(pComponent,
				    &shvpu_avcdec_Private->ports[0], 0,
				    OMX_TRUE);
	base_video_port_Constructor(pComponent,
				    &shvpu_avcdec_Private->ports[1], 1,
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
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	inPort->sPortParam.nBufferSize = INPUT_BUFFER_SIZE; //max NAL /2 *DHG*/
	inPort->sPortParam.nBufferCountMin = INPUT_BUFFER_COUNT;
	inPort->sPortParam.nBufferCountActual = INPUT_BUFFER_COUNT;
	inPort->sPortParam.format.video.xFramerate = 0;
	inPort->sPortParam.format.video.eCompressionFormat =
		OMX_VIDEO_CodingAVC;

	//common parameters related to output port
	outPort =
		(omx_base_video_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	outPort->sPortParam.format.video.eColorFormat =
		OUTPUT_DECODED_COLOR_FMT;
	outPort->sPortParam.nBufferSize = DEFAULT_VIDEO_OUTPUT_BUF_SIZE;
	outPort->sPortParam.format.video.xFramerate = 0;

	/** settings of output port parameter definition */
	outPort->sVideoParam.eColorFormat = OUTPUT_DECODED_COLOR_FMT;
	outPort->sVideoParam.xFramerate = 0;

	/** now it's time to know the video coding type of the component */
	if (!strcmp(cComponentName, VIDEO_DEC_MPEG4_NAME)) {
		shvpu_avcdec_Private->video_coding_type =
			OMX_VIDEO_CodingMPEG4;
	} else if (!strcmp(cComponentName, VIDEO_DEC_H264_NAME)) {
		shvpu_avcdec_Private->video_coding_type = OMX_VIDEO_CodingAVC;
	} else if (!strcmp(cComponentName, VIDEO_DEC_BASE_NAME)) {
		shvpu_avcdec_Private->video_coding_type =
			OMX_VIDEO_CodingUnused;
	} else {
		// IL client specified an invalid component name
		return OMX_ErrorInvalidComponentName;
	}

	SetInternalVideoParameters(pComponent);

	shvpu_avcdec_Private->eOutFramePixFmt = 0;

	if (shvpu_avcdec_Private->video_coding_type ==
	    OMX_VIDEO_CodingMPEG4) {
		shvpu_avcdec_Private->
			ports[OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.format.
			video.eCompressionFormat = OMX_VIDEO_CodingMPEG4;
	} else {
		shvpu_avcdec_Private->
			ports[OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.format.
			video.eCompressionFormat = OMX_VIDEO_CodingAVC;
	}

	/** general configuration irrespective of any video formats
	 * setting other parameters of shvpu_avcdec_private
	 */
	shvpu_avcdec_Private->avCodec = NULL;
	shvpu_avcdec_Private->avCodecContext = NULL;
	shvpu_avcdec_Private->avcodecReady = OMX_FALSE;
	shvpu_avcdec_Private->extradata = NULL;
	shvpu_avcdec_Private->extradata_size = 0;

	/** initializing the codec context etc that was done earlier
	    by vpulibinit function */
	shvpu_avcdec_Private->BufferMgmtFunction =
		shvpu_avcdec_BufferMgmtFunction;
	shvpu_avcdec_Private->messageHandler = shvpu_avcdec_MessageHandler;
	shvpu_avcdec_Private->destructor = shvpu_avcdec_Destructor;
	pComponent->SetParameter = shvpu_avcdec_SetParameter;
	pComponent->GetParameter = shvpu_avcdec_GetParameter;
	pComponent->ComponentRoleEnum = shvpu_avcdec_ComponentRoleEnum;
	pComponent->GetExtensionIndex = shvpu_avcdec_GetExtensionIndex;

	shvpu_avcdec_Private->pPicQueue = calloc(1, sizeof(queue_t));
	queue_init(shvpu_avcdec_Private->pPicQueue);
	shvpu_avcdec_Private->pPicSem = calloc(1, sizeof(tsem_t));
	tsem_init(shvpu_avcdec_Private->pPicSem, 0);
	shvpu_avcdec_Private->pNalQueue = calloc(1, sizeof(queue_t));
	queue_init(shvpu_avcdec_Private->pNalQueue);
	shvpu_avcdec_Private->pNalSem = calloc(1, sizeof(tsem_t));
	tsem_init(shvpu_avcdec_Private->pNalSem, 0);

	noVideoDecInstance++;

	if (noVideoDecInstance > maxVPUInstances.nInstances)   {
		noVideoDecInstance--;
		return OMX_ErrorInsufficientResources;
	}
	tsem_init(&shvpu_avcdec_Private->uio_sem, 0);
	tsem_init(&shvpu_avcdec_Private->return_sem, 0);
	return eError;
}

/** The destructor of the video decoder component
 */
OMX_ERRORTYPE shvpu_avcdec_Destructor(OMX_COMPONENTTYPE * pComponent)
{
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		pComponent->pComponentPrivate;
	OMX_U32 i;

	if (shvpu_avcdec_Private->extradata) {
		free(shvpu_avcdec_Private->extradata);
		shvpu_avcdec_Private->extradata = NULL;
	}

	/* frees port/s */
	if (shvpu_avcdec_Private->ports) {
		for (i = 0;
		     i < shvpu_avcdec_Private->
			     sPortTypesParam[OMX_PortDomainVideo].nPorts; i++) {
			if (shvpu_avcdec_Private->ports[i])
				shvpu_avcdec_Private->
					ports[i]->PortDestructor
					(shvpu_avcdec_Private->ports[i]);
		}
		free(shvpu_avcdec_Private->ports);
		shvpu_avcdec_Private->ports = NULL;
	}

	DEBUG(DEB_LEV_FUNCTION_NAME,
	      "Destructor of video decoder component is called\n");

	omx_base_filter_Destructor(pComponent);
	noVideoDecInstance--;

	return OMX_ErrorNone;
}

static void
handle_vpu5intr(void *arg)
{
	MCVDEC_CONTEXT_T *context = arg;
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		(shvpu_avcdec_PrivateType *)context->user_info;

	logd("----- invoke mciph_vpu5_int_handler() -----\n");
	mciph_vpu5_int_handler(shvpu_avcdec_Private->avCodec->drvInfo);
	logd("----- resume from mciph_vpu5_int_handler() -----\n");
	return;
}

/** It initializates the VPU framework, and opens an VPU videodecoder
    of type specified by IL client
*/
OMX_ERRORTYPE
shvpu_avcdec_vpuLibInit(shvpu_avcdec_PrivateType * shvpu_avcdec_Private)
{
	unsigned int reg, mem;
	size_t memsz;
	int ret;

	DEBUG(DEB_LEV_SIMPLE_SEQ, "VPU library/codec initialized\n");
	/* initialize a vpu uio */
	uio_init("VPU", &reg, &mem, &memsz);
	loge("reg = %x, mem = %x, memsz = %d\n",
	     reg, mem, memsz);

	uiomux_lock_vpu();
	/* initialize the decoder middleware */
	ret = decode_init(shvpu_avcdec_Private);
	if (ret != MCVDEC_NML_END) {
		loge("decode_init() failed (%ld)\n", ret);
		return OMX_ErrorInsufficientResources;
	}
	uiomux_unlock_vpu();

	/* register an interrupt handler */
	uio_create_int_handle(&shvpu_avcdec_Private->uioIntrThread,
			      handle_vpu5intr,
			      shvpu_avcdec_Private->avCodecContext,
				&shvpu_avcdec_Private->uio_sem,
				&shvpu_avcdec_Private->return_sem,
				&shvpu_avcdec_Private->exit_handler);

	return OMX_ErrorNone;
}

/** It Deinitializates the vpu framework, and close the vpu video
    decoder of selected coding type
*/
void
shvpu_avcdec_vpuLibDeInit(shvpu_avcdec_PrivateType *
			  shvpu_avcdec_Private)
{
	int err;
	if (shvpu_avcdec_Private) {
		uiomux_lock_vpu();
		decode_deinit(shvpu_avcdec_Private);

		uio_exit_handler( &shvpu_avcdec_Private->uio_sem,
			&shvpu_avcdec_Private->return_sem,
			&shvpu_avcdec_Private->exit_handler);
		uiomux_unlock_vpu();

		pthread_join(shvpu_avcdec_Private->uioIntrThread, NULL);
		uio_deinit();
	}
}

/** internal function to set codec related parameters in the private
    type structure
*/
void
SetInternalVideoParameters(OMX_COMPONENTTYPE * pComponent)
{

	shvpu_avcdec_PrivateType *shvpu_avcdec_Private;
	omx_base_video_PortType *inPort;

	shvpu_avcdec_Private = pComponent->pComponentPrivate;;

	if (shvpu_avcdec_Private->video_coding_type == OMX_VIDEO_CodingMPEG4) {
		strcpy(shvpu_avcdec_Private->ports
		       [OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.format.
		       video.cMIMEType, "video/mpeg4");
		shvpu_avcdec_Private->
			ports[OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.format.
			video.eCompressionFormat = OMX_VIDEO_CodingMPEG4;

		setHeader(&shvpu_avcdec_Private->pVideoMpeg4,
			  sizeof(OMX_VIDEO_PARAM_MPEG4TYPE));
		shvpu_avcdec_Private->pVideoMpeg4.nPortIndex = 0;
		shvpu_avcdec_Private->pVideoMpeg4.nSliceHeaderSpacing = 0;
		shvpu_avcdec_Private->pVideoMpeg4.bSVH = OMX_FALSE;
		shvpu_avcdec_Private->pVideoMpeg4.bGov = OMX_FALSE;
		shvpu_avcdec_Private->pVideoMpeg4.nPFrames = 0;

		shvpu_avcdec_Private->pVideoMpeg4.nBFrames = 0;
		shvpu_avcdec_Private->pVideoMpeg4.nIDCVLCThreshold = 0;
		shvpu_avcdec_Private->pVideoMpeg4.bACPred = OMX_FALSE;
		shvpu_avcdec_Private->pVideoMpeg4.nMaxPacketSize = 0;
		shvpu_avcdec_Private->pVideoMpeg4.nTimeIncRes = 0;
		shvpu_avcdec_Private->pVideoMpeg4.eProfile =
			OMX_VIDEO_MPEG4ProfileSimple;
		shvpu_avcdec_Private->pVideoMpeg4.eLevel =
			OMX_VIDEO_MPEG4Level0;
		shvpu_avcdec_Private->pVideoMpeg4.nAllowedPictureTypes = 0;
		shvpu_avcdec_Private->pVideoMpeg4.nHeaderExtension = 0;
		shvpu_avcdec_Private->pVideoMpeg4.bReversibleVLC = OMX_FALSE;

		inPort =
			(omx_base_video_PortType *)
			shvpu_avcdec_Private->ports
			[OMX_BASE_FILTER_INPUTPORT_INDEX];
		inPort->sVideoParam.eCompressionFormat =
			OMX_VIDEO_CodingMPEG4;

	} else if (shvpu_avcdec_Private->video_coding_type ==
		   OMX_VIDEO_CodingAVC) {
		strcpy(shvpu_avcdec_Private->ports
		       [OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.format.
		       video.cMIMEType, "video/avc(h264)");
		shvpu_avcdec_Private->
			ports[OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.format.
			video.eCompressionFormat = OMX_VIDEO_CodingAVC;

		setHeader(&shvpu_avcdec_Private->pVideoAvc,
			  sizeof(OMX_VIDEO_PARAM_AVCTYPE));
		shvpu_avcdec_Private->pVideoAvc.nPortIndex = 0;
		shvpu_avcdec_Private->pVideoAvc.nSliceHeaderSpacing = 0;
		shvpu_avcdec_Private->pVideoAvc.bUseHadamard = OMX_FALSE;
		shvpu_avcdec_Private->pVideoAvc.nRefFrames = 2;
		shvpu_avcdec_Private->pVideoAvc.nPFrames = 0;
		shvpu_avcdec_Private->pVideoAvc.nBFrames = 0;
		shvpu_avcdec_Private->pVideoAvc.bUseHadamard = OMX_FALSE;
		shvpu_avcdec_Private->pVideoAvc.nRefFrames = 2;
		shvpu_avcdec_Private->pVideoAvc.eProfile =
			OMX_VIDEO_AVCProfileBaseline;
		shvpu_avcdec_Private->pVideoAvc.eLevel = OMX_VIDEO_AVCLevel1;
		shvpu_avcdec_Private->pVideoAvc.nAllowedPictureTypes = 0;
		shvpu_avcdec_Private->pVideoAvc.bFrameMBsOnly = OMX_FALSE;
		shvpu_avcdec_Private->pVideoAvc.nRefIdx10ActiveMinus1 = 0;
		shvpu_avcdec_Private->pVideoAvc.nRefIdx11ActiveMinus1 = 0;
		shvpu_avcdec_Private->pVideoAvc.bEnableUEP = OMX_FALSE;
		shvpu_avcdec_Private->pVideoAvc.bEnableFMO = OMX_FALSE;
		shvpu_avcdec_Private->pVideoAvc.bEnableASO = OMX_FALSE;
		shvpu_avcdec_Private->pVideoAvc.bEnableRS = OMX_FALSE;

		shvpu_avcdec_Private->pVideoAvc.bMBAFF = OMX_FALSE;
		shvpu_avcdec_Private->pVideoAvc.bEntropyCodingCABAC =
			OMX_FALSE;
		shvpu_avcdec_Private->pVideoAvc.bWeightedPPrediction =
			OMX_FALSE;
		shvpu_avcdec_Private->pVideoAvc.nWeightedBipredicitonMode = 0;
		shvpu_avcdec_Private->pVideoAvc.bconstIpred = OMX_FALSE;
		shvpu_avcdec_Private->pVideoAvc.bDirect8x8Inference =
			OMX_FALSE;
		shvpu_avcdec_Private->pVideoAvc.bDirectSpatialTemporal =
			OMX_FALSE;
		shvpu_avcdec_Private->pVideoAvc.nCabacInitIdc = 0;
		shvpu_avcdec_Private->pVideoAvc.eLoopFilterMode =
			OMX_VIDEO_AVCLoopFilterDisable;

	/*OMX_VIDEO_PARAM_PROFILELEVELTYPE*/
		setHeader(&shvpu_avcdec_Private->pVideoProfile[0],
			  sizeof(OMX_VIDEO_PARAM_PROFILELEVELTYPE));
		shvpu_avcdec_Private->pVideoProfile[0].eProfile =
			OMX_VIDEO_AVCProfileBaseline;
		shvpu_avcdec_Private->pVideoProfile[0].eLevel =
			OMX_VIDEO_AVCLevel3;
		shvpu_avcdec_Private->pVideoProfile[0].nProfileIndex = 0;

		setHeader(&shvpu_avcdec_Private->pVideoProfile[1],
			  sizeof(OMX_VIDEO_PARAM_PROFILELEVELTYPE));
		shvpu_avcdec_Private->pVideoProfile[1].eProfile =
			OMX_VIDEO_AVCProfileMain;
		shvpu_avcdec_Private->pVideoProfile[1].eLevel =
			OMX_VIDEO_AVCLevel41;
		shvpu_avcdec_Private->pVideoProfile[1].nProfileIndex = 1;

		setHeader(&shvpu_avcdec_Private->pVideoProfile[2],
			  sizeof(OMX_VIDEO_PARAM_PROFILELEVELTYPE));
		shvpu_avcdec_Private->pVideoProfile[2].eProfile =
			OMX_VIDEO_AVCProfileHigh;
		shvpu_avcdec_Private->pVideoProfile[2].eLevel =
			OMX_VIDEO_AVCLevel31;
		shvpu_avcdec_Private->pVideoProfile[2].nProfileIndex = 2;

		memcpy(&shvpu_avcdec_Private->pVideoCurrentProfile,
			&shvpu_avcdec_Private->pVideoProfile[0],
			sizeof (OMX_VIDEO_PARAM_PROFILELEVELTYPE));
	/*OMX_PARAM_REVPU5MAXPARAM*/
		setHeader(&shvpu_avcdec_Private->maxVideoParameters,
			  sizeof(OMX_PARAM_REVPU5MAXPARAM));
		shvpu_avcdec_Private->maxVideoParameters.nWidth = 1280;
		shvpu_avcdec_Private->maxVideoParameters.nHeight = 720;
		shvpu_avcdec_Private->maxVideoParameters.eVPU5AVCLevel = OMX_VPU5AVCLevel31;
		/*OMX_PARAM_REVPU5MAXINSTANCE*/
		setHeader(&maxVPUInstances,
			sizeof (OMX_PARAM_REVPU5MAXINSTANCE));

		inPort =
			(omx_base_video_PortType *)
			shvpu_avcdec_Private->ports
			[OMX_BASE_FILTER_INPUTPORT_INDEX];
		inPort->sVideoParam.eCompressionFormat = OMX_VIDEO_CodingAVC;
	}
}

/** The Initialization function of the video decoder
 */
OMX_ERRORTYPE
shvpu_avcdec_Init(OMX_COMPONENTTYPE * pComponent)
{

	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		pComponent->pComponentPrivate;
	OMX_ERRORTYPE eError = OMX_ErrorNone;

	/** Temporary First Output buffer size */
	shvpu_avcdec_Private->inputCurrBuffer = NULL;
	shvpu_avcdec_Private->inputCurrLength = 0;
	shvpu_avcdec_Private->isFirstBuffer = OMX_TRUE;
	shvpu_avcdec_Private->isNewBuffer = 1;

	return eError;
}

/** The Deinitialization function of the video decoder
 */
OMX_ERRORTYPE
shvpu_avcdec_Deinit(OMX_COMPONENTTYPE * pComponent)
{

	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		pComponent->pComponentPrivate;
	OMX_ERRORTYPE eError = OMX_ErrorNone;

	if (shvpu_avcdec_Private->avcodecReady) {
		shvpu_avcdec_vpuLibDeInit(shvpu_avcdec_Private);
		shvpu_avcdec_Private->avcodecReady = OMX_FALSE;
	}

	return eError;
}

/** Executes all the required steps after an output buffer
    frame-size has changed.
*/
static inline void
UpdateFrameSize(OMX_COMPONENTTYPE * pComponent)
{
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		pComponent->pComponentPrivate;
	omx_base_video_PortType *outPort =
		(omx_base_video_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	omx_base_video_PortType *inPort =
		(omx_base_video_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	outPort->sPortParam.format.video.nFrameWidth =
		inPort->sPortParam.format.video.nFrameWidth;
	outPort->sPortParam.format.video.nFrameHeight =
		inPort->sPortParam.format.video.nFrameHeight;
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
}

static inline void
handle_buffer_flush(shvpu_avcdec_PrivateType *shvpu_avcdec_Private,
		    OMX_BOOL *pIsInBufferNeeded,
		    OMX_BOOL *pIsOutBufferNeeded,
		    int *pInBufExchanged, int *pOutBufExchanged,
		    OMX_BUFFERHEADERTYPE *pInBuffer[],
		    OMX_BUFFERHEADERTYPE **ppOutBuffer)
{
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	tsem_t *pInputSem = pInPort->pBufferSem;
	tsem_t *pOutputSem = pOutPort->pBufferSem;
	int i;

	pthread_mutex_lock(&shvpu_avcdec_Private->flush_mutex);
	while (PORT_IS_BEING_FLUSHED(pInPort) ||
	       PORT_IS_BEING_FLUSHED(pOutPort)) {
		pthread_mutex_unlock
			(&shvpu_avcdec_Private->flush_mutex);

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

		if (*pIsInBufferNeeded == OMX_FALSE
		    && PORT_IS_BEING_FLUSHED(pInPort)) {
			for (i=0; i<2; i++) {
				if (!pInBuffer[i])
					continue;
				pInPort->ReturnBufferFunction(pInPort,
							      pInBuffer[i]);
				pInBuffer[i] = NULL;
				(*pInBufExchanged)--;
			}
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

		tsem_up(shvpu_avcdec_Private->flush_all_condition);
		tsem_down(shvpu_avcdec_Private->flush_condition);
		pthread_mutex_lock(&shvpu_avcdec_Private->
				   flush_mutex);
	}
	pthread_mutex_unlock(&shvpu_avcdec_Private->flush_mutex);

	return;
}

static inline int
waitBuffers(shvpu_avcdec_PrivateType *shvpu_avcdec_Private,
	     OMX_BOOL isInBufferNeeded, OMX_BOOL isOutBufferNeeded)
{
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
	tsem_t *pPicSem = shvpu_avcdec_Private->pPicSem;
	tsem_t *pInputSem = pInPort->pBufferSem;
	tsem_t *pOutputSem = pOutPort->pBufferSem;

	if ((isInBufferNeeded == OMX_TRUE &&
	     pPicSem->semval == 0 && pInputSem->semval == 0) &&
	    (shvpu_avcdec_Private->state != OMX_StateLoaded &&
	     shvpu_avcdec_Private->state != OMX_StateInvalid)) {
		//Signalled from EmptyThisBuffer or
		//FillThisBuffer or some thing else
		DEBUG(DEB_LEV_FULL_SEQ,
		      "Waiting for next input/output buffer\n");
		tsem_down(shvpu_avcdec_Private->bMgmtSem);

	}

	if (shvpu_avcdec_Private->state == OMX_StateLoaded
	    || shvpu_avcdec_Private->state == OMX_StateInvalid) {
		DEBUG(DEB_LEV_SIMPLE_SEQ,
		      "In %s Buffer Management Thread is exiting\n",
		      __func__);
		return -1;
	}

	if ((isOutBufferNeeded == OMX_TRUE &&
	     pOutputSem->semval == 0) &&
	    (shvpu_avcdec_Private->state != OMX_StateLoaded &&
	     shvpu_avcdec_Private->state != OMX_StateInvalid) &&
	    !(PORT_IS_BEING_FLUSHED(pInPort) ||
	      PORT_IS_BEING_FLUSHED(pOutPort))) {
		//Signalled from EmptyThisBuffer or
		//FillThisBuffer or some thing else
		DEBUG(DEB_LEV_FULL_SEQ,
		      "Waiting for next input/output buffer\n");
		tsem_down(shvpu_avcdec_Private->bMgmtSem);

	}

	if (shvpu_avcdec_Private->state == OMX_StateLoaded ||
	    shvpu_avcdec_Private->state == OMX_StateInvalid) {
		DEBUG(DEB_LEV_SIMPLE_SEQ,
		      "In %s Buffer Management Thread is exiting\n",
		      __func__);
		return -1;
	}

	return 0;
}

static inline OMX_BOOL
getInBuffer(shvpu_avcdec_PrivateType *shvpu_avcdec_Private,
	    OMX_BUFFERHEADERTYPE **ppInBuffer,
	    int *pInBufExchanged, queue_t *pProcessInBufQueue)
{
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
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
		loge("%s: hMarkTargetComponent = %p\n",
		     __FUNCTION__, (*ppInBuffer)->hMarkTargetComponent);
		loge("%s: pMarkData = %p\n", __FUNCTION__,
		     (*ppInBuffer)->pMarkData);
		loge("%s: nTimeStamp = %d\n", __FUNCTION__,
		     (*ppInBuffer)->nTimeStamp);
		loge("%s: nFlags = %08x\n", __FUNCTION__,
		     (*ppInBuffer)->nFlags);
	}

	(*pInBufExchanged)++;
	queue(pProcessInBufQueue, *ppInBuffer);

	return OMX_FALSE;
}

static inline OMX_BOOL
takeOutBuffer(shvpu_avcdec_PrivateType *shvpu_avcdec_Private,
	      OMX_BUFFERHEADERTYPE **ppOutBuffer,
	      int *pOutBufExchanged)
{
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
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
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		(shvpu_avcdec_PrivateType *) pComponent->pComponentPrivate;

	if ((OMX_COMPONENTTYPE *)pInBuffer->hMarkTargetComponent ==
	    (OMX_COMPONENTTYPE *)pComponent) {
		/*Clear the mark and generate an event */
		(*(shvpu_avcdec_Private->callbacks->EventHandler))
			(pComponent, shvpu_avcdec_Private->callbackData,
			 OMX_EventMark,	/* The command was completed */
			 1,		/* The commands was a
				   	   OMX_CommandStateSet */
			 0,		/* The state has been changed
					   in message->messageParam2 */
			 pInBuffer->pMarkData);
	} else {
		/*If this is not the target component then pass the mark */
		shvpu_avcdec_Private->pMark.hMarkTargetComponent =
			pInBuffer->hMarkTargetComponent;
		shvpu_avcdec_Private->pMark.pMarkData =
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
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		pComponent->pComponentPrivate;
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];

	/*If EOS and Input buffer Filled Len Zero
	  then Return output buffer immediately */
	if (((*ppOutBuffer)->nFilledLen == 0) &&
	    !((*ppOutBuffer)->nFlags & OMX_BUFFERFLAG_EOS))
		return;

	if ((*ppOutBuffer)->nFlags & OMX_BUFFERFLAG_EOS) {
	        (*(shvpu_avcdec_Private->callbacks->EventHandler))
			(pComponent,
			 shvpu_avcdec_Private->callbackData,
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
checkEmptyDone(shvpu_avcdec_PrivateType *shvpu_avcdec_Private,
	       queue_t *pInBufQueue, int *pInBufExchanged)
{
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
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

		loge("send EmptyBufferDone(%p,%08x)\n",
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
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		(shvpu_avcdec_PrivateType *) pComponent->pComponentPrivate;
	omx_base_PortType *pInPort =
		(omx_base_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
	omx_base_PortType *pOutPort =
		(omx_base_PortType *)
		shvpu_avcdec_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
 	tsem_t *pInputSem = pInPort->pBufferSem;
	tsem_t *pOutputSem = pOutPort->pBufferSem;
	queue_t *pInputQueue = pInPort->pBufferQueue;
	queue_t *pOutputQueue = pOutPort->pBufferQueue;
	OMX_BUFFERHEADERTYPE *pOutBuffer = NULL;
	OMX_BUFFERHEADERTYPE *pInBuffer[2] = { NULL, NULL };
	OMX_BOOL isInBufferNeeded = OMX_TRUE,
		isOutBufferNeeded = OMX_TRUE;
	int inBufExchanged = 0, outBufExchanged = 0;
	tsem_t *pPicSem = shvpu_avcdec_Private->pPicSem;
	queue_t *pPicQueue = shvpu_avcdec_Private->pPicQueue;
	queue_t processInBufQueue;
	pic_t *pPic;
	nal_t *pNal = NULL;
	size_t remain = 0;
	int ret;

	shvpu_avcdec_Private->bellagioThreads->nThreadBufferMngtID =
		(long int)syscall(__NR_gettid);
	DEBUG(DEB_LEV_FUNCTION_NAME, "In %s of component %x\n", __func__,
	      (int)pComponent);
	DEBUG(DEB_LEV_SIMPLE_SEQ, "In %s the thread ID is %i\n", __func__,
	      (int)shvpu_avcdec_Private->bellagioThreads->
	      nThreadBufferMngtID);

	queue_init(&processInBufQueue);

	DEBUG(DEB_LEV_FUNCTION_NAME, "In %s\n", __func__);
	while (shvpu_avcdec_Private->state == OMX_StateIdle
	       || shvpu_avcdec_Private->state == OMX_StateExecuting
	       || shvpu_avcdec_Private->state == OMX_StatePause
	       || shvpu_avcdec_Private->transientState ==
	       OMX_TransStateLoadedToIdle) {

		/*Wait till the ports are being flushed */
		handle_buffer_flush(shvpu_avcdec_Private,
				    &isInBufferNeeded,
				    &isOutBufferNeeded,
				    &inBufExchanged, &outBufExchanged,
				    pInBuffer, &pOutBuffer);

		/*No buffer to process. So wait here */
		ret = waitBuffers(shvpu_avcdec_Private,
				   isInBufferNeeded,
				   isOutBufferNeeded);
		if (ret < 0)
			break;

		if ((isInBufferNeeded == OMX_TRUE) &&
		    (pInputSem->semval > 0)) {
			pInBuffer[1] = pInBuffer[0];
			getInBuffer(shvpu_avcdec_Private,
				    &pInBuffer[0],
				    &inBufExchanged, &processInBufQueue);
			/* TODO: error check for pInBuffer[0] */
			if (pNal) {
				pNal->pBuffer[1] = pInBuffer[0];
			} else {
				void *pHead;
				pNal = calloc(1, sizeof(nal_t));
				skipFirstPadding(pInBuffer[0]);
				pNal->pBuffer[0] = pInBuffer[0];
				pNal->pBuffer[1] = NULL;
				pNal->offset = pInBuffer[0]->nOffset;
				pNal->size = pInBuffer[0]->nFilledLen;
			}
			isInBufferNeeded = OMX_FALSE;
		}

		if ((pInBuffer[0]) &&
		    (pInBuffer[0]->hMarkTargetComponent != NULL))
			handleEventMark(pComponent, pInBuffer[0]);

		/* Split the input buffer into NALs and pictures */
		if (pNal && (pPicSem->semval == 0) &&
		    (shvpu_avcdec_Private->bIsEOSReached == OMX_FALSE))
			pNal = parseBuffer(pComponent,
					   pNal,
					   &isInBufferNeeded);

		/*When we have input buffer to process then get
		  one output buffer */
		if ((isOutBufferNeeded == OMX_TRUE) &&
		    (pOutputSem->semval > 0))
			isOutBufferNeeded =
				takeOutBuffer(shvpu_avcdec_Private,
					      &pOutBuffer,
					      &outBufExchanged);

		if (((pPicSem->semval > 0) ||
		     shvpu_avcdec_Private->bIsEOSReached) &&
		    (isOutBufferNeeded == OMX_FALSE)) {

			if (shvpu_avcdec_Private->state ==
			    OMX_StateExecuting) {
				uiomux_lock_vpu();
				shvpu_avcdec_DecodePicture(pComponent,
							   pOutBuffer);
				uiomux_unlock_vpu();
			}
			else if (!(PORT_IS_BEING_FLUSHED(pInPort) ||
				   PORT_IS_BEING_FLUSHED(pOutPort))) {
				DEBUG(DEB_LEV_ERR,
				      "In %s Received Buffer in non-"
				      "Executing State(%x)\n",
				      __func__,
				      (int)shvpu_avcdec_Private->state);
			} else if (pInBuffer[0]) {
				pInBuffer[0]->nFilledLen = 0;
			}

			if (shvpu_avcdec_Private->state == OMX_StatePause
			    && !(PORT_IS_BEING_FLUSHED(pInPort)
				 || PORT_IS_BEING_FLUSHED(pOutPort))) {
				/*Waiting at paused state */
				tsem_wait(shvpu_avcdec_Private->bStateSem);
			}

			checkFillDone(pComponent,
					&pOutBuffer,
					&outBufExchanged,
					&isOutBufferNeeded);
		}

		if (shvpu_avcdec_Private->state == OMX_StatePause
		    && !(PORT_IS_BEING_FLUSHED(pInPort)
			 || PORT_IS_BEING_FLUSHED(pOutPort))) {
			/*Waiting at paused state */
			tsem_wait(shvpu_avcdec_Private->bStateSem);
		}

		if (inBufExchanged > 0)
			checkEmptyDone(shvpu_avcdec_Private,
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

	loge("mcvdec_get_error_info() = %d\n", ret);
	loge("errinfo.dec_status = %ld\n", errinfo.dec_status);
	loge("errinfo.refs_status = %ld\n", errinfo.refs_status);
	loge("errinfo.hdr_err_erc = %ld\n", errinfo.hdr_err_erc);
	loge("errinfo.hdr_err_elvl = %ld\n", errinfo.hdr_err_elvl);
	loge("errinfo.hdr_err_strm_idx = %ld\n", errinfo.hdr_err_strm_idx);
	loge("errinfo.hdr_err_strm_ofs = %ld\n", errinfo.hdr_err_strm_ofs);
	loge("errinfo.vlc_err_esrc = %lx\n", errinfo.vlc_err_esrc);
	loge("errinfo.vlc_err_elvl = %lx\n", errinfo.vlc_err_elvl);
	loge("errinfo.vlc_err_sn = %lx\n", errinfo.vlc_err_sn);
	loge("errinfo.vlc_err_mbh = %lx\n", errinfo.vlc_err_mbh);
	loge("errinfo.vlc_err_mbv = %lx\n", errinfo.vlc_err_mbv);
	loge("errinfo.vlc_err_erc = %lx\n", errinfo.vlc_err_erc);
	loge("errinfo.vlc_err_sbcv = %lx\n", errinfo.vlc_err_sbcv);
	loge("errinfo.ce_err_erc = %lx\n", errinfo.ce_err_erc);
	loge("errinfo.ce_err_epy = %lx\n", errinfo.ce_err_epy);
	loge("errinfo.ce_err_epx = %lx\n", errinfo.ce_err_epx);

	return ret;
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
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private;
	shvpu_codec_t *pCodec;
	MCVDEC_CONTEXT_T *pCodecContext;
	OMX_ERRORTYPE err = OMX_ErrorNone;
	size_t size, len;
	nal_t *nal;
	int i, j, off;
	OMX_U8 *pbuf;
	long ret, hdr_ready;

	shvpu_avcdec_Private = pComponent->pComponentPrivate;
#if 1
	hdr_ready = MCVDEC_ON;
	pCodec = shvpu_avcdec_Private->avCodec;
	pCodecContext = shvpu_avcdec_Private->avCodecContext;

	if (shvpu_avcdec_Private->bIsEOSReached &&
	    (pCodec->bufferingCount == 0)) {
		printf("finalize\n");
		shvpu_avcdec_Private->bIsEOSReached = OMX_FALSE;
		pOutBuffer->nFlags |= OMX_BUFFERFLAG_EOS;
		return;
	}

	tsem_up(&shvpu_avcdec_Private->uio_sem);

	logd("----- invoke mcvdec_decode_picture() -----\n");
	ret = mcvdec_decode_picture(pCodecContext,
				    &shvpu_avcdec_Private->avPicInfo,
				    pCodec->codecMode,
				    &hdr_ready);
	logd("----- resume from mcvdec_decode_picture() = %d -----\n", ret);
	logd("hdr_ready = %s\n", (hdr_ready == MCVDEC_ON) ?
	     "MCVDEC_ON" : "MCVDEC_OFF");

	if (pCodec->codecMode == MCVDEC_MODE_BUFFERING) {
		if (hdr_ready == MCVDEC_ON) {
			pCodec->enoughHeaders = OMX_TRUE;
			if (pCodec->enoughPreprocess)
				if (shvpu_avcdec_Private->enable_sync)
					pCodec->codecMode = MCVDEC_MODE_SYNC;
				else
					pCodec->codecMode = MCVDEC_MODE_MAIN;
		}
		uio_wakeup();
		tsem_down(&shvpu_avcdec_Private->return_sem);
		return;
	}

	switch (ret) {
	case MCVDEC_CAUTION:
	case MCVDEC_CONCEALED_1:
	case MCVDEC_CONCEALED_2:
		loge("an error(%d) recoverd.\n", ret);
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
		err = OMX_ErrorUnderflow;
		loge("nothing to decode (%d)\n", ret);
		break;
	case MCVDEC_RESOURCE_LACK:
	case MCVDEC_ERR_FMEM:
	case MCVDEC_NO_FMEM_TO_WRITE:
		err = OMX_ErrorInsufficientResources;
		break;
	case MCVDEC_NML_END:
		break;
	}

	if (err != OMX_ErrorNone) {
		(*(shvpu_avcdec_Private->callbacks->EventHandler))
			(pComponent, shvpu_avcdec_Private->callbackData,
			OMX_EventError, // An error occured
			err, 		// Error code
			0, NULL);
		if (err == OMX_ErrorInvalidState)
			shvpu_avcdec_Private->state = OMX_StateInvalid;
	}

	/* port status */
	if (shvpu_avcdec_Private->avPicInfo &&
	    (ret == MCVDEC_NML_END)) {
		omx_base_video_PortType *inPort =
			(omx_base_video_PortType *)
			shvpu_avcdec_Private->
			ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
		long xpic, ypic;
		xpic = shvpu_avcdec_Private->avPicInfo->xpic_size -
			shvpu_avcdec_Private->avPicInfo->
			frame_crop[MCVDEC_CROP_LEFT] -
			shvpu_avcdec_Private->avPicInfo->
			frame_crop[MCVDEC_CROP_RIGHT];
		ypic = shvpu_avcdec_Private->avPicInfo->ypic_size -
			shvpu_avcdec_Private->avPicInfo->
			frame_crop[MCVDEC_CROP_TOP] -
			shvpu_avcdec_Private->avPicInfo->
			frame_crop[MCVDEC_CROP_BOTTOM];
		if((inPort->sPortParam.format.video.nFrameWidth != xpic) ||
		   (inPort->sPortParam.format.video.nFrameHeight != ypic)) {
			if ((xpic > shvpu_avcdec_Private->
					maxVideoParameters.nWidth) || (ypic >
					shvpu_avcdec_Private->
					maxVideoParameters.nHeight) ) {

			    (*(shvpu_avcdec_Private->callbacks->EventHandler))
			    (pComponent, shvpu_avcdec_Private->callbackData,
			    OMX_EventError, // An error occured
			    OMX_ErrorStreamCorrupt, // Error code
			    0, NULL);
			}

			DEBUG(DEB_LEV_SIMPLE_SEQ, "Sending Port Settings Change Event in video decoder\n");

			switch(shvpu_avcdec_Private->video_coding_type) {
			case OMX_VIDEO_CodingMPEG4 :
			case OMX_VIDEO_CodingAVC :
				inPort->sPortParam.format.video.nFrameWidth =
					xpic;
				inPort->sPortParam.format.video.nFrameHeight =
					ypic;
				break;
			default :
				shvpu_avcdec_Private->state = OMX_StateInvalid;
				DEBUG(DEB_LEV_ERR, "Video formats other than MPEG-4 AVC not supported\nCodec not found\n");
				err = OMX_ErrorFormatNotDetected;
				break;
			}

			UpdateFrameSize (pComponent);

			/** Send Port Settings changed call back */
			(*(shvpu_avcdec_Private->callbacks->EventHandler))
				(pComponent,
				 shvpu_avcdec_Private->callbackData,
				 OMX_EventPortSettingsChanged, // The command was completed
				 0,  //to adjust the file pointer to resume the correct decode process
				 0, // This is the input port index
				 NULL);
		}
	}

	logd("----- invoke mcvdec_get_output_picture() -----\n");
	loge("pCodec->bufferingCount = %d\n", pCodec->bufferingCount);
	ret = mcvdec_get_output_picture(pCodecContext,
					pic_infos, &frame,
					MCVDEC_OUTMODE_PUSH);
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
		(*(shvpu_avcdec_Private->callbacks->EventHandler))
			(pComponent, shvpu_avcdec_Private->callbackData,
			OMX_EventError, // An error occured
			err, 		// Error code
			0, NULL);
		if (err == OMX_ErrorInvalidState)
			shvpu_avcdec_Private->state = OMX_StateInvalid;
	}

	if ((ret == MCVDEC_NML_END) && pic_infos[0] && frame) {
		OMX_U8 *pOut = pOutBuffer->pBuffer;
		void *vaddr;
		size_t pic_size;
		int i;
		buffer_metainfo_t *pBMI;
		queue_t *pBMIQueue = pCodec->pBMIQueue;

		logd("pic_infos[0]->frame_cnt = %d\n",
		     pic_infos[0]->frame_cnt);
		logd("pic_infos[0]->fmem_index = %d\n",
		     pic_infos[0]->fmem_index);
		loge("pic_infos[0]->strm_id = %d\n",
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
		vaddr = uio_phys_to_virt(frame->Ypic_addr);
		if ((pic_size / 2 * 3) > pOutBuffer->nAllocLen) {
			loge("WARNING: shrink output size %d to %d\n",
			     pic_size / 2 * 3, pOutBuffer->nAllocLen);
			pic_size = pOutBuffer->nAllocLen / 3 * 2;
		}
		memcpy(pOut, vaddr, pic_size);
		vaddr = uio_phys_to_virt(frame->Cpic_addr);
		pOut += pic_size;
		memcpy(pOut, vaddr, pic_size / 2);
		pOutBuffer->nFilledLen += pic_size + pic_size / 2;

		/* receive an appropriate metadata */
		if (pBMIQueue->nelem > 0) {
			pBMI = shvpu_dequeue(pBMIQueue);
			if (pBMI->id == pic_infos[0]->strm_id) {
				pOutBuffer->nTimeStamp = pBMI->nTimeStamp;
				pOutBuffer->nFlags = pBMI->nFlags;
			} else {
				loge("FATAL: got incorrect BMI (%d != %d)\n",
				     pBMI->id, pic_infos[0]->strm_id);
			}
			free(pBMI);
		}
		pCodec->bufferingCount--;
	}
	uio_wakeup();
	tsem_down(&shvpu_avcdec_Private->return_sem);
#else
	/* Simply transfer input to output */
	for (i = 0; i < pPic->n_nals; i++) {
		nal = pPic->pNal[i];
		size = nal->size;
		off = nal->offset;
		for (j = 0; j < 2; j++) {
			pbuf = nal->pBuffer[j]->pBuffer + off;
			len = nal->pBuffer[j]->nFilledLen +
				nal->pBuffer[j]->nOffset - off;
			if (size < len)
				len = size;
			memcpy(pOutBuffer->pBuffer + pOutBuffer->nFilledLen,
			       pbuf, len);
			size -= len;
			nal->pBuffer[j]->nFilledLen -= len;
			nal->pBuffer[j]->nOffset += len;
			pOutBuffer->nFilledLen += len;
			if (size <= 0)
				break;
			off = 0;
		}
		free(nal);
	}
#endif
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
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
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
				shvpu_avcdec_Private->ports[portIndex];
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
				shvpu_avcdec_Private->ports[portIndex];
			memcpy(&port->sVideoParam, pVideoPortFormat,
			       sizeof
			       (OMX_VIDEO_PARAM_PORTFORMATTYPE));
			shvpu_avcdec_Private->
				ports[portIndex]->sPortParam.format.video.
				eColorFormat =
				port->sVideoParam.eColorFormat;

			if (portIndex == 1) {
				switch (port->sVideoParam.
					eColorFormat) {
				case OMX_COLOR_Format24bitRGB888:
					shvpu_avcdec_Private->eOutFramePixFmt = 0;
					break;
				case OMX_COLOR_Format24bitBGR888:
					shvpu_avcdec_Private->eOutFramePixFmt = 1;
					break;
				case OMX_COLOR_Format32bitBGRA8888:
					shvpu_avcdec_Private->eOutFramePixFmt = 2;
					break;
				case OMX_COLOR_Format32bitARGB8888:
					shvpu_avcdec_Private->eOutFramePixFmt = 3;
					break;
				case OMX_COLOR_Format16bitARGB1555:
					shvpu_avcdec_Private->eOutFramePixFmt = 4;
					break;
				case OMX_COLOR_Format16bitRGB565:
					shvpu_avcdec_Private->eOutFramePixFmt = 5;
					break;
				case OMX_COLOR_Format16bitBGR565:
					shvpu_avcdec_Private->eOutFramePixFmt = 6;
					break;
				default:
					shvpu_avcdec_Private->eOutFramePixFmt = 7;
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
		memcpy(&shvpu_avcdec_Private->pVideoAvc, pVideoAvc,
		       sizeof(OMX_VIDEO_PARAM_AVCTYPE));
		break;
	}
	case OMX_IndexParamStandardComponentRole:
	{
		OMX_PARAM_COMPONENTROLETYPE *pComponentRole;
		pComponentRole = ComponentParameterStructure;
		if (shvpu_avcdec_Private->state != OMX_StateLoaded
		    && shvpu_avcdec_Private->state !=
		    OMX_StateWaitForResources) {
			DEBUG(DEB_LEV_ERR,
			      "In %s Incorrect State=%x lineno=%d\n",
			      __func__, shvpu_avcdec_Private->state,
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
			shvpu_avcdec_Private->video_coding_type =
				OMX_VIDEO_CodingMPEG4;
		} else
			if (!strcmp
			    ((char *)pComponentRole->cRole,
			     VIDEO_DEC_H264_ROLE)) {
				shvpu_avcdec_Private->video_coding_type =
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
			memcpy(&shvpu_avcdec_Private->pVideoMpeg4,
			       pVideoMpeg4,
			       sizeof(OMX_VIDEO_PARAM_MPEG4TYPE));
		} else {
			return OMX_ErrorBadPortIndex;
		}
		break;
	}
	case OMX_IndexParamVPUMaxOutputSetting:
	{
		OMX_PARAM_REVPU5MAXPARAM *pMaxVals;
		if (shvpu_avcdec_Private->state != OMX_StateLoaded)
			return OMX_ErrorIncorrectStateOperation;
		pMaxVals = ComponentParameterStructure;
		if ((eError =
			checkHeader(pMaxVals,
			sizeof(OMX_PARAM_REVPU5MAXPARAM)) != OMX_ErrorNone)) {
			break;
		}
		memcpy (&shvpu_avcdec_Private->maxVideoParameters, pMaxVals,
			sizeof(OMX_PARAM_REVPU5MAXPARAM));
		break;
	}
	case OMX_IndexParamVPUMaxInstance:
	{
		OMX_PARAM_REVPU5MAXINSTANCE *pMaxInst;
		if (shvpu_avcdec_Private->state != OMX_StateLoaded)
			return OMX_ErrorIncorrectStateOperation;
		pMaxInst = ComponentParameterStructure;
		if ((eError =
			checkHeader(pMaxInst,
			sizeof(OMX_PARAM_REVPU5MAXINSTANCE)) != OMX_ErrorNone))
				break;
		if (pMaxInst->nInstances <= MAX_COMPONENT_VIDEODEC) {
			if (pMaxInst->nInstances > 1)
				shvpu_avcdec_Private->enable_sync = OMX_TRUE;
			memcpy (&maxVPUInstances,
				pMaxInst, sizeof(OMX_PARAM_REVPU5MAXINSTANCE));
			break;
		} else {
			return OMX_ErrorBadParameter;
		}
	}
	default:		/*Call the base component function */
		return omx_base_component_SetParameter(hComponent,
						       nParamIndex,
						       ComponentParameterStructure);
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
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
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
		       &shvpu_avcdec_Private->sPortTypesParam
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
				shvpu_avcdec_Private->ports
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
		memcpy(pVideoAvc, &shvpu_avcdec_Private->pVideoAvc,
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
				&shvpu_avcdec_Private->pVideoProfile[pAVCProfile->nProfileIndex],
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
			&shvpu_avcdec_Private->pVideoCurrentProfile,
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
		if (shvpu_avcdec_Private->video_coding_type ==
		    OMX_VIDEO_CodingMPEG4) {
			strcpy((char *)pComponentRole->cRole,
			       VIDEO_DEC_MPEG4_ROLE);
		} else if (shvpu_avcdec_Private->video_coding_type ==
			   OMX_VIDEO_CodingAVC) {
			strcpy((char *)pComponentRole->cRole,
			       VIDEO_DEC_H264_ROLE);
		} else {
			strcpy((char *)pComponentRole->cRole, "\0");
		}
		break;
	}
	case OMX_IndexParamVPUMaxOutputSetting:
	{
		OMX_PARAM_REVPU5MAXPARAM *pMaxVals;
		pMaxVals = ComponentParameterStructure;
		if ((eError =
			checkHeader(pMaxVals,
			sizeof(OMX_PARAM_REVPU5MAXPARAM)) != OMX_ErrorNone))
			break;

		memcpy (pMaxVals,&shvpu_avcdec_Private->maxVideoParameters,
			sizeof(OMX_PARAM_REVPU5MAXPARAM));
		break;
	}
	default:		/*Call the base component function */
		return omx_base_component_GetParameter(hComponent,
						       nParamIndex,
						       ComponentParameterStructure);
	}
	return OMX_ErrorNone;
}

OMX_ERRORTYPE
shvpu_avcdec_GetExtensionIndex(OMX_HANDLETYPE hComponent,
				OMX_STRING cParameterName,
				OMX_INDEXTYPE *pIndexType) {
	if (!cParameterName || !pIndexType)
		return OMX_ErrorBadParameter;
	if (!strcmp(cParameterName, OMX_VPU5_CommandMaxOut)) {
		*pIndexType = OMX_IndexParamVPUMaxOutputSetting;
		return OMX_ErrorNone;
	}
	if (!strcmp(cParameterName, OMX_VPU5_CommandMaxInst)) {
		*pIndexType = OMX_IndexParamVPUMaxInstance;
		return OMX_ErrorNone;
	}
	return OMX_ErrorUnsupportedIndex;
}

OMX_ERRORTYPE
shvpu_avcdec_MessageHandler(OMX_COMPONENTTYPE * pComponent,
			    internalRequestMessageType * message)
{
	shvpu_avcdec_PrivateType *shvpu_avcdec_Private =
		(shvpu_avcdec_PrivateType *) pComponent->pComponentPrivate;
	OMX_ERRORTYPE err;
	OMX_STATETYPE eCurrentState = shvpu_avcdec_Private->state;

	DEBUG(DEB_LEV_FUNCTION_NAME, "In %s\n", __func__);

	if (message->messageType == OMX_CommandStateSet) {
		switch(shvpu_avcdec_Private->state) {
		case OMX_StateIdle:
			if (message->messageParam == OMX_StateExecuting) {
				shvpu_avcdec_Private->isFirstBuffer = OMX_TRUE;
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
				err = shvpu_avcdec_vpuLibInit
					(shvpu_avcdec_Private);
				if (err != OMX_ErrorNone) {
					DEBUG(DEB_LEV_ERR,
					"In %s shvpu_avcdec_vpuLibInit Failed\n",
					__func__);
					return err;
				}
				err = shvpu_avcdec_Init(pComponent);
				if (err != OMX_ErrorNone) {
					DEBUG(DEB_LEV_ERR,
						"In %s Video Decoder Init"
						"Failed Error=%x\n",
						__func__, err);
					return err;
				}
				shvpu_avcdec_Private->avcodecReady = OMX_TRUE;
				return err;
			}
			break;
		}
	} else if (message->messageType == OMX_FreeHandle) {
		if (shvpu_avcdec_Private->state != OMX_StateLoaded) {
			err = shvpu_avcdec_Deinit(pComponent);
			if (err != OMX_ErrorNone) {
				DEBUG(DEB_LEV_ERR,
					"In %s Video Decoder Deinit"
					"Failed Error=%x\n",
					__func__, err);
				return err;
			}
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
