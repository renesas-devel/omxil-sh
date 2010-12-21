/**
  src/spu2/omx_audioenc_component.c

  This component implements an audio (MP3/AAC/G726) encoder. The encoder is based on ffmpeg
  software library.

  Copyright (C) 2007-2009  STMicroelectronics
  Copyright (C) 2007-2009 Nokia Corporation and/or its subsidiary(-ies).

  This library is free software; you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation; either version 2.1 of the License, or (at your option)
  any later version.

  This library is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
  details.

  You should have received a copy of the GNU Lesser General Public License
  along with this library; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St, Fifth Floor, Boston, MA
  02110-1301  USA

*/

#include <bellagio/omxcore.h>
#include <bellagio/omx_base_audio_port.h>
#include <omx_audioenc_component.h>
/** modification to include audio formats */
#include<OMX_Audio.h>
#include "spu2helper/spuaacenc.h"

#define MAX_COMPONENT_AUDIOENC 4

/** Number of Audio Component Instance*/
static OMX_U32 noaudioencInstance=0;

/** This is the central function for component processing. It
  * is executed in a separate thread, is synchronized with
  * semaphores at each port, those are released each time a new buffer
  * is available on the given port.
  */
static void* _omx_base_filter_BufferMgmtFunction (void* param) {
  OMX_COMPONENTTYPE* openmaxStandComp = (OMX_COMPONENTTYPE*)param;
  omx_base_filter_PrivateType* omx_base_filter_Private = (omx_base_filter_PrivateType*)openmaxStandComp->pComponentPrivate;
  omx_base_PortType *pInPort=(omx_base_PortType *)omx_base_filter_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX];
  omx_base_PortType *pOutPort=(omx_base_PortType *)omx_base_filter_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];
  tsem_t* pInputSem = pInPort->pBufferSem;
  tsem_t* pOutputSem = pOutPort->pBufferSem;
  queue_t* pInputQueue = pInPort->pBufferQueue;
  queue_t* pOutputQueue = pOutPort->pBufferQueue;
  OMX_BUFFERHEADERTYPE* pOutputBuffer=NULL;
  OMX_BUFFERHEADERTYPE* pInputBuffer=NULL;
  OMX_BOOL isInputBufferNeeded=OMX_TRUE,isOutputBufferNeeded=OMX_TRUE;
  int inBufExchanged=0,outBufExchanged=0;
  int eosspecial;

  omx_base_filter_Private->bellagioThreads->nThreadBufferMngtID = (long int)syscall(__NR_gettid);
  DEBUG(DEB_LEV_FUNCTION_NAME, "In %s of component %x\n", __func__, (int)openmaxStandComp);
  DEBUG(DEB_LEV_SIMPLE_SEQ, "In %s the thread ID is %i\n", __func__, (int)omx_base_filter_Private->bellagioThreads->nThreadBufferMngtID);

  DEBUG(DEB_LEV_FUNCTION_NAME, "In %s\n", __func__);
  while(omx_base_filter_Private->state == OMX_StateIdle || omx_base_filter_Private->state == OMX_StateExecuting ||  omx_base_filter_Private->state == OMX_StatePause ||
    omx_base_filter_Private->transientState == OMX_TransStateLoadedToIdle){

    /*Wait till the ports are being flushed*/
    pthread_mutex_lock(&omx_base_filter_Private->flush_mutex);
    while( PORT_IS_BEING_FLUSHED(pInPort) ||
           PORT_IS_BEING_FLUSHED(pOutPort)) {
      pthread_mutex_unlock(&omx_base_filter_Private->flush_mutex);

      DEBUG(DEB_LEV_FULL_SEQ, "In %s 1 signalling flush all cond iE=%d,iF=%d,oE=%d,oF=%d iSemVal=%d,oSemval=%d\n",
        __func__,inBufExchanged,isInputBufferNeeded,outBufExchanged,isOutputBufferNeeded,pInputSem->semval,pOutputSem->semval);

      if(isOutputBufferNeeded==OMX_FALSE && PORT_IS_BEING_FLUSHED(pOutPort)) {
        pOutPort->ReturnBufferFunction(pOutPort,pOutputBuffer);
        outBufExchanged--;
        pOutputBuffer=NULL;
        isOutputBufferNeeded=OMX_TRUE;
        DEBUG(DEB_LEV_FULL_SEQ, "Ports are flushing,so returning output buffer\n");
      }

      if(isInputBufferNeeded==OMX_FALSE && PORT_IS_BEING_FLUSHED(pInPort)) {
        pInPort->ReturnBufferFunction(pInPort,pInputBuffer);
        inBufExchanged--;
        pInputBuffer=NULL;
        isInputBufferNeeded=OMX_TRUE;
        DEBUG(DEB_LEV_FULL_SEQ, "Ports are flushing,so returning input buffer\n");
      }

      DEBUG(DEB_LEV_FULL_SEQ, "In %s 2 signalling flush all cond iE=%d,iF=%d,oE=%d,oF=%d iSemVal=%d,oSemval=%d\n",
        __func__,inBufExchanged,isInputBufferNeeded,outBufExchanged,isOutputBufferNeeded,pInputSem->semval,pOutputSem->semval);

      tsem_up(omx_base_filter_Private->flush_all_condition);
      tsem_down(omx_base_filter_Private->flush_condition);
      pthread_mutex_lock(&omx_base_filter_Private->flush_mutex);
    }
    pthread_mutex_unlock(&omx_base_filter_Private->flush_mutex);

    /*No buffer to process. So wait here*/
    if((isInputBufferNeeded==OMX_TRUE && pInputSem->semval==0) &&
      (omx_base_filter_Private->state != OMX_StateLoaded && omx_base_filter_Private->state != OMX_StateInvalid)) {
      //Signalled from EmptyThisBuffer or FillThisBuffer or some thing else
      DEBUG(DEB_LEV_FULL_SEQ, "Waiting for next input/output buffer\n");
      tsem_down(omx_base_filter_Private->bMgmtSem);

    }
    if(omx_base_filter_Private->state == OMX_StateLoaded || omx_base_filter_Private->state == OMX_StateInvalid) {
      DEBUG(DEB_LEV_SIMPLE_SEQ, "In %s Buffer Management Thread is exiting\n",__func__);
      break;
    }
    if((isOutputBufferNeeded==OMX_TRUE && pOutputSem->semval==0) &&
      (omx_base_filter_Private->state != OMX_StateLoaded && omx_base_filter_Private->state != OMX_StateInvalid) &&
       !(PORT_IS_BEING_FLUSHED(pInPort) || PORT_IS_BEING_FLUSHED(pOutPort))) {
      //Signalled from EmptyThisBuffer or FillThisBuffer or some thing else
      DEBUG(DEB_LEV_FULL_SEQ, "Waiting for next input/output buffer\n");
      tsem_down(omx_base_filter_Private->bMgmtSem);

    }
    if(omx_base_filter_Private->state == OMX_StateLoaded || omx_base_filter_Private->state == OMX_StateInvalid) {
      DEBUG(DEB_LEV_SIMPLE_SEQ, "In %s Buffer Management Thread is exiting\n",__func__);
      break;
    }

    DEBUG(DEB_LEV_FULL_SEQ, "Waiting for input buffer semval=%d \n",pInputSem->semval);
    if(pInputSem->semval>0 && isInputBufferNeeded==OMX_TRUE ) {
      tsem_down(pInputSem);
      if(pInputQueue->nelem>0){
        inBufExchanged++;
        isInputBufferNeeded=OMX_FALSE;
        pInputBuffer = dequeue(pInputQueue);
        if(pInputBuffer == NULL){
          DEBUG(DEB_LEV_ERR, "Had NULL input buffer!!\n");
          break;
        }
      }
    }
    /*When we have input buffer to process then get one output buffer*/
    if(pOutputSem->semval>0 && isOutputBufferNeeded==OMX_TRUE) {
      tsem_down(pOutputSem);
      if(pOutputQueue->nelem>0){
        outBufExchanged++;
        isOutputBufferNeeded=OMX_FALSE;
        pOutputBuffer = dequeue(pOutputQueue);
        if(pOutputBuffer == NULL){
          DEBUG(DEB_LEV_ERR, "Had NULL output buffer!! op is=%d,iq=%d\n",pOutputSem->semval,pOutputQueue->nelem);
          break;
        }
      }
    }

    if(isInputBufferNeeded==OMX_FALSE) {
      if(pInputBuffer->hMarkTargetComponent != NULL){
        if((OMX_COMPONENTTYPE*)pInputBuffer->hMarkTargetComponent ==(OMX_COMPONENTTYPE *)openmaxStandComp) {
          /*Clear the mark and generate an event*/
          (*(omx_base_filter_Private->callbacks->EventHandler))
            (openmaxStandComp,
            omx_base_filter_Private->callbackData,
            OMX_EventMark, /* The command was completed */
            1, /* The commands was a OMX_CommandStateSet */
            0, /* The state has been changed in message->messageParam2 */
            pInputBuffer->pMarkData);
        } else {
          /*If this is not the target component then pass the mark*/
          omx_base_filter_Private->pMark.hMarkTargetComponent = pInputBuffer->hMarkTargetComponent;
          omx_base_filter_Private->pMark.pMarkData            = pInputBuffer->pMarkData;
        }
        pInputBuffer->hMarkTargetComponent = NULL;
      }
    }

    eosspecial = 0;
    if(isInputBufferNeeded==OMX_FALSE && isOutputBufferNeeded==OMX_FALSE) {

      if(omx_base_filter_Private->pMark.hMarkTargetComponent != NULL){
        pOutputBuffer->hMarkTargetComponent = omx_base_filter_Private->pMark.hMarkTargetComponent;
        pOutputBuffer->pMarkData            = omx_base_filter_Private->pMark.pMarkData;
        omx_base_filter_Private->pMark.hMarkTargetComponent = NULL;
        omx_base_filter_Private->pMark.pMarkData            = NULL;
      }

      pOutputBuffer->nTimeStamp = pInputBuffer->nTimeStamp;
      if((pInputBuffer->nFlags & OMX_BUFFERFLAG_STARTTIME) == OMX_BUFFERFLAG_STARTTIME) {
         DEBUG(DEB_LEV_FULL_SEQ, "Detected  START TIME flag in the input buffer filled len=%d\n", (int)pInputBuffer->nFilledLen);
         pOutputBuffer->nFlags = pInputBuffer->nFlags;
         pInputBuffer->nFlags = 0;
      }

      if(omx_base_filter_Private->state == OMX_StateExecuting)  {
        if (omx_base_filter_Private->BufferMgmtCallback && pInputBuffer->nFilledLen > 0) {
          (*(omx_base_filter_Private->BufferMgmtCallback))(openmaxStandComp, pInputBuffer, pOutputBuffer);
        } else {
          /*It no buffer management call back the explicitly consume input buffer*/
          pInputBuffer->nFilledLen = 0;
        }
      } else if(!(PORT_IS_BEING_FLUSHED(pInPort) || PORT_IS_BEING_FLUSHED(pOutPort))) {
        DEBUG(DEB_LEV_ERR, "In %s Received Buffer in non-Executing State(%x)\n", __func__, (int)omx_base_filter_Private->state);
      } else {
          pInputBuffer->nFilledLen = 0;
      }

      if((pInputBuffer->nFlags & OMX_BUFFERFLAG_EOS) == OMX_BUFFERFLAG_EOS && pInputBuffer->nFilledLen==0) {
        (*(omx_base_filter_Private->BufferMgmtCallback))(openmaxStandComp, pInputBuffer, pOutputBuffer);
	if (pOutputBuffer->nFilledLen != 0) {
		eosspecial = 1;
		goto skip_eosspecial;
	}
        DEBUG(DEB_LEV_FULL_SEQ, "Detected EOS flags in input buffer filled len=%d\n", (int)pInputBuffer->nFilledLen);
        pOutputBuffer->nFlags=pInputBuffer->nFlags;
        pInputBuffer->nFlags=0;
        (*(omx_base_filter_Private->callbacks->EventHandler))
          (openmaxStandComp,
          omx_base_filter_Private->callbackData,
          OMX_EventBufferFlag, /* The command was completed */
          1, /* The commands was a OMX_CommandStateSet */
          pOutputBuffer->nFlags, /* The state has been changed in message->messageParam2 */
          NULL);
        omx_base_filter_Private->bIsEOSReached = OMX_TRUE;
      }
    skip_eosspecial:
      if(omx_base_filter_Private->state==OMX_StatePause && !(PORT_IS_BEING_FLUSHED(pInPort) || PORT_IS_BEING_FLUSHED(pOutPort))) {
        /*Waiting at paused state*/
        tsem_wait(omx_base_filter_Private->bStateSem);
      }

      /*If EOS and Input buffer Filled Len Zero then Return output buffer immediately*/
      if((pOutputBuffer->nFilledLen != 0) || ((pOutputBuffer->nFlags & OMX_BUFFERFLAG_EOS) == OMX_BUFFERFLAG_EOS) || (omx_base_filter_Private->bIsEOSReached == OMX_TRUE)) {
        pOutPort->ReturnBufferFunction(pOutPort,pOutputBuffer);
        outBufExchanged--;
        pOutputBuffer=NULL;
        isOutputBufferNeeded=OMX_TRUE;
      }
    }

    if(omx_base_filter_Private->state==OMX_StatePause && !(PORT_IS_BEING_FLUSHED(pInPort) || PORT_IS_BEING_FLUSHED(pOutPort))) {
      /*Waiting at paused state*/
      tsem_wait(omx_base_filter_Private->bStateSem);
    }

    if (eosspecial)
	    continue;
    /*Input Buffer has been completely consumed. So, return input buffer*/
    if((isInputBufferNeeded == OMX_FALSE) && (pInputBuffer->nFilledLen==0)) {
      if(omx_base_filter_Private->state != OMX_StateInvalid)
        pInPort->ReturnBufferFunction(pInPort,pInputBuffer);
      inBufExchanged--;
      pInputBuffer=NULL;
      isInputBufferNeeded=OMX_TRUE;
    }
  }
  DEBUG(DEB_LEV_FUNCTION_NAME, "Out of %s of component %x\n", __func__, (int)openmaxStandComp);
  return NULL;
}

