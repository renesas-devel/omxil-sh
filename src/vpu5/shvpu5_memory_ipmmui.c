/**
   src/vpu5/shvpu5_memory_ipmmui.c

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
#include "shvpu5_common_uio.h"
#include "shvpu5_common_log.h"
#include "mciph.h"
#include <sys/file.h>
#include <sys/mman.h>
#include "shvpu5_memory_util.h"

#define IPMMUI_MEMSIZE (1048576 * 100 - 4095)

struct ipmmui_list {
	struct ipmmui_list *next;
	char *vaddr;
	unsigned long paddr;
	size_t size;
};

static struct ipmmui_list *ipmmui_alloc = NULL;
static char *ipmmui_vaddr = NULL;
static unsigned long ipmmui_paddr;
static size_t ipmmui_size;
static pthread_mutex_t ipmmui_mutex = PTHREAD_MUTEX_INITIALIZER;

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
		return (size > (size_t) (p->vaddr - tvaddr));
	else
		return (p->size > (size_t) (tvaddr - p->vaddr));
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
	tvaddr = (char *)tvaddr + tpaddr - _tpaddr;
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
ipmmui_pmem_alloc(size_t size, int align, unsigned long *paddr)
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
ipmmui_pmem_free(void *_vaddr, size_t size)
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
ipmmui_phys_pmem_free(unsigned long paddr, size_t size)
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

/**
 *
 */
int
ipmmui_memory_init(unsigned long *paddr_pmem, size_t *size_pmem)
{
	FILE *fp;
	int mapsize, mapaddr, fd, i;

	ipmmui_vaddr = NULL;
	fp = fopen("/sys/kernel/ipmmui/vpu5/map", "w");
	if (!fp) {
		loge("Cannot open /sys/kernel/ipmmui/vpu5/map for write");
		goto ipmmui_error;
	}
	if (fprintf(fp, "0,0") <= 0) {
		loge("Cannot write to /sys/kernel/ipmmui/vpu5/map");
		goto ipmmui_error;
	}
	fflush(fp);
	fd = open("/dev/ipmmui", O_RDWR);
	if (fd < 0) {
		loge("Cannot open /dev/ipmmui");
		goto ipmmui_error;
	}
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

	if (paddr_pmem)
		*paddr_pmem = ipmmui_paddr;
	if (size_pmem)
		*size_pmem = ipmmui_size;
	return 0;

ipmmui_error:
	if (fp)
		fclose(fp);
	if (ipmmui_vaddr)
		munmap(ipmmui_vaddr, IPMMUI_MEMSIZE);
	return -1;
}

void
ipmmui_memory_deinit() {
	FILE *fp;

	fp = fopen("/sys/kernel/ipmmui/vpu5/map", "w");
	if (fp) {
		fprintf(fp, "0,0");
		fclose(fp);
	}
	munmap(ipmmui_vaddr, IPMMUI_MEMSIZE);
	if (ipmmui_alloc)
		loge("ipmmui_alloc should be NULL, possible memory leak!");
}

int
ipmmui_get_virt_memory(void **address, unsigned long *size) {
	if (address)
		*address = ipmmui_vaddr;
	if (size)
		*size = ipmmui_size;
	return 0;
}

int
ipmmui_get_phys_memory(unsigned long *address, unsigned long *size) {
	if (address)
		*address = ipmmui_paddr;
	if (size)
		*size = ipmmui_size;
	return 0;
}

/**
 *
 */
long
ipmmui_mem_read(unsigned long src_addr,
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
ipmmui_mem_write(unsigned long src_addr,
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

unsigned long
ipmmui_virt_to_phys(void *addr)
{
	unsigned long paddr;

	if ((char *)addr >= ipmmui_vaddr &&
	    (char *)addr < ipmmui_vaddr + ipmmui_size)
		paddr = (char *)addr - ipmmui_vaddr + ipmmui_paddr;
	else
		paddr = 0xFFFFFFFF;

	logd("%lx\n", paddr);

	return paddr;
}

void *
ipmmui_phys_to_virt(unsigned long paddr)
{
	void *vaddr;

	if (paddr >= ipmmui_paddr && paddr < ipmmui_paddr + ipmmui_size)
		vaddr = &ipmmui_vaddr[paddr - ipmmui_paddr];
	else
		vaddr = NULL;

	return vaddr;
}

struct memory_ops ipmmui_ops = {
	.pmem_alloc = ipmmui_pmem_alloc,
	.pmem_free = ipmmui_pmem_free,
	.phys_pmem_free = ipmmui_phys_pmem_free,
	.memory_init = ipmmui_memory_init,
	.memory_deinit = ipmmui_memory_deinit,
	.get_virt_memory = ipmmui_get_virt_memory,
	.get_phys_memory = ipmmui_get_phys_memory,
	.mem_read = ipmmui_mem_read,
	.mem_write = ipmmui_mem_write,
	.virt_to_phys = ipmmui_virt_to_phys,
	.phys_to_virt = ipmmui_phys_to_virt,
};

struct memory_ops *get_memory_ops() {
	return &ipmmui_ops;
}
