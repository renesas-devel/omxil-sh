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
#include <sys/mman.h>

#define IPMMUI_MEMSIZE (1048576 * 64 - 4095)

struct ipmmui_list {
	struct ipmmui_list *next;
	char *vaddr;
	unsigned long paddr;
	size_t size;
};

static UIOMux *uiomux = NULL;

static struct ipmmui_list *ipmmui_alloc;
static char *ipmmui_vaddr;
static unsigned long ipmmui_paddr;
static size_t ipmmui_size;
static pthread_mutex_t ipmmui_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t uiomux_mutex = PTHREAD_MUTEX_INITIALIZER;
static int ref_cnt = 0;
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

static int
paddr_hit(struct ipmmui_list *p, unsigned long tpaddr, size_t size)
{
	if (tpaddr < p->paddr)
		return (size > p->paddr - tpaddr);
	else
		return (p->size > tpaddr - p->paddr);
}

static int
vaddr_hit(struct ipmmui_list *p, char *tvaddr, size_t size)
{
	if (tvaddr - p->vaddr < 0)
		return (size > p->vaddr - tvaddr);
	else
		return (p->size > tvaddr - p->vaddr);
}

static void *
tryalloc(void *tvaddr, unsigned long _tpaddr, int align, size_t size,
	 unsigned long *paddr)
{
	struct ipmmui_list *p;
	unsigned long tpaddr;

	tpaddr = _tpaddr;
	tpaddr += align - 1;
	tpaddr -= tpaddr % align;
	tvaddr += tpaddr - _tpaddr;
	if ((tpaddr - ipmmui_paddr) + size > ipmmui_size)
		return NULL;
	for (p = ipmmui_alloc; p; p = p->next) {
		if (paddr_hit(p, tpaddr, size))
			return NULL;
	}
	p = malloc (sizeof *p);
	p->vaddr = tvaddr;
	p->paddr = tpaddr;
	p->size = size;
	p->next = ipmmui_alloc;
	ipmmui_alloc = p;
	if (paddr)
		*paddr = tpaddr;
	return tvaddr;
}

void *
pmem_alloc(size_t size, int align, unsigned long *paddr)
{
	struct ipmmui_list *p;
	void *vaddr;

	if (size <= 0 || align <= 0)
		return NULL;
	vaddr = NULL;
	pthread_mutex_lock(&ipmmui_mutex);
	vaddr = tryalloc(ipmmui_vaddr, ipmmui_paddr, align, size, paddr);
	if (!vaddr) {
		for (p = ipmmui_alloc; p; p = p->next) {
			vaddr = tryalloc(p->vaddr + p->size, p->paddr +
					 p->size, align, size, paddr);
			if (vaddr)
				break;
		}
	}
	pthread_mutex_unlock(&ipmmui_mutex);
	return vaddr;
}

void
pmem_free(void *_vaddr, size_t size)
{
	struct ipmmui_list **p, **q, *a;
	char *vaddr;
	int s;

	vaddr = _vaddr;
	pthread_mutex_lock(&ipmmui_mutex);
	for (p = &ipmmui_alloc; (q = &(*p)->next), *p; p = q) {
		if (vaddr_hit(*p, vaddr, size)) {
			s = vaddr - (*p)->vaddr;
			if (s > 0) {
				a = malloc (sizeof *a);
				if (!a) /* error */
					return;
				a->vaddr = (*p)->vaddr;
				a->paddr = (*p)->paddr;
				a->size = s;
				a->next = ipmmui_alloc;
				ipmmui_alloc = a;
			}
			s = ((*p)->vaddr + size) - (vaddr + size);
			if (s > 0) {
				a = malloc (sizeof *a);
				if (!a) /* error */
					return;
				a->vaddr = (*p)->vaddr + (*p)->size - s;
				a->paddr = (*p)->paddr + (*p)->size - s;
				a->size = s;
				a->next = ipmmui_alloc;
				ipmmui_alloc = a;
			}
			a = *p;
			*p = *q;
			q = p;
			free (a);
		}
	}
	pthread_mutex_unlock(&ipmmui_mutex);
}

