/**
   src/vpu/shvpu_avcdec_uio.c

   This component implements H.264 / MPEG-4 AVC video decoder.
   The H.264 / MPEG-4 AVC Video decoder is implemented on the
   Renesas's VPU5HG middleware library.

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
#include <string.h>
#include <pthread.h>
#include "uiomux/uiomux.h"
#include "shvpu_avcdec_uio.h"
#include "mciph.h"
#include "shvpu_avcdec.h"
#include "tsemaphore.h"
#include <sys/file.h>

static UIOMux *uiomux = NULL;

static pthread_mutex_t uiomux_mutex = PTHREAD_MUTEX_INITIALIZER;
static ref_cnt = 0;
int
uio_interrupt_clear()
{
	unsigned int *vp5_irq_sta;

	vp5_irq_sta = uiomux_phys_to_virt(uiomux,
					  UIOMUX_SH_VPU, 0xfe900014);
	if (vp5_irq_sta == NULL)
		return -1;

	*vp5_irq_sta = 0U;			// mask all
	return 0;
}

void *
pmem_alloc(size_t size, int align, unsigned long *paddr)
{
	void *vaddr;

	vaddr = uiomux_malloc(uiomux, UIOMUX_SH_VPU, size, align);
	if (vaddr && paddr)
		*paddr = uiomux_virt_to_phys(uiomux, UIOMUX_SH_VPU, vaddr);

	return vaddr;
}

void
pmem_free(void *vaddr, size_t size)
{
	return uiomux_free(uiomux, UIOMUX_SH_VPU, vaddr, size);
}

void
phys_pmem_free(unsigned long paddr, size_t size)
{
	return uiomux_free(uiomux, UIOMUX_SH_VPU,
		uiomux_phys_to_virt(uiomux, UIOMUX_SH_VPU, paddr), size);
}
static void *
uio_int_handler(void *arg)
{
	int ret;
	void *(*ufunc)(void *);
	void *uarg;
	tsem_t *uio_sem;
	int *exit_flag;
	if (arg) {
		void **args = arg;
		ufunc = args[0];
		uarg = args[1];
		uio_sem = args[2];
		exit_flag = args[3];
	} else {
		ufunc = uarg = NULL;
		return NULL;
	}

	logd("%s start\n", __FUNCTION__);

	while (uiomux) {
		tsem_down(uio_sem);
		if (*exit_flag)
			break;
		while (uiomux && !*exit_flag) {
			logd("wait for an interrupt...\n");
			ret = uiomux_sleep(uiomux, UIOMUX_SH_VPU);
			if (ret < 0) {
				break;
			}
			logd("got an interrupt! (%d)\n", ret);
			if (ufunc)
				ufunc(uarg);
		}
	}
	free (arg);
	return NULL;
}
void
uio_wakeup() {
	uiomux_wakeup(uiomux, UIOMUX_SH_VPU);
}

void
uio_exit_handler(tsem_t *uio_sem, int *exit_flag) {
	*exit_flag = 1;
	tsem_up(uio_sem);
}

int
uio_create_int_handle(pthread_t *thid,
		      void *(*routine)(void *), void *arg,
		      tsem_t *uio_sem, int *exit_flag)
{
	int ret;
	void **args;

	args = calloc(4, sizeof(void *));
	args[0] = routine;
	args[1] = arg;
	args[2] = uio_sem;
	args[3] = exit_flag;
	*exit_flag = 0;

	ret = pthread_create(thid, NULL, uio_int_handler,
			     (void *)args);

	return ret;
}

/**
 *
 */
void *
uio_init(char *name, unsigned long *paddr_reg,
	 unsigned long *paddr_pmem, size_t *size_pmem)
{
	uiomux_resource_t uiores;

	pthread_mutex_lock(&uiomux_mutex);
	if (!uiomux) {
		uiores = uiomux_query();
		if (!(uiores & UIOMUX_SH_VPU))
			return NULL;
		uiomux = uiomux_open();
	}
	ref_cnt++;
	pthread_mutex_unlock(&uiomux_mutex);
	uiomux_get_mmio(uiomux, UIOMUX_SH_VPU, paddr_reg, NULL, NULL);
	uiomux_get_mem(uiomux, UIOMUX_SH_VPU, paddr_pmem,
		       (unsigned long *)size_pmem, NULL);

	return (void *)uiomux;
}

void
uio_deinit() {
	pthread_mutex_lock(&uiomux_mutex);
	ref_cnt--;
	if (!ref_cnt) {
		uiomux_close(uiomux);
		uiomux = NULL;
	}
	pthread_mutex_unlock(&uiomux_mutex);
}

int
uio_get_virt_memory(void **address, unsigned long *size) {
	uiomux_get_mem(uiomux, UIOMUX_SH_VPU, NULL,
		       size, address);
	return 0;
}

/**
 *
 */
long
mciph_uf_mem_read(unsigned long src_addr,
		  unsigned long dst_addr, long count)
{
	void *src_vaddr;
	logd("%s(%lx, %lx, %ld) invoked.\n", __FUNCTION__,
	       src_addr, dst_addr, count);
	src_vaddr = uiomux_phys_to_virt(uiomux, UIOMUX_SH_VPU, src_addr);
	if ((unsigned long)src_vaddr != dst_addr)
		memcpy((void *)dst_addr, src_vaddr, count);
	else
		logd("%s: copy between the same region\n",
			__FUNCTION__);

	return count;
}

