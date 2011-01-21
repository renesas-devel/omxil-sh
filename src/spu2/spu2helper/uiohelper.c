/**
   uiohelper.c

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

#include <string.h>
#include <pthread.h>
#include "uiomux/uiomux.h"
#include "uiohelper.h"

struct uio_data {
	UIOMux *uiomux;
	uiomux_resource_t type;
	void (*interrupt_thread_ufunc) (void *arg);
	void *interrupt_thread_uarg;
	int interrupt_enabled;
	pthread_mutex_t interrupt_lock, interrupt_thread_end_lock;
	unsigned long paddr_reg, paddr_pmem;
	unsigned long size_reg, size_pmem;
	void *vaddr_reg, *vaddr_pmem;
};

void *
UIO_pmem_alloc (UIO *up, size_t size, int align, unsigned long *paddr)
{
	void *vaddr;

	vaddr = uiomux_malloc (up->uiomux, up->type, size, align);
	if (vaddr && paddr)
		*paddr = uiomux_virt_to_phys (up->uiomux, up->type, vaddr);

	return vaddr;
}

void
UIO_pmem_free (UIO *up, void *vaddr, size_t size)
{
	return uiomux_free (up->uiomux, up->type, vaddr, size);
}

void
UIO_phys_pmem_free (UIO *up, unsigned long paddr, size_t size)
{
	return uiomux_free (up->uiomux, up->type,
		uiomux_phys_to_virt (up->uiomux, up->type, paddr), size);
}

static void *
UIO_interrupt_thread (void *arg)
{
	int ret;
	UIO *up;

	if (!arg)
		return NULL;
	up = arg;

	for (;;) {
		ret = uiomux_sleep (up->uiomux, up->type);
		pthread_mutex_lock (&up->interrupt_lock);
		if (!up->interrupt_enabled) {
			pthread_mutex_unlock (&up->interrupt_lock);
			pthread_mutex_unlock (&up->interrupt_thread_end_lock);
			break;
		}
		if (ret < 0) {
			up->interrupt_enabled = 0;
			pthread_mutex_unlock (&up->interrupt_lock);
			break;
		}
		pthread_mutex_unlock (&up->interrupt_lock);
		up->interrupt_thread_ufunc (up->interrupt_thread_uarg);
	}

	return NULL;
}

void
UIO_interrupt_disable (UIO *up)
{
	int action = 0;

	pthread_mutex_lock (&up->interrupt_lock);
	if (up->interrupt_enabled) {
		up->interrupt_enabled = 0;
		action = 1;
	}
	pthread_mutex_unlock (&up->interrupt_lock);
	if (action) {
		uiomux_wakeup (up->uiomux, up->type);
		pthread_mutex_lock (&up->interrupt_thread_end_lock);
	}
}

int
UIO_interrupt_enable (UIO *up)
{
	int ret = 0;
	pthread_t thid;

	pthread_mutex_lock (&up->interrupt_lock);
	if (!up->interrupt_enabled) {
		up->interrupt_enabled = 1;
		ret = pthread_create (&thid, NULL, UIO_interrupt_thread,
				      (void *)up);
	}
	pthread_mutex_unlock (&up->interrupt_lock);
	return ret;
}

UIO *
UIO_open (const char *name, unsigned long *paddr_reg,
	  unsigned long *paddr_pmem, void **vaddr_reg, void **vaddr_pmem,
	  size_t *size_reg, size_t *size_pmem,
	  void (*interrupt_callback) (void *arg), void *arg)
{
	uiomux_resource_t uiores;
	UIO *up;
	UIOMux *uiomux;
	const char *names[2] = { name, NULL };

	uiomux = uiomux_open_named (names);
	if (!uiomux)
		return NULL;
	if (!uiomux_check_resource (uiomux, 1)) {
		uiomux_close (uiomux);
		return NULL;
	}
	up = malloc (sizeof *up);
	if (!up) {
		uiomux_close (uiomux);
		return NULL;
	}
	up->uiomux = uiomux;
	up->type = 1;
	up->interrupt_thread_ufunc = interrupt_callback;
	up->interrupt_thread_uarg = arg;
	up->interrupt_enabled = 0;
	pthread_mutex_init (&up->interrupt_lock, NULL);
	pthread_mutex_init (&up->interrupt_thread_end_lock, NULL);
	pthread_mutex_lock (&up->interrupt_thread_end_lock);
	uiomux_get_mmio (up->uiomux, up->type, &up->paddr_reg, &up->size_reg,
			 &up->vaddr_reg);
	uiomux_get_mem (up->uiomux, up->type, &up->paddr_pmem, &up->size_pmem,
			&up->vaddr_pmem);
	if (paddr_reg)
		*paddr_reg = up->paddr_reg;
	if (vaddr_reg)
		*vaddr_reg = up->vaddr_reg;
	if (size_reg)
		*size_reg = up->size_reg;
	if (paddr_pmem)
		*paddr_pmem = up->paddr_pmem;
	if (vaddr_pmem)
		*vaddr_pmem = up->vaddr_pmem;
	if (size_pmem)
		*size_pmem = up->size_pmem;

	return up;
}

void
UIO_close (UIO *up)
{
	UIO_interrupt_disable (up);
	uiomux_close (up->uiomux);
	free (up);
}

unsigned long
UIO_virt_to_phys (UIO *up, void *vaddr)
{
	unsigned long paddr;

	paddr = uiomux_virt_to_phys (up->uiomux, up->type, vaddr);
	return paddr;
}

void *
UIO_phys_to_virt (UIO *up, unsigned long paddr)
{
	void *vaddr;

	vaddr = uiomux_phys_to_virt (up->uiomux, up->type, paddr);
	return vaddr;
}

void
UIO_lock (UIO *up)
{
	uiomux_lock (up->uiomux, up->type);
}

void
UIO_unlock (UIO *up)
{
	uiomux_unlock (up->uiomux, up->type);
}
