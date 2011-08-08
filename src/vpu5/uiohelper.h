/**
   uiohelper.h

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

struct uio_data;
typedef struct uio_data UIO;

void *UIO_pmem_alloc (UIO *up, size_t size, int align, unsigned long *paddr);
void UIO_pmem_free (UIO *up, void *vaddr, size_t size);
void UIO_phys_pmem_free (UIO *up, unsigned long paddr, size_t size);
void UIO_interrupt_disable (UIO *up);
int UIO_interrupt_enable (UIO *up);
UIO *UIO_open (const char *name, unsigned long *paddr_reg,
	       unsigned long *paddr_pmem, void **vaddr_reg, void **vaddr_pmem,
	       size_t *size_reg, size_t *size_pmem,
	       void (*interrupt_callback) (void *arg), void *arg);
void UIO_close (UIO *up);
unsigned long UIO_virt_to_phys (UIO *up, void *vaddr);
void *UIO_phys_to_virt (UIO *up, unsigned long paddr);
void UIO_lock (UIO *up);
void UIO_unlock (UIO *up);