/** The Constructor
 */
OMX_ERRORTYPE omx_audioenc_component_Constructor(OMX_COMPONENTTYPE *openmaxStandComp,OMX_STRING cComponentName) {

  OMX_ERRORTYPE err = OMX_ErrorNone;
  omx_audioenc_component_PrivateType* omx_audioenc_component_Private;
  OMX_U32 i;

  if (!openmaxStandComp->pComponentPrivate) {
    DEBUG(DEB_LEV_FUNCTION_NAME,"In %s, allocating component\n",__func__);
    openmaxStandComp->pComponentPrivate = calloc(1, sizeof(omx_audioenc_component_PrivateType));
    if(openmaxStandComp->pComponentPrivate==NULL)
      return OMX_ErrorInsufficientResources;
  }
  else
    DEBUG(DEB_LEV_FUNCTION_NAME,"In %s, Error Component %x Already Allocated\n",__func__, (int)openmaxStandComp->pComponentPrivate);

  omx_audioenc_component_Private = openmaxStandComp->pComponentPrivate;
  omx_audioenc_component_Private->ports = NULL;

  /** Calling base filter constructor */
  err = omx_base_filter_Constructor(openmaxStandComp,cComponentName);
  omx_audioenc_component_Private->BufferMgmtFunction =
	  _omx_base_filter_BufferMgmtFunction;

  omx_audioenc_component_Private->sPortTypesParam[OMX_PortDomainAudio].nStartPortNumber = 0;
  omx_audioenc_component_Private->sPortTypesParam[OMX_PortDomainAudio].nPorts = 2;

  /** Allocate Ports and call port constructor. */
  if (omx_audioenc_component_Private->sPortTypesParam[OMX_PortDomainAudio].nPorts && !omx_audioenc_component_Private->ports) {
    omx_audioenc_component_Private->ports = calloc(omx_audioenc_component_Private->sPortTypesParam[OMX_PortDomainAudio].nPorts, sizeof(omx_base_PortType *));
    if (!omx_audioenc_component_Private->ports) {
      return OMX_ErrorInsufficientResources;
    }
    for (i=0; i < omx_audioenc_component_Private->sPortTypesParam[OMX_PortDomainAudio].nPorts; i++) {
      omx_audioenc_component_Private->ports[i] = calloc(1, sizeof(omx_base_audio_PortType));
      if (!omx_audioenc_component_Private->ports[i]) {
        return OMX_ErrorInsufficientResources;
      }
    }
  }

  base_audio_port_Constructor(openmaxStandComp, &omx_audioenc_component_Private->ports[0], 0, OMX_TRUE);
  base_audio_port_Constructor(openmaxStandComp, &omx_audioenc_component_Private->ports[1], 1, OMX_FALSE);

  /** Domain specific section for the ports. */
  // first we set the parameter common to both formats
  //common parameters related to input port
  omx_audioenc_component_Private->ports[OMX_BASE_FILTER_INPUTPORT_INDEX]->sPortParam.nBufferSize =  DEFAULT_OUT_BUFFER_SIZE;
  //common parameters related to output port
  omx_audioenc_component_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX]->sPortParam.nBufferSize = DEFAULT_IN_BUFFER_SIZE;

  // now it's time to know the audio coding type of the component
  if(!strcmp(cComponentName, AUDIO_ENC_AAC_NAME))   // AAC format encoder
    omx_audioenc_component_Private->audio_coding_type = OMX_AUDIO_CodingAAC;

  else  // IL client specified an invalid component name
    return OMX_ErrorInvalidComponentName;

  omx_audioenc_component_SetInternalParameters(openmaxStandComp);

  //settings of output port
  //output is pcm mode for all encoders - so generalise it
  setHeader(&omx_audioenc_component_Private->pAudioPcmMode,sizeof(OMX_AUDIO_PARAM_PCMMODETYPE));
  omx_audioenc_component_Private->pAudioPcmMode.nPortIndex = 0;
  omx_audioenc_component_Private->pAudioPcmMode.nChannels = 2;
  omx_audioenc_component_Private->pAudioPcmMode.eNumData = OMX_NumericalDataSigned;
  omx_audioenc_component_Private->pAudioPcmMode.eEndian = OMX_EndianLittle;
  omx_audioenc_component_Private->pAudioPcmMode.bInterleaved = OMX_TRUE;
  omx_audioenc_component_Private->pAudioPcmMode.nBitPerSample = 16;
  omx_audioenc_component_Private->pAudioPcmMode.nSamplingRate = 44100;
  omx_audioenc_component_Private->pAudioPcmMode.ePCMMode = OMX_AUDIO_PCMModeLinear;
  omx_audioenc_component_Private->pAudioPcmMode.eChannelMapping[0] = OMX_AUDIO_ChannelLF;
  omx_audioenc_component_Private->pAudioPcmMode.eChannelMapping[1] = OMX_AUDIO_ChannelRF;

  omx_audioenc_component_Private->BufferMgmtCallback = omx_audioenc_component_BufferMgmtCallback;

  omx_audioenc_component_Private->messageHandler = omx_audioenc_component_MessageHandler;
  omx_audioenc_component_Private->destructor = omx_audioenc_component_Destructor;
  openmaxStandComp->SetParameter = omx_audioenc_component_SetParameter;
  openmaxStandComp->GetParameter = omx_audioenc_component_GetParameter;
  openmaxStandComp->ComponentRoleEnum = omx_audioenc_component_ComponentRoleEnum;

  noaudioencInstance++;

  if(noaudioencInstance>MAX_COMPONENT_AUDIOENC)
    return OMX_ErrorInsufficientResources;

  return err;
}

