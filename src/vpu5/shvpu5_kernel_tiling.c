/**
   src/vpu5/shvpu5_kernel_tiling.c

   This component implements H.264 / MPEG-4 AVC video codec.
   The H.264 / MPEG-4 AVC video encoder/decoder is implemented
   on the Renesas's VPU5HG middleware library.

   Copyright (C) 2012 IGEL Co., Ltd
   Copyright (C) 2012 Renesas Solutions Corp.

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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "shvpu5_decode.h"
#include "shvpu5_ipmmu_util.h"
#include "shvpu5_kernel_tiling.h"
#define PMB_FILE "/dev/pmb"

static int
init_kernel_tiling(struct shvpu_ipmmui_t *ipmmui_data,
	   unsigned long phys_base,
	   int stride,
	   int tile_logw,
	   int tile_logh)
{
	int fd, ret;	
	struct ipmmu_pmb_info pmb;
	struct pmb_tile_info tile;
	unsigned long vaddr;

	fd = open(PMB_FILE, O_RDWR);
	if (fd < 0)
		return -1;
	
	ROUND_NEXT_POW2(stride, stride);

	pmb.size_mb = PMB_SIZE;
	pmb.paddr = phys_base & ~((PMB_SIZE << 20)- 1);
	pmb.enabled = 1;

	tile.enabled = 1;
	tile.buffer_pitch = stride;
	tile.tile_width = (1 << tile_logw);
	tile.tile_height = (1 << tile_logh);

	ret = ioctl(fd, IPMMU_SET_PMB, &pmb);	
	if (ret < 0)
		return ret;
	ret = ioctl(fd, IPMMU_GET_PMB_HANDLE, &vaddr);	
	if (ret < 0)
		return ret;

	ret = ioctl(fd, IPMMU_SET_PMB_TL, &tile);	
	if (ret < 0)
		return ret;

	ipmmui_data->private_data = (void *)fd;
	ipmmui_data->ipmmui_mask = ~((PMB_SIZE << 20) - 1);
	ipmmui_data->ipmmui_vaddr = vaddr;
	return 0;
}

static void
deinit_kernel_tiling(struct shvpu_ipmmui_t *ipmmui_data)
{
	int fd = (int) ipmmui_data->private_data;
	close(fd);
}

static struct ipmmu_pmb_ops kernel_ops = {
	.init = init_kernel_tiling,
	.deinit = deinit_kernel_tiling,
};

struct ipmmu_pmb_ops *pmb_ops = &kernel_ops;
