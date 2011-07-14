/**
   src/vpu5/shvpu5_driver.h

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
#ifndef __SHVPU5_DRIVER_H_
#define __SHVPU5_DRIVER_H_
#include <pthread.h>
#include <bellagio/tsemaphore.h>
#include "mciph.h"
#include "uiomux/uiomux.h"
#if defined(VPU_VERSION_5HA)
#include "mciph_ip0_cmn.h"
#endif

typedef struct {
	MCIPH_DRV_INFO_T*	pDrvInfo;
	MCIPH_API_T		apiTbl;
	/** @param mode for VPU5HG video decoder */
	MCIPH_WORK_INFO_T	wbufVpu5;
	MCIPH_VPU5_INIT_T	vpu5Init;
#if defined(VPU_VERSION_5HA)
	MCIPH_IP0_INIT_T	ip0Init;
#endif
	UIOMux*			uiomux;
	pthread_t		intrHandler;
	int			frameId;
	int			lastOutput;
	unsigned char		isEndInput;
	tsem_t			uioSem;
	int			isExit;
} shvpu_driver_t;

int
shvpu_driver_deinit(shvpu_driver_t *pHandle);

long
shvpu_driver_init(shvpu_driver_t **ppDriver);

unsigned long
shvpu5_load_firmware(char *filename, size_t *size);
#endif /* __SHVPU5_DRIVER_H_ */