/** The destructor
 */
OMX_ERRORTYPE omx_audioenc_component_Destructor(OMX_COMPONENTTYPE *openmaxStandComp)
{
  omx_audioenc_component_PrivateType* omx_audioenc_component_Private = openmaxStandComp->pComponentPrivate;
  OMX_U32 i;

  /* frees port/s */
  if (omx_audioenc_component_Private->ports) {
    for (i=0; i < omx_audioenc_component_Private->sPortTypesParam[OMX_PortDomainAudio].nPorts; i++) {
      if(omx_audioenc_component_Private->ports[i])
        omx_audioenc_component_Private->ports[i]->PortDestructor(omx_audioenc_component_Private->ports[i]);
    }
    free(omx_audioenc_component_Private->ports);
    omx_audioenc_component_Private->ports=NULL;
  }

  DEBUG(DEB_LEV_FUNCTION_NAME, "Destructor of audioencoder component is called\n");

  omx_base_filter_Destructor(openmaxStandComp);

  noaudioencInstance--;

  return OMX_ErrorNone;
}

void omx_audioenc_component_SetInternalParameters(OMX_COMPONENTTYPE *openmaxStandComp) {
  omx_audioenc_component_PrivateType* omx_audioenc_component_Private;
  omx_base_audio_PortType *pPort;

  omx_audioenc_component_Private = openmaxStandComp->pComponentPrivate;

  pPort = (omx_base_audio_PortType *) omx_audioenc_component_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX];

  if(omx_audioenc_component_Private->audio_coding_type == OMX_AUDIO_CodingAAC) {
    strcpy(omx_audioenc_component_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX]->sPortParam.format.audio.cMIMEType, "audio/aac");
    omx_audioenc_component_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX]->sPortParam.format.audio.eEncoding = OMX_AUDIO_CodingAAC;

    setHeader(&omx_audioenc_component_Private->pAudioAac,sizeof(OMX_AUDIO_PARAM_AACPROFILETYPE));
    omx_audioenc_component_Private->pAudioAac.nPortIndex = 1;
    omx_audioenc_component_Private->pAudioAac.nChannels = 2;
    omx_audioenc_component_Private->pAudioAac.nBitRate = 128000;
    omx_audioenc_component_Private->pAudioAac.nSampleRate = 44100;
    omx_audioenc_component_Private->pAudioAac.nAudioBandWidth = 0; //encoder decides the needed bandwidth
    omx_audioenc_component_Private->pAudioAac.eChannelMode = OMX_AUDIO_ChannelModeStereo;
    omx_audioenc_component_Private->pAudioAac.nFrameLength = 0; //encoder decides the framelength

    pPort->sAudioParam.nIndex = OMX_IndexParamAudioAac;
    pPort->sAudioParam.eEncoding = OMX_AUDIO_CodingAAC;

  } else
    return;

}

