/**
   src/vpu5/ipmmuihelper.h

   This component implements H.264 / MPEG-4 AVC video codec.
   The H.264 / MPEG-4 AVC video encoder/decoder is implemented
   on the Renesas's VPU5HG middleware library.

   Copyright (C) 2011 Renesas Solutions Corp.

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
#include <errno.h>
#include <stdlib.h>


#ifdef DMAC_MODE
int ipmmui_buffer_init();
int ipmmui_buffer_map_vaddr(void *vaddr, unsigned int size,
		unsigned long *paddr);
int ipmmui_buffer_unmap_vaddr(void *vaddr);
void ipmmui_buffer_deinit();
#else
#define ipmmui_buffer_init() (0)
#define ipmmui_buffer_map_vaddr(x, y, z) (0)
#define ipmmui_buffer_unmap_vaddr(x) (0)
#define ipmmui_buffer_deinit()
#endif //DMAC_MODE
