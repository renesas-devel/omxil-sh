/**
   src/vpu5/shvpu5_common_uio.h

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
#ifndef __UIO_H_
#define __UIO_H_
#include <pthread.h>
#include "tsemaphore.h"
#define MAXNAMELEN	256
#define MAXUIOIDS	32

#define PHYS_INVALID	(~0UL)

struct uio_device {
	char *name;
	char *path;
	int fd;
};

struct uio_map {
	unsigned long address;
	unsigned long size;
	void *iomem;
};

/** allocate UIO memory
 *
 * @param size amount of memory to allocate
 * @param align alignemnt of memory
 * @param paddr (returns) physical address of allocated memory
 * @return NULL on failure, virtual address of allocated memroy otherwise
 */
void *
pmem_alloc(size_t size, int align, unsigned long *paddr);

/** Free UIO memory via virtual address
 *
 * @param vaddr virtual address of memory allocated with pmem_alloc
 * @param size amount of memroy to free
 */
void
pmem_free(void *vaddr, size_t size);

/** Free UIO memory via physical address
 *
 * @param vaddr physical address of memory allocated with pmem_alloc
 * @param size amount of memroy to free
 */
void
phys_pmem_free(unsigned long paddr, size_t size);

/** Wake up int handler that is waiting on an interrupt from a UIO device
  */
void
uio_wakeup();

/** Exit the interrupt handler created with uio_create_int_handle
  *
  * @param uio_sem semaphore used for synching the interrupt handler (not used)
  * @param exit_flag address of exit_flag specified to uio_create_int_handle
  */
void
uio_exit_handler(tsem_t *uio_sem, int *exit_flag);

/** Create interrupt handler for VPU
  *
  * @param thid address to store the thread id on creation
  * @param routine function to execute in new thread
  * @param arg argument to pass to routine
  * @param uio_sem pointer to semaphore for syncing interrupt handler (not used)
  * @param exit_flag pointer to flag used for terminating interrupt handler
  * @return 0 on success, otherwise error
  */
int
uio_create_int_handle(pthread_t *thid,
		      void *(*routine)(void *), void *arg,
		      tsem_t *uio_sem, int *exit_flag);

/** Create UIO intstance
  *
  * @param name unused
  * @param paddr_reg base address of VPU registers
  * @param paddr_pmem physical base address of UIO memory block
  * @param size_pmem size of UIO memory block
  * @return opaque pointer to UIOMUX instance
  */
void *
uio_init(char *name, unsigned long *paddr_reg,
	 unsigned long *paddr_pmem, size_t *size_pmem);

/** Close UIO instance
  *
  */
void
uio_deinit();

/** Get virtual base address of UIO memory block
  *
  * @param address pointer to store virtual address of UIO memory block
  * @param size pointer to store size of UIO memory block
  */
int
uio_get_virt_memory(void **address, unsigned long *size);


/** Read UIO memory (copy from UIO)
  *
  * @param src_addr physical address to copy from
  * @param dst_addr physical address to copy to
  * @param count number of bytes to copy
  */
long
vpu5_mem_read(unsigned long src_addr,
		  unsigned long dst_addr, long count);

/** Write UIO memory (copy to UIO)
  *
  * @param src_addr physical address to copy from
  * @param dst_addr physical address to copy to
  * @param count number of bytes to copy
  */
long
vpu5_mem_write(unsigned long src_addr,
		   unsigned long dst_addr, long count);

/** Read UIO registers (copy from UIO)
  *
  * @param src_addr physical address to copy from
  * @param reg_table physical address to copy to
  * @param count number of bytes to copy
  */
long
vpu5_mmio_read(unsigned long src_addr,
			unsigned long reg_table, long size);

/** Write UIO registers (copy to UIO)
  *
  * @param dst_addr physical address to copy from
  * @param reg_table physical address to copy to
  * @param count number of bytes to copy
  */
long
vpu5_mmio_write(unsigned long dst_addr,
			 unsigned long reg_table, long size);

/** set interrupt imask
  *
  * @param mask_enable mask bits
  * @param now_interrupt
  */
void
vpu5_set_imask(long mask_enable, long now_interrupt);

/** convert UIO virtual address to physical address)
  *
  * @param context unused
  * @param mode
  * @param addr virutal address to convert
  * @return UIO physical address
  */
unsigned long
uio_virt_to_phys(void *context, long mode, unsigned long addr);

/** convert UIO physical address to virtual address)
  *
  * @param addr physical address to convert
  * @return UIO virtual address
  */
void *
uio_phys_to_virt(unsigned long paddr);

/** get the base address of the VPU registers
  *
  * @return physical base address of VPU registers
  */
unsigned long
uio_register_base(void);

/** Lock access to VPU
  *
  */
void
uiomux_lock_vpu();

/** Unlock access to VPU
  *
  */
void
uiomux_unlock_vpu();

/** Register a buffer with UIOMux
  *
  */
void
uiomux_register_memory(void *vaddr, unsigned long paddr, int size);

#ifdef ICBCACHE_FLUSH
void icbcache_flush(void);
#endif

#endif /* __UIO_H_ */
