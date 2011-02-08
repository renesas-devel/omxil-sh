/**
   src/vpu5/shvpu5_avcdec.h

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
#ifndef __SIMPLE_AVCDEC_H_
#define __SIMPLE_AVCDEC_H_
#include "mcvdec.h"

int logd(const char *format, ...);
int loge(const char *format, ...);

void *
pmem_alloc(size_t size, int align, unsigned long *paddr);
void
pmem_free(void *vaddr, size_t size);

unsigned long
uio_virt_to_phys(void *context, long mode, unsigned long addr);
void *
uio_phys_to_virt(unsigned long paddr);

#if 0
long
decode_init(MCVDEC_CONTEXT_T **context);
#endif
int
decode_finalize(void *context);

#endif /* __SIMPLE_AVCDEC_H_ */