//int headersent=0;
//unsigned char *tmp;
/** The Initialization function
 */
OMX_ERRORTYPE omx_audioenc_component_Init(OMX_COMPONENTTYPE *openmaxStandComp)
{
  omx_audioenc_component_PrivateType* omx_audioenc_component_Private = openmaxStandComp->pComponentPrivate;
  OMX_ERRORTYPE err = OMX_ErrorNone;
  OMX_U32 nBufferSize;

  if (spu_aac_encode_init () < 0)
	  return OMX_ErrorInsufficientResources;

  /*Temporary First Output buffer size*/
  omx_audioenc_component_Private->inputCurrBuffer=NULL;
  omx_audioenc_component_Private->inputCurrLength=0;
  nBufferSize=omx_audioenc_component_Private->ports[OMX_BASE_FILTER_OUTPUTPORT_INDEX]->sPortParam.nBufferSize * 2;
  omx_audioenc_component_Private->internalOutputBuffer = malloc(nBufferSize);
  memset(omx_audioenc_component_Private->internalOutputBuffer, 0, nBufferSize);
  omx_audioenc_component_Private->positionInOutBuf = 0;
  omx_audioenc_component_Private->isNewBuffer=1;
  omx_audioenc_component_Private->isFirstBuffer = 1;

  return err;

};

