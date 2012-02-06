/**
   src/vpu5/shvpu5_common_udfio.c

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
#include "shvpu5_common_uio.h"
long
mciph_uf_mem_read(unsigned long src_addr,
		  unsigned long dst_addr, long count)
{
	return vpu5_mem_read(src_addr, dst_addr, count);
}

long
mciph_uf_mem_write(unsigned long src_addr,
		   unsigned long dst_addr, long count)
{
	return vpu5_mem_write(src_addr, dst_addr, count);
}
long
mciph_uf_reg_table_read(unsigned long src_addr,
			unsigned long reg_table, long size)
{
	return vpu5_mmio_read(src_addr, reg_table, size);
}
long
mciph_uf_reg_table_write(unsigned long dst_addr,
			 unsigned long reg_table, long size)
{
	return vpu5_mmio_write(dst_addr, reg_table, size);
}
void
mciph_uf_set_imask(long mask_enable, long now_interrupt)
{
	vpu5_set_imask(mask_enable, now_interrupt);
}

int vpc_start_frame() {
	return vpc_clear();
}