void
phys_pmem_free(unsigned long paddr, size_t size)
{
	struct ipmmui_list **p, **q, *a;
	int s;

	pthread_mutex_lock(&ipmmui_mutex);
	for (p = &ipmmui_alloc; (q = &(*p)->next), *p; p = q) {
		if (paddr_hit(*p, paddr, size)) {
			if (paddr > (*p)->paddr) {
				s = paddr - (*p)->paddr;
				a = malloc (sizeof *a);
				if (!a) /* error */
					return;
				a->vaddr = (*p)->vaddr;
				a->paddr = (*p)->paddr;
				a->size = s;
				a->next = ipmmui_alloc;
				ipmmui_alloc = a;
			}
			if ((*p)->paddr + size > paddr + size) {
				s = ((*p)->paddr + size) - (paddr + size);
				a = malloc (sizeof *a);
				if (!a) /* error */
					return;
				a->vaddr = (*p)->vaddr + (*p)->size - s;
				a->paddr = (*p)->paddr + (*p)->size - s;
				a->size = s;
				a->next = ipmmui_alloc;
				ipmmui_alloc = a;
			}
			a = *p;
			*p = *q;
			q = p;
			free (a);
		}
	}
	pthread_mutex_unlock(&ipmmui_mutex);
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
		ret = uiomux_sleep(uiomux, UIOMUX_SH_VPU);
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
	uiomux_wakeup(uiomux, UIOMUX_SH_VPU);
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
	FILE *fp;
	int mapsize, mapaddr, fd, i;

	ipmmui_vaddr = NULL;
	fp = fopen("/sys/kernel/ipmmui/vpu5/map", "w");
	if (!fp)
		goto ipmmui_error;
	if (fprintf(fp, "0,0") <= 0)
		goto ipmmui_error;
	fflush(fp);
	fd = open("/dev/ipmmui", O_RDWR);
	if (fd < 0)
		goto ipmmui_error;
	ipmmui_vaddr = mmap(NULL, IPMMUI_MEMSIZE, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE, fd, 0);
	close(fd);
	if (ipmmui_vaddr == MAP_FAILED) {
		ipmmui_vaddr = NULL;
		goto ipmmui_error;
	}
	if (fprintf(fp, "%p,%d", ipmmui_vaddr, IPMMUI_MEMSIZE) <= 0)
		goto ipmmui_error;
	fclose(fp);
	fp = fopen("/sys/kernel/ipmmui/vpu5/mapsize", "r");
	if (!fp || getc(fp) != '0' || getc(fp) != 'x' ||
	    fscanf(fp, "%x", &mapsize) != 1 || mapsize < IPMMUI_MEMSIZE)
		goto ipmmui_error;
	fclose(fp);
	fp = fopen("/sys/kernel/ipmmui/vpu5/mapaddr", "r");
	if (!fp || getc(fp) != '0' || getc(fp) != 'x' ||
	    fscanf(fp, "%x", &mapaddr) != 1)
		goto ipmmui_error;
	fclose(fp);
	ipmmui_paddr = mapaddr;
	ipmmui_size = mapsize;

	pthread_mutex_lock(&uiomux_mutex);
	if (!uiomux) {
		uiores = uiomux_query();
		if (!(uiores & UIOMUX_SH_VPU)) {
			munmap(ipmmui_vaddr, IPMMUI_MEMSIZE);
			return NULL;
		}
		uiomux = uiomux_open();
	}
	ref_cnt++;
	pthread_mutex_unlock(&uiomux_mutex);
	uiomux_get_mmio(uiomux, UIOMUX_SH_VPU, paddr_reg, NULL, NULL);

	if (paddr_pmem)
		*paddr_pmem = ipmmui_paddr;
	if (size_pmem)
		*size_pmem = ipmmui_size;

	/* clear register save on init */
	save[0] = save[1] = save[2] = 0;

	return (void *)uiomux;
ipmmui_error:
	if (fp)
		fclose(fp);
	if (ipmmui_vaddr)
		munmap(ipmmui_vaddr, IPMMUI_MEMSIZE);
	return NULL;
}

void
uio_deinit() {
	FILE *fp;

	pthread_mutex_lock(&uiomux_mutex);
	ref_cnt--;
	if (!ref_cnt) {
		uiomux_close(uiomux);
		uiomux = NULL;
	}
	pthread_mutex_unlock(&uiomux_mutex);
	fp = fopen("/sys/kernel/ipmmui/vpu5/map", "w");
	if (fp) {
		fprintf(fp, "0,0");
		fclose(fp);
	}
	munmap(ipmmui_vaddr, IPMMUI_MEMSIZE);
}

int
uio_get_virt_memory(void **address, unsigned long *size) {
	if (address)
		*address = ipmmui_vaddr;
	if (size)
		*size = ipmmui_size;
	return 0;
}

/**
 *
 */
long
vpu5_mem_read(unsigned long src_addr,
		  unsigned long dst_addr, long count)
{
	void *src_vaddr;
	logd("%s(%lx, %lx, %ld) invoked.\n", __FUNCTION__,
	       src_addr, dst_addr, count);
	src_vaddr = &ipmmui_vaddr[src_addr - ipmmui_paddr];
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
vpu5_mem_write(unsigned long src_addr,
		   unsigned long dst_addr, long count)
{
	void *dst_vaddr;

	logd("%s(%lx, %lx, %ld) invoked.\n", __FUNCTION__,
	     src_addr, dst_addr, count);
	dst_vaddr = &ipmmui_vaddr[dst_addr - ipmmui_paddr];
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
vpu5_mmio_read(unsigned long src_addr,
			unsigned long reg_table, long size)
{
	uint8_t *src_vaddr;
	int count = size;
	unsigned int val;

	logd("%s(%08lx, %08lx, %ld) invoked.\n", __FUNCTION__,
	     src_addr, reg_table, size);
	src_vaddr = uio_phys_to_virt(src_addr);
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
vpu5_mmio_write(unsigned long dst_addr,
			 unsigned long reg_table, long size)
{
	uint8_t *dst_vaddr;
	int count = size;
	unsigned int val;

	logd("%s(%08lx, %08lx, %ld) invoked.\n", __FUNCTION__,
	     dst_addr, reg_table, size);
	dst_vaddr = uio_phys_to_virt(dst_addr);
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
vpu5_set_imask(long mask_enable, long now_interrupt)
{
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

	if (addr >= (unsigned long)ipmmui_vaddr &&
	    addr < (unsigned long)ipmmui_vaddr + ipmmui_size)
		paddr = addr - (unsigned long)ipmmui_vaddr + ipmmui_paddr;
	else
		paddr = uiomux_virt_to_phys(uiomux, UIOMUX_SH_VPU,
					    (void *)addr);

	logd("%lx\n", paddr);

	return paddr;
}

void *
uio_phys_to_virt(unsigned long paddr)
{
	void *vaddr;

	if (paddr >= ipmmui_paddr && paddr < ipmmui_paddr + ipmmui_size)
		vaddr = &ipmmui_vaddr[paddr - ipmmui_paddr];
	else
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