/** The Deinitialization function
 */
OMX_ERRORTYPE omx_audioenc_component_Deinit(OMX_COMPONENTTYPE *openmaxStandComp) {
  omx_audioenc_component_PrivateType* omx_audioenc_component_Private = openmaxStandComp->pComponentPrivate;
  OMX_ERRORTYPE err = OMX_ErrorNone;
  long mid_ret;

  free(omx_audioenc_component_Private->internalOutputBuffer);
  omx_audioenc_component_Private->internalOutputBuffer = NULL;

  mid_ret = spu_aac_encode_deinit ();
  switch(mid_ret){
  case 0:
    return OMX_ErrorNone;
  default:
    return OMX_ErrorTimeout;
  }

//  free(tmp);

  return err;
}

/** buffer management callback function for encoding in new standard
 * of ffmpeg library
 */
void omx_audioenc_component_BufferMgmtCallback(OMX_COMPONENTTYPE *openmaxStandComp, OMX_BUFFERHEADERTYPE* pInputBuffer, OMX_BUFFERHEADERTYPE* pOutputBuffer)
{
  DEBUG(DEB_LEV_FUNCTION_NAME, "In %s\n",__func__);

  void *outbuf, *inbuf;
  void *outbufstart, *inbufstart;
  void *outbufend, *inbufend;
  long ret;
  internalRequestMessageType message;
  omx_audioenc_component_PrivateType* omx_audioenc_component_Private = (omx_audioenc_component_PrivateType*)openmaxStandComp->pComponentPrivate;

  outbuf = outbufstart = pOutputBuffer->pBuffer + pOutputBuffer->nOffset
	  + pOutputBuffer->nFilledLen;
  outbufend = pOutputBuffer->pBuffer + pOutputBuffer->nAllocLen;
  inbuf = inbufstart = pInputBuffer->pBuffer + pInputBuffer->nOffset;
  inbufend = pInputBuffer->pBuffer + pInputBuffer->nOffset
	  + pInputBuffer->nFilledLen;
  if ((pInputBuffer->nFlags & OMX_BUFFERFLAG_EOS) == OMX_BUFFERFLAG_EOS) {
    if ((ret = spu_aac_encode (&outbuf, outbufend, NULL, NULL)) < 0) {
      fprintf (stderr, "Encode error\n");
      message.messageType = OMX_CommandStateSet;
      message.messageParam = OMX_StateInvalid;
      omx_audioenc_component_Private->messageHandler(openmaxStandComp, &message);
    }
  } else {
    if ((ret = spu_aac_encode (&outbuf, outbufend, &inbuf, inbufend)) < 0) {
      fprintf (stderr, "Encode error\n");
      message.messageType = OMX_CommandStateSet;
      message.messageParam = OMX_StateInvalid;
      omx_audioenc_component_Private->messageHandler(openmaxStandComp, &message);
    }
  }
  pOutputBuffer->nFilledLen += (char *)outbuf - (char *)outbufstart;
  pInputBuffer->nOffset += (char *)inbuf - (char *)inbufstart;
  pInputBuffer->nFilledLen -= (char *)inbuf - (char *)inbufstart;

  /** return output buffer */
}

