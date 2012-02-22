/**
   src/vpu5/shvpu5_memory_util.h

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
/* Memory management operations
   All memory management schemes (eg. UIO, IPMMUI) must implemenet all
   operations in the memory_ops structure, and the get_memory_ops()
   function */
#ifndef __MEMORY_UTIL_H_
#define __MEMORY_UTIL_H_

#define PHYS_UNDEF 0xFFFFFFFF

struct memory_ops {
	/* initalize memory, return memory base address and size */
	int (*memory_init) (unsigned long *vaddr, size_t *size);
	/* deinitialize memory */
	void (*memory_deinit) (void);
	/*see shvpu5_common_uio.h*/
	void *(*pmem_alloc) (size_t size, int align, unsigned long *paddr);
	/*see shvpu5_common_uio.h*/
	void (*pmem_free) (void *vaddr, size_t size);
	/*see shvpu5_common_uio.h*/
	void (*phys_pmem_free) (unsigned long paddr, size_t size);
	/*see shvpu5_common_uio.h*/
        int (*get_virt_memory) (void **vaddr, unsigned long *size);
	/*see shvpu5_common_uio.h*/
	long (*mem_read) (unsigned long src, unsigned long dst, long count);
	/*see shvpu5_common_uio.h*/
	long (*mem_write) (unsigned long src, unsigned long dst, long count);
	/* virt to phys address conversion, return PHYS_UNDEF on failure */
	unsigned long (*virt_to_phys) (void *);
	/* phys to virt address conversion, return NULL on failure */
	void *(*phys_to_virt) (unsigned long);
};
/* return a pointer to the instance of the memory_ops structure */
struct memory_ops *get_memory_ops();
#endif /* __MEMORY_UTIL_H_ */
