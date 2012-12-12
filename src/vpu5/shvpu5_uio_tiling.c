/**
   src/vpu5/shvpu5_uio_tiling.c

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
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include "shvpu5_common_log.h"

#include <meram/ipmmui.h>
#include "shvpu5_common_ipmmu.h"

#define PMB_INDEX 0

#include "shvpu5_ipmmu_util.h"

struct uio_ipmmu_priv {
	IPMMUI *ipmmui;
	PMB    *pmb;
};

int
init_uio_ipmmu(shvpu_ipmmui_t *ipmmui_data,
	   unsigned long phys_base,
	   int stride,
	   int tile_logw,
	   int tile_logh)
{
	IPMMUI_REG *reg;
	unsigned int log2_stride;
	int ipmmui_size;
	int ipmmui_size_code;
	IPMMUI *ipmmui;
	unsigned long tmp;
	struct uio_ipmmu_priv *priv;
	PMB *pmb0;
	int i;
	unsigned long mask;

	memset(ipmmui_data, 0, sizeof(*ipmmui_data));
	ipmmui_data->private_data = priv = malloc(sizeof *priv);

	if (stride > 8196 || stride == 0)
		return -1;

	mask = 0xffffe000;
	for (i = 13; i > 0; i--, mask >>= 1) {
		if (stride & mask)
			break;
	}
	log2_stride = i;

	if (log2_stride < tile_logw)
		return -1;

	ipmmui = priv->ipmmui = ipmmui_open();
	if (!ipmmui)
		return -1;


	if (ipmmui_get_vaddr(ipmmui, "vpu", &ipmmui_data->ipmmui_vaddr,
		&ipmmui_size) < 0)
		return -1;

	switch (ipmmui_size) {
		case 16:
			ipmmui_size_code = 0x0;
			break;
		case 64:
			ipmmui_size_code = 0x10;
			break;
		case 128:
			ipmmui_size_code = 0x80;
			break;
		case 256:
			ipmmui_size_code = 0x90;
			break;
		default:
			return -1;
	}
	ipmmui_data->ipmmui_mask = ~((ipmmui_size << 20) - 1);

	pmb0 = priv->pmb = ipmmui_lock_pmb(ipmmui, PMB_INDEX);
	ipmmui_write_pmb(ipmmui, pmb0, IMPMBA, ipmmui_data->ipmmui_vaddr
		| (1 << 8));
	ipmmui_write_pmb(ipmmui, pmb0, IMPMBD,
                (phys_base & ipmmui_data->ipmmui_mask) |
                (1 << 8) | ipmmui_size_code | ((tile_logh - 1) << 20) |
                ((log2_stride - tile_logw - 1) << 16) | ((tile_logw - 4) << 12)
                | (1 << 9));

	reg = ipmmui_lock_reg(ipmmui);
	ipmmui_read_reg(ipmmui, reg, IMCTR1, &tmp);
	ipmmui_write_reg(ipmmui, reg, IMCTR1, tmp | 2);
	ipmmui_write_reg(ipmmui, reg, IMCTR2, 1);
	ipmmui_unlock_reg(ipmmui, reg);
	return 0;
	
}

void
deinit_uio_ipmmu(shvpu_ipmmui_t *ipmmui_data) {
	IPMMUI_REG *reg;
	PMB *pmb0;
	struct uio_ipmmu_priv *priv = ipmmui_data->private_data;
	if (!ipmmui_data->private_data)
		return;

	if (priv->pmb) {
		pmb0 = priv->pmb;
		ipmmui_write_pmb(priv->ipmmui, pmb0, IMPMBA, 0);
		ipmmui_write_pmb(priv->ipmmui, pmb0, IMPMBD, 0);
		ipmmui_unlock_pmb(priv->ipmmui, pmb0);
	}

	reg = ipmmui_lock_reg(priv->ipmmui);
	ipmmui_write_reg(priv->ipmmui, reg, IMCTR2, 0);
	ipmmui_write_reg(priv->ipmmui, reg, IMCTR1, 2);
	ipmmui_unlock_reg(priv->ipmmui, reg);

	ipmmui_close(priv->ipmmui);

	free(priv);
}

struct ipmmu_pmb_ops uio_ops = {
	.init = init_uio_ipmmu,
	.deinit = deinit_uio_ipmmu,
};

struct ipmmu_pmb_ops *pmb_ops = &uio_ops;