OMX_ERRORTYPE omx_audioenc_component_SetParameter(
  OMX_HANDLETYPE hComponent,
  OMX_INDEXTYPE nParamIndex,
  OMX_PTR ComponentParameterStructure)
{
  OMX_ERRORTYPE err = OMX_ErrorNone;
  OMX_AUDIO_PARAM_PORTFORMATTYPE *pAudioPortFormat;
  OMX_AUDIO_PARAM_PCMMODETYPE* pAudioPcmMode;
  OMX_AUDIO_PARAM_AACPROFILETYPE *pAudioAac; //support for AAC format
  OMX_PARAM_COMPONENTROLETYPE * pComponentRole;
  OMX_U32 portIndex;
  struct spu_aac_encode_setfmt_data SetfmtData;
  int err_mid = 0;

  /* Check which structure we are being fed and make control its header */
  OMX_COMPONENTTYPE *openmaxStandComp = (OMX_COMPONENTTYPE *)hComponent;
  omx_audioenc_component_PrivateType* omx_audioenc_component_Private = openmaxStandComp->pComponentPrivate;
  omx_base_audio_PortType *port;
  if (ComponentParameterStructure == NULL) {
    return OMX_ErrorBadParameter;
  }

  DEBUG(DEB_LEV_SIMPLE_SEQ, "   Setting parameter %i\n", nParamIndex);
  switch(nParamIndex) {
  case OMX_IndexParamAudioPortFormat:
    pAudioPortFormat = (OMX_AUDIO_PARAM_PORTFORMATTYPE*)ComponentParameterStructure;
    portIndex = pAudioPortFormat->nPortIndex;
    /*Check Structure Header and verify component state*/
    err = omx_base_component_ParameterSanityCheck(hComponent, portIndex, pAudioPortFormat, sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE));
    if(err!=OMX_ErrorNone) {
      DEBUG(DEB_LEV_ERR, "In %s Parameter Check Error=%x\n",__func__,err);
      break;
    }
    if (portIndex <= 1) {
      port = (omx_base_audio_PortType *) omx_audioenc_component_Private->ports[portIndex];
      memcpy(&port->sAudioParam,pAudioPortFormat,sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE));
    } else {
      return OMX_ErrorBadPortIndex;
    }
    break;

  case OMX_IndexParamAudioPcm:
    pAudioPcmMode = (OMX_AUDIO_PARAM_PCMMODETYPE*)ComponentParameterStructure;
    portIndex = pAudioPcmMode->nPortIndex;
    /*Check Structure Header and verify component state*/
    err = omx_base_component_ParameterSanityCheck(hComponent, portIndex, pAudioPcmMode, sizeof(OMX_AUDIO_PARAM_PCMMODETYPE));
    if(err!=OMX_ErrorNone) {
      DEBUG(DEB_LEV_ERR, "In %s Parameter Check Error=%x\n",__func__,err);
      break;
    }
    if(pAudioPcmMode->nPortIndex == 0)
      memcpy(&omx_audioenc_component_Private->pAudioPcmMode,pAudioPcmMode,sizeof(OMX_AUDIO_PARAM_PCMMODETYPE));
    else
      return OMX_ErrorBadPortIndex;
    break;

  case OMX_IndexParamAudioAac:
    pAudioAac = (OMX_AUDIO_PARAM_AACPROFILETYPE*) ComponentParameterStructure;
    portIndex = pAudioAac->nPortIndex;
    err = omx_base_component_ParameterSanityCheck(hComponent,portIndex,pAudioAac,sizeof(OMX_AUDIO_PARAM_AACPROFILETYPE));
    if(err!=OMX_ErrorNone) {
      DEBUG(DEB_LEV_ERR, "In %s Parameter Check Error=%x\n",__func__,err);
      break;
    }
    if (pAudioAac->nPortIndex == 1) {
      memcpy(&omx_audioenc_component_Private->pAudioAac,pAudioAac,sizeof(OMX_AUDIO_PARAM_AACPROFILETYPE));
    } else {
      return OMX_ErrorBadPortIndex;
    }
    /* call middleware function to set encode parameter */
    memset(&SetfmtData, 0, sizeof(SetfmtData));
    SetfmtData.cbrmode = 0; /* 0:VBR */
    SetfmtData.samplerate = pAudioAac->nSampleRate;
    SetfmtData.bitrate = pAudioAac->nBitRate;
    SetfmtData.channel = pAudioAac->nChannels;
    if (pAudioAac->eChannelMode == OMX_AUDIO_ChannelModeMono && pAudioAac->nChannels == 2) {
      SetfmtData.channel = SPU_AAC_ENCODE_SETFMT_CHANNEL_DUAL_MONO;
	}
    switch (pAudioAac->eAACStreamFormat) {
      case OMX_AUDIO_AACStreamFormatMP2ADTS:
      case OMX_AUDIO_AACStreamFormatMP4ADTS:
        SetfmtData.type = SPU_AAC_ENCODE_SETFMT_TYPE_ADTS;
        break;
      case OMX_AUDIO_AACStreamFormatADIF:
        SetfmtData.type = SPU_AAC_ENCODE_SETFMT_TYPE_ADIF;
        break;
      case OMX_AUDIO_AACStreamFormatRAW:
        switch (pAudioAac->eAACProfile) {
        case OMX_AUDIO_AACObjectLC:
        case OMX_AUDIO_AACObjectHE:
          SetfmtData.type = SPU_AAC_ENCODE_SETFMT_TYPE_RAW;
          break;
        case OMX_AUDIO_AACObjectERLC:
        case OMX_AUDIO_AACObjectLD:
        case OMX_AUDIO_AACObjectLTP:
        case OMX_AUDIO_AACObjectMain:
        case OMX_AUDIO_AACObjectSSR:
        case OMX_AUDIO_AACObjectScalable:
        case OMX_AUDIO_AACObjectHE_PS:
          return OMX_ErrorUnsupportedSetting;
        default: 
          return OMX_ErrorBadParameter;
        }
        break;
      case OMX_AUDIO_AACStreamFormatMP4LOAS:
      case OMX_AUDIO_AACStreamFormatMP4LATM:
      case OMX_AUDIO_AACStreamFormatMP4FF:
        return OMX_ErrorUnsupportedSetting;
      default:
        return OMX_ErrorBadParameter;
    }
    if ((err_mid = spu_aac_encode_setfmt(&SetfmtData)) < 0) {
        switch(err_mid){
        case -1:
          return OMX_ErrorBadParameter;
        case -2:
          return OMX_ErrorInvalidState;
        default:
          return OMX_ErrorUndefined;
        }
    }
    memcpy(&omx_audioenc_component_Private->pAudioAac,pAudioAac,sizeof(OMX_AUDIO_PARAM_AACPROFILETYPE));
    break;

  case OMX_IndexParamStandardComponentRole:
    pComponentRole = (OMX_PARAM_COMPONENTROLETYPE*)ComponentParameterStructure;
    if (!strcmp((char*)pComponentRole->cRole, AUDIO_ENC_AAC_ROLE)) {
      omx_audioenc_component_Private->audio_coding_type = OMX_AUDIO_CodingAAC;
    } else {
      return OMX_ErrorBadParameter;
    }
    omx_audioenc_component_SetInternalParameters(openmaxStandComp);
    break;

  default: /*Call the base component function*/
    return omx_base_component_SetParameter(hComponent, nParamIndex, ComponentParameterStructure);
  }
  return err;
}

