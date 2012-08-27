/**
   src/vpu5/shvpu5_common_ipmmu.c

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
#include <stdlib.h>
#include <string.h>
#include "shvpu5_common_ipmmu.h"
#include "shvpu5_ipmmu_util.h"

shvpu_ipmmui_t *
init_ipmmu(unsigned long phys_base, int stride) {

	shvpu_ipmmui_t *ipmmui_data;
	ipmmui_data = malloc (sizeof *ipmmui_data);
	memset(ipmmui_data, 0, sizeof(*ipmmui_data));
	if (ipmmui_data) {
		pmb_ops->init(ipmmui_data, phys_base, stride);
		uiomux_register(ipmmui_data->ipmmui_vaddr,
		(unsigned long) ipmmui_data->ipmmui_vaddr, (64 << 20));
	}
	return ipmmui_data;
}

void
deinit_ipmmu(shvpu_ipmmui_t *ipmmui_data)
{
	pmb_ops->deinit(ipmmui_data);
	free(ipmmui_data);
}

unsigned long
phys_to_ipmmui(shvpu_ipmmui_t *ipmmui_data, unsigned long address) {
	if (ipmmui_data)
		return (address & ~ipmmui_data->ipmmui_mask) |
			ipmmui_data->ipmmui_vaddr;
	else
		return address;
}

unsigned long
ipmmui_to_phys(shvpu_ipmmui_t *ipmmui_data, unsigned long ipmmu,
	unsigned long phys_base) {
	if (ipmmui_data)
		return (ipmmu & ~ipmmui_data->ipmmui_mask) |
			(phys_base & ipmmui_data->ipmmui_mask);
	else
		return ipmmu;
}
