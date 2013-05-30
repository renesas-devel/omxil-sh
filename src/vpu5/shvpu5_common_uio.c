/**
   src/vpu5/shvpu5_common_uio.c

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
#include <string.h>
#include <stdint.h>
#include "uiomux/uiomux.h"
#include "shvpu5_common_uio.h"
#include "shvpu5_common_log.h"
#include "mciph.h"
#include <sys/file.h>
#include "shvpu5_memory_util.h"

static UIOMux *uiomux = NULL;
static const char *uio_names[] = {
	"VPU",
	"VPC",
	"ICB_CACHE",
	NULL
};

#define VPU_UIO	(1 << 0)
#define VPC_UIO	(1 << 1)
#define ICB_UIO	(1 << 2)

static struct memory_ops *memops;
static pthread_mutex_t uiomux_mutex = PTHREAD_MUTEX_INITIALIZER;
static int ref_cnt = 0;
static unsigned long uio_reg_base = 0;

#define VPCCTL	4
#define VPCSTS	8

#define VPCCTL_ENB	(1 << 0)
#define VPCCTL_CLR	(1 << 1)
#define VPCCTL_LWSWP	(1 << 12)

static uint8_t *vpc_regs = NULL;

int vpc_init(void) {
	unsigned long tmp;
	uiomux_get_mmio(uiomux, VPC_UIO, NULL, NULL, &vpc_regs);
	tmp = *((unsigned long *) (vpc_regs + VPCCTL));
	*((unsigned long *) (vpc_regs + VPCCTL)) =
		tmp | VPCCTL_ENB | VPCCTL_CLR | VPCCTL_LWSWP;
	return 0;
}

int vpc_clear(void) {
	unsigned long tmp;
	tmp = *((unsigned long *) (vpc_regs + VPCCTL));
	*((unsigned long *) (vpc_regs + VPCCTL)) =
		tmp | VPCCTL_CLR;
	return 0;
}

#ifdef ICBCACHE_FLUSH
#define MEBUFCCNTR		(0x94)
#define MEBUFCCNTR_CE		(1 << 27)
#define MEBUFCCNTR_FLUSH	(1 << 31)
#define MEBUFCCNTR_CMSA		(0x148)

static uint8_t *icbcache_regs = NULL;

int icbcache_init(void) {
	uiomux_get_mmio(uiomux, ICB_UIO, NULL, NULL, &icbcache_regs);
	*((unsigned long *) (icbcache_regs + MEBUFCCNTR)) =
		MEBUFCCNTR_CE | MEBUFCCNTR_FLUSH | MEBUFCCNTR_CMSA;
	return 0;
}

int icbcache_deinit(void) {
	unsigned long tmp;
	tmp = *((unsigned long*) (icbcache_regs + MEBUFCCNTR));
	*((unsigned long *) (icbcache_regs + MEBUFCCNTR)) =
		tmp & ~MEBUFCCNTR_CE;
	return 0;
}

void icbcache_flush(void) {
	unsigned long tmp;
	tmp = *((unsigned long*) (icbcache_regs + MEBUFCCNTR));
	*((unsigned long*) (icbcache_regs + MEBUFCCNTR)) =
		tmp | MEBUFCCNTR_FLUSH;
}
#endif
int
uio_interrupt_clear()
{
	unsigned int *vp5_irq_sta;

	vp5_irq_sta = uiomux_phys_to_virt(uiomux,
					  VPU_UIO, uio_reg_base + 0x14);
	if (vp5_irq_sta == NULL)
		return -1;

	*vp5_irq_sta = 0U;			// mask all
	return 0;
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

	while (uiomux && !*exit_flag) {
		logd("wait for an interrupt...\n");
		ret = uiomux_sleep(uiomux, VPU_UIO);
		if (ret < 0) {
			break;
		}
		logd("got an interrupt! (%d)\n", ret);
		if (ufunc)
			ufunc(uarg);
	}
	free (arg);
	return NULL;
}
void
uio_wakeup() {
	uiomux_wakeup(uiomux, VPU_UIO);
}

void
uio_exit_handler(tsem_t *uio_sem, int *exit_flag) {
	*exit_flag = 1;
}

int
uio_create_int_handle(pthread_t *thid,
		      void *(*routine)(void *), void *arg,
		      tsem_t *uio_sem, int *exit_flag)
{
	int ret, priority;
	void **args;
	static struct sched_param sparam;

	args = calloc(4, sizeof(void *));
	args[0] = routine;
	args[1] = arg;
	args[2] = uio_sem;
	args[3] = exit_flag;
	*exit_flag = 0;

	ret = pthread_create(thid, NULL, uio_int_handler,
			     (void *)args);
        priority = sched_get_priority_max(SCHED_RR);
	sparam.sched_priority = priority;
	if ((ret = pthread_setschedparam(*thid, SCHED_RR, &sparam))) {
		loge("WARN: the uio interrupt handler "
		     "may be preempted.");
	}

	return ret;
}

static unsigned int save[2 + 1];
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
/*		uiores = uiomux_query();
		if (!(uiores & UIOMUX_SH_VPU))
			return NULL;*/
		uiomux = uiomux_open_named(uio_names);
		memops = get_memory_ops();
		if (memops->memory_init(paddr_pmem, size_pmem) != 0) {
			memops = NULL;
			uiomux_close(uiomux);
			uiomux = NULL;
			pthread_mutex_unlock(&uiomux_mutex);
			return NULL;
		}
		/* clear register save on init */
		save[0] = save[1] = save[2] = 0;