OMX_ERRORTYPE omx_audioenc_component_GetParameter(
  OMX_HANDLETYPE hComponent,
  OMX_INDEXTYPE nParamIndex,
  OMX_PTR ComponentParameterStructure)
{
  OMX_AUDIO_PARAM_PORTFORMATTYPE *pAudioPortFormat;
  OMX_AUDIO_PARAM_PCMMODETYPE *pAudioPcmMode;
  OMX_AUDIO_PARAM_AACPROFILETYPE *pAudioAac; //support for AAC format
  OMX_PARAM_COMPONENTROLETYPE * pComponentRole;
  omx_base_audio_PortType *port;
  OMX_ERRORTYPE err = OMX_ErrorNone;

  OMX_COMPONENTTYPE *openmaxStandComp = (OMX_COMPONENTTYPE *)hComponent;
  omx_audioenc_component_PrivateType* omx_audioenc_component_Private = (omx_audioenc_component_PrivateType*)openmaxStandComp->pComponentPrivate;
  if (ComponentParameterStructure == NULL) {
    return OMX_ErrorBadParameter;
  }
  DEBUG(DEB_LEV_SIMPLE_SEQ, "   Getting parameter %i\n", nParamIndex);
  /* Check which structure we are being fed and fill its header */
  switch(nParamIndex) {
  case OMX_IndexParamAudioInit:
    if ((err = checkHeader(ComponentParameterStructure, sizeof(OMX_PORT_PARAM_TYPE))) != OMX_ErrorNone) {
      break;
    }
    memcpy(ComponentParameterStructure, &omx_audioenc_component_Private->sPortTypesParam[OMX_PortDomainAudio], sizeof(OMX_PORT_PARAM_TYPE));
    break;

  case OMX_IndexParamAudioPortFormat:
    pAudioPortFormat = (OMX_AUDIO_PARAM_PORTFORMATTYPE*)ComponentParameterStructure;
    if ((err = checkHeader(ComponentParameterStructure, sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE))) != OMX_ErrorNone) {
      break;
    }
    if (pAudioPortFormat->nPortIndex <= 1) {
      port = (omx_base_audio_PortType *)omx_audioenc_component_Private->ports[pAudioPortFormat->nPortIndex];
      memcpy(pAudioPortFormat, &port->sAudioParam, sizeof(OMX_AUDIO_PARAM_PORTFORMATTYPE));
    } else {
      return OMX_ErrorBadPortIndex;
    }
    break;

  case OMX_IndexParamAudioPcm:
    pAudioPcmMode = (OMX_AUDIO_PARAM_PCMMODETYPE*)ComponentParameterStructure;
    if (pAudioPcmMode->nPortIndex != 0) {
      return OMX_ErrorBadPortIndex;
    }
    if ((err = checkHeader(ComponentParameterStructure, sizeof(OMX_AUDIO_PARAM_PCMMODETYPE))) != OMX_ErrorNone) {
      break;
    }
    memcpy(pAudioPcmMode,&omx_audioenc_component_Private->pAudioPcmMode,sizeof(OMX_AUDIO_PARAM_PCMMODETYPE));
    break;

  case OMX_IndexParamAudioAac:
    pAudioAac = (OMX_AUDIO_PARAM_AACPROFILETYPE*)ComponentParameterStructure;
    if (pAudioAac->nPortIndex != 1) {
      return OMX_ErrorBadPortIndex;
    }
    if ((err = checkHeader(ComponentParameterStructure, sizeof(OMX_AUDIO_PARAM_AACPROFILETYPE))) != OMX_ErrorNone) {
      break;
    }
    memcpy(pAudioAac,&omx_audioenc_component_Private->pAudioAac,sizeof(OMX_AUDIO_PARAM_AACPROFILETYPE));
    break;

  case OMX_IndexParamStandardComponentRole:
    pComponentRole = (OMX_PARAM_COMPONENTROLETYPE*)ComponentParameterStructure;
    if ((err = checkHeader(ComponentParameterStructure, sizeof(OMX_PARAM_COMPONENTROLETYPE))) != OMX_ErrorNone) {
      break;
    }

    if (omx_audioenc_component_Private->audio_coding_type == OMX_AUDIO_CodingAAC) {
      strcpy((char*)pComponentRole->cRole, AUDIO_ENC_AAC_ROLE);
    } else {
      strcpy((char*)pComponentRole->cRole,"\0");;
    }
    break;

  default: /*Call the base component function*/
    return omx_base_component_GetParameter(hComponent, nParamIndex, ComponentParameterStructure);
  }
  return err;
}

