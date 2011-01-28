
/**
   src/vpu5/shvpu5_avcdec_ipmmu.h

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
#ifndef _SHVPU5_COMMON_IPMMU_H
#define _SHVPU5_COMMON_IPMMU_H

#ifdef IPMMU_ENABLE
#include <meram/ipmmui.h>
#endif

typedef struct {
#ifdef IPMMU_ENABLE
	IPMMUI *ipmmui;
	PMB *pmb;
	unsigned long ipmmui_vaddr;
	unsigned long ipmmui_mask;
#endif
} shvpu_ipmmui_t;

int
init_ipmmu(shvpu_ipmmui_t *ipmmui_data,
	   int pmb,
	   unsigned long phys_base,
	   int log2_stride,
	   int *align);

void
deinit_ipmmu(shvpu_ipmmui_t *ipmmui_data);

unsigned long
phys_to_ipmmui(shvpu_ipmmui_t *ipmmui_data, unsigned long address);

unsigned long
ipmmui_to_phys(shvpu_ipmmui_t *ipmmui_data, unsigned long ipmmu,
	unsigned long phys_base);


#endif /*  _SHVPU5_COMMON_IPMMU_H */