#if defined(VPC_ENABLE)
		vpc_init();
#endif
#ifdef ICBCACHE_FLUSH
		icbcache_init();
#endif
	} else {
		memops->get_phys_memory(paddr_pmem, size_pmem);
	}
	ref_cnt++;

	pthread_mutex_unlock(&uiomux_mutex);

	uiomux_get_mmio(uiomux, VPU_UIO, &uio_reg_base, NULL, NULL);

	if (paddr_reg)
		*paddr_reg = uio_reg_base;
	return (void *)uiomux;
}


void
uio_deinit() {
	pthread_mutex_lock(&uiomux_mutex);
	ref_cnt--;
	if (!ref_cnt) {
#ifdef ICBCACHE_FLUSH
		icbcache_deinit();
#endif
		memops->memory_deinit();
		uiomux_close(uiomux);
		uiomux = NULL;
	}
	pthread_mutex_unlock(&uiomux_mutex);
}

/**
 *
 */
long
vpu5_mmio_read(unsigned long src_addr,
			unsigned long reg_table, long size)
{
	uint8_t *src_vaddr;
	int count = size;
	unsigned int val;

	logd("%s(%08lx, %08lx, %ld) invoked.\n", __FUNCTION__,
	     src_addr, reg_table, size);
	/* "VPU internal" registers reside in RAM */
	src_vaddr = uio_phys_to_virt(src_addr);
	while (count > 0) {
		val = *(unsigned int *)src_vaddr;
		switch (src_addr - uio_reg_base) {
		case 0x10:
			logd("%s(VP5_IRQ_ENB) = %08x\n",
			       __FUNCTION__, val);
			break;
		case 0x14:
			logd("%s(VP5_IRQ_STA) = %08x\n",
			       __FUNCTION__, val);
			break;
		case 0x20:
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
vpu5_mmio_write(unsigned long dst_addr,
			 unsigned long reg_table, long size)
{
	uint8_t *dst_vaddr;
	int count = size;
	unsigned int val;

	logd("%s(%08lx, %08lx, %ld) invoked.\n", __FUNCTION__,
	     dst_addr, reg_table, size);
	/* "VPU internal" registers reside in RAM */
	dst_vaddr = uio_phys_to_virt(dst_addr);
	while (count > 0) {
		val = *(unsigned int *)reg_table;
		switch (dst_addr - uio_reg_base) {
		case 0x10:
			logd("%s(VP5_IRQ_ENB, %08x)\n",
			       __FUNCTION__, val);
			break;
		case 0x14:
			logd("%s(VP5_IRQ_STA, %08x)\n",
			       __FUNCTION__, val);
			break;
		case 0x20:
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
vpu5_set_imask(long mask_enable, long now_interrupt)
{
	unsigned int *vp5_irq_enb;

	logd("%s(%lx, %lx) invoked.\n", __FUNCTION__,
	       mask_enable, now_interrupt);

	if ((mask_enable < 0) || (mask_enable > 1) ||
	    (now_interrupt < 0) || (now_interrupt > 2))
		return;

	vp5_irq_enb = uiomux_phys_to_virt(uiomux,
					  VPU_UIO, uio_reg_base + 0x10);
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

unsigned long uio_register_base(void) {
	return uio_reg_base;
}

void
uiomux_lock_vpu() {
	logd("Locking VPU in thread %lx\n", pthread_self());
	uiomux_lock(uiomux, VPU_UIO);
	logd("Locked: %lx\n", pthread_self());
}

void
uiomux_unlock_vpu() {
	logd("Unlocking VPU in thread %lx\n", pthread_self());
	uiomux_unlock(uiomux, VPU_UIO);
}

/* Memory management functions */
void *
pmem_alloc(size_t size, int align, unsigned long *paddr)
{
	return memops->pmem_alloc(size, align, paddr);
}

void
pmem_free (void *vaddr, size_t size)
{
	memops->pmem_free(vaddr, size);
}

void
phys_pmem_free (unsigned long paddr, size_t size)
{
	memops->phys_pmem_free(paddr, size);
}

int
uio_get_virt_memory(void **vaddr, unsigned long *size)
{
	return memops->get_virt_memory(vaddr, size);
}

long
vpu5_mem_read(unsigned long src_addr, unsigned long dst_addr, long count)
{
	return memops->mem_read(src_addr, dst_addr, count);
}

long
vpu5_mem_write(unsigned long src_addr, unsigned long dst_addr, long count)
{
	return memops->mem_write(src_addr, dst_addr, count);
}

unsigned long
uio_virt_to_phys(void *context, long mode, unsigned long addr)
{
	unsigned long paddr;
	logd("%s(%s, %lx) = ", __FUNCTION__,
	       (mode == MCIPH_DEC) ? "MCIPH_DEC" : "MCIPH_ENC",
	       addr);

	paddr = memops->virt_to_phys((void *)addr);

	if (paddr == PHYS_UNDEF)
		paddr = uiomux_virt_to_phys(uiomux, VPU_UIO, (void *)addr);

	return paddr;
}

void *
uio_phys_to_virt(unsigned long paddr)
{
	void *vaddr;

	vaddr = memops->phys_to_virt(paddr);

	if (vaddr == NULL)
		vaddr = uiomux_phys_to_virt(uiomux, VPU_UIO, paddr);

	return vaddr;
}

/* UIO memory management is included inline here, since it uses the
   same UIOMux as the register definitions.  Other implementations
   can be defined elsewhere */

void
uiomux_register_memory(void *vaddr, unsigned long paddr, int size) {
	uiomux_register(vaddr, paddr, size);
}

#if defined(VPU_UIO_MEMORY)

int
uiomem_memory_init(unsigned long *paddr_pmem, size_t *size_pmem)
{
	return uiomem_get_phys_memory(paddr_pmem,
			(unsigned long *)size_pmem);
}

void
uiomem_memory_deinit() {
}

void *
uiomem_pmem_alloc(size_t size, int align, unsigned long *paddr)
{
	void *vaddr;

	vaddr = uiomux_malloc(uiomux, VPU_UIO, size, align);
	if (vaddr && paddr)
		*paddr = uiomux_virt_to_phys(uiomux, VPU_UIO, vaddr);

	return vaddr;
}

void
uiomem_pmem_free(void *vaddr, size_t size)
{
	return uiomux_free(uiomux, VPU_UIO, vaddr, size);
}

void
uiomem_phys_pmem_free(unsigned long paddr, size_t size)
{
	return uiomux_free(uiomux, VPU_UIO,
		uiomux_phys_to_virt(uiomux, VPU_UIO, paddr), size);
}

int
uiomem_get_virt_memory(void **address, unsigned long *size) {
	uiomux_get_mem(uiomux, VPU_UIO, NULL,
		       size, address);
	return 0;
}

int
uiomem_get_phys_memory(unsigned long *address, unsigned long *size) {
	uiomux_get_mem(uiomux, VPU_UIO, address,
		       size, NULL);
	return 0;
}

/**
 *
 */
long
uiomem_mem_read(unsigned long src_addr,
		  unsigned long dst_addr, long count)
{
	void *src_vaddr;
	logd("%s(%lx, %lx, %ld) invoked.\n", __FUNCTION__,
	       src_addr, dst_addr, count);
	src_vaddr = uiomux_phys_to_virt(uiomux, VPU_UIO, src_addr);
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
uiomem_mem_write(unsigned long src_addr,
		   unsigned long dst_addr, long count)
{
	void *dst_vaddr;

	logd("%s(%lx, %lx, %ld) invoked.\n", __FUNCTION__,
	     src_addr, dst_addr, count);
	dst_vaddr = uiomux_phys_to_virt(uiomux, VPU_UIO, dst_addr);
	if (src_addr != (unsigned long)dst_vaddr)
		memcpy(dst_vaddr, (void *)src_addr, count);
	else
		logd("%s: copy between the same region\n",
		     __FUNCTION__);

	return count;
}


unsigned long
uiomem_virt_to_phys(void *context, long mode, unsigned long addr)
{
	return PHYS_UNDEF;
}

void *
uiomem_phys_to_virt(unsigned long paddr)
{
	return NULL;
}


struct memory_ops uiomem_ops = {
	.pmem_alloc = uiomem_pmem_alloc,
	.pmem_free = uiomem_pmem_free,
	.phys_pmem_free = uiomem_phys_pmem_free,
	.memory_init = uiomem_memory_init,
	.memory_deinit = uiomem_memory_deinit,
	.get_virt_memory = uiomem_get_virt_memory,
	.get_phys_memory = uiomem_get_phys_memory,
	.mem_read = uiomem_mem_read,
	.mem_write = uiomem_mem_write,
	.virt_to_phys = uiomem_virt_to_phys,
	.phys_to_virt = uiomem_phys_to_virt,
};

struct memory_ops *get_memory_ops() {
	return &uiomem_ops;
}
#endif