OMX_ERRORTYPE omx_audioenc_component_MessageHandler(OMX_COMPONENTTYPE* openmaxStandComp,internalRequestMessageType *message)
{
  omx_audioenc_component_PrivateType* omx_audioenc_component_Private = (omx_audioenc_component_PrivateType*)openmaxStandComp->pComponentPrivate;
  OMX_ERRORTYPE err;

  DEBUG(DEB_LEV_FUNCTION_NAME, "In %s\n", __func__);

  if (message->messageType == OMX_CommandStateSet){
    if ((message->messageParam == OMX_StateExecuting ) && (omx_audioenc_component_Private->state == OMX_StateIdle)) {
    } else if ((message->messageParam == OMX_StateIdle ) && (omx_audioenc_component_Private->state == OMX_StateLoaded)) {
      err = omx_audioenc_component_Init(openmaxStandComp);
      if(err!=OMX_ErrorNone) {
        DEBUG(DEB_LEV_ERR, "In %s Audio Encoder Init Failed Error=%x\n",__func__,err);
        return err;
      }
    } else if ((message->messageParam == OMX_StateLoaded) && (omx_audioenc_component_Private->state == OMX_StateIdle)) {
      err = omx_audioenc_component_Deinit(openmaxStandComp);
      if(err!=OMX_ErrorNone) {
        DEBUG(DEB_LEV_ERR, "In %s Audio Encoder Deinit Failed Error=%x\n",__func__,err);
        return err;
      }
    } else if (message->messageParam == OMX_StateInvalid) {
      err = omx_audioenc_component_Deinit(openmaxStandComp);
      if(err!=OMX_ErrorNone) {
        DEBUG(DEB_LEV_ERR, "In %s Audio Encoder Deinit Failed Error=%x\n",__func__,err);
        return err;
      }
    }
  }
  // Execute the base message handling
  err =  omx_base_component_MessageHandler(openmaxStandComp,message);

  return err;
}

OMX_ERRORTYPE omx_audioenc_component_ComponentRoleEnum(
  OMX_HANDLETYPE hComponent,
  OMX_U8 *cRole,
  OMX_U32 nIndex)
{
  if (nIndex == 0) {
    strcpy((char*)cRole, AUDIO_ENC_AAC_ROLE);
  } else {
    return OMX_ErrorUnsupportedIndex;
  }
  return OMX_ErrorNone;
}