/**
 *
 */
long
mciph_uf_mem_write(unsigned long src_addr,
		   unsigned long dst_addr, long count)
{
	void *dst_vaddr;

	logd("%s(%lx, %lx, %ld) invoked.\n", __FUNCTION__,
	     src_addr, dst_addr, count);
	dst_vaddr = uiomux_phys_to_virt(uiomux, UIOMUX_SH_VPU, dst_addr);
	if (src_addr != (unsigned long)dst_vaddr)
		memcpy(dst_vaddr, (void *)src_addr, count);
	else
		logd("%s: copy between the same region\n",
		     __FUNCTION__);

	return count;
}

/**
 *
 */
long
mciph_uf_reg_table_read(unsigned long src_addr,
			unsigned long reg_table, long size)
{
	void *src_vaddr;
	int count = size;
	unsigned int val;

	logd("%s(%08lx, %08lx, %ld) invoked.\n", __FUNCTION__,
	     src_addr, reg_table, size);
	src_vaddr = uiomux_phys_to_virt(uiomux, UIOMUX_SH_VPU, src_addr);
	while (count > 0) {
		val = *(unsigned int *)src_vaddr;
		switch (src_addr) {
		case 0xfe900010:
			logd("%s(VP5_IRQ_ENB) = %08x\n",
			       __FUNCTION__, val);
			break;
		case 0xfe900014:
			logd("%s(VP5_IRQ_STA) = %08x\n",
			       __FUNCTION__, val);
			break;
		case 0xfe900020:
			logd("%s(VP5_STATUS) = %08x\n",
			       __FUNCTION__, val);
			break;
		}
		*(unsigned long *)reg_table = val;
		src_vaddr += sizeof(unsigned int);
		reg_table += sizeof(unsigned int);
		count--;
	}

	return size;
}

/**
 *
 */
long
mciph_uf_reg_table_write(unsigned long dst_addr,
			 unsigned long reg_table, long size)
{
	void *dst_vaddr;
	int count = size;
	unsigned int val;

	logd("%s(%08lx, %08lx, %ld) invoked.\n", __FUNCTION__,
	     dst_addr, reg_table, size);
	dst_vaddr = uiomux_phys_to_virt(uiomux, UIOMUX_SH_VPU, dst_addr);
	while (count > 0) {
		val = *(unsigned int *)reg_table;
		switch (dst_addr) {
		case 0xfe900010:
			logd("%s(VP5_IRQ_ENB, %08x)\n",
			       __FUNCTION__, val);
			break;
		case 0xfe900014:
			logd("%s(VP5_IRQ_STA, %08x)\n",
			       __FUNCTION__, val);
			break;
		case 0xfe900020:
			logd("%s(VP5_STATUS, %08x)\n",
			       __FUNCTION__, val);
			break;
		}
		*(unsigned long *)dst_vaddr = val;
		reg_table += sizeof(unsigned int);
		dst_vaddr += sizeof(unsigned int);
		count--;
	}

	return size;
}

/**
 *
 */
void
mciph_uf_set_imask(long mask_enable, long now_interrupt)
{
	static unsigned int save[2 + 1];
	unsigned int *vp5_irq_enb;

	logd("%s(%lx, %lx) invoked.\n", __FUNCTION__,
	       mask_enable, now_interrupt);

	if ((mask_enable < 0) || (mask_enable > 1) ||
	    (now_interrupt < 0) || (now_interrupt > 2))
		return;

	vp5_irq_enb = uiomux_phys_to_virt(uiomux,
					  UIOMUX_SH_VPU, 0xfe900010);
	if (vp5_irq_enb == NULL)
		return;

	/* disable preempt? */
	if (mask_enable == MCIPH_ON) {
		save[now_interrupt] = *vp5_irq_enb;	// save the current
							// disable all
		*vp5_irq_enb = 0U;			// mask all
	} else if (save[now_interrupt] != 0U) {
		*vp5_irq_enb |= save[now_interrupt];	// restore mask
							// enable interrupt
	}
	return;
}

unsigned long
uio_virt_to_phys(void *context, long mode, unsigned long addr)
{
	unsigned long paddr;

	logd("%s(%s, %lx) = ", __FUNCTION__,
	       (mode == MCIPH_DEC) ? "MCIPH_DEC" : "MCIPH_ENC",
	       addr);

	paddr = uiomux_virt_to_phys(uiomux, UIOMUX_SH_VPU, (void *)addr);

	logd("%lx\n", paddr);

	return paddr;
}

void *
uio_phys_to_virt(unsigned long paddr)
{
	void *vaddr;

	vaddr = uiomux_phys_to_virt(uiomux, UIOMUX_SH_VPU, paddr);

	return vaddr;
}

void
uiomux_lock_vpu() {
	logd("Locking VPU in thread %lx\n", pthread_self());
	uiomux_lock(uiomux, UIOMUX_SH_VPU);
	logd("Locked: %lx\n", pthread_self());
}

void
uiomux_unlock_vpu() {
	logd("Unlocking VPU in thread %lx\n", pthread_self());
	uiomux_unlock(uiomux, UIOMUX_SH_VPU);
}
