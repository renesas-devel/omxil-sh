/**
   src/vpu5/shvpu5_common_driver.c

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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "mciph.h"
#include "mciph_hg.h"
#include "shvpu5_driver.h"

static shvpu_driver_t *pDriver;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;
static int nCodecInstances;

static inline void *
malloc_aligned(size_t size, int align)
{
	return malloc(size);
}

static void
handle_shvpu5_interrupt(void *arg)
{
	MCIPH_DRV_INFO_T *pDrvInfo = arg;

	logd("----- invoke mciph_vpu5_int_handler() -----\n");
	mciph_vpu5_int_handler(pDrvInfo);
	logd("----- resume from mciph_vpu5_int_handler() -----\n");

	return;
}

int
shvpu_driver_deinit(shvpu_driver_t *pHandle)
{
	pthread_mutex_lock(&initMutex);

	if ((pHandle != pDriver) || (nCodecInstances <= 0)) {
		pthread_mutex_unlock(&initMutex);
		return -1;
	}

	nCodecInstances--;
	if (nCodecInstances == 0) {
		uio_exit_handler(&pDriver->uioSem, &pDriver->isExit);
		uio_wakeup();
		pthread_join(pDriver->intrHandler, NULL);
		uio_deinit();
		free(pDriver);
		pDriver = NULL;
	}
	pthread_mutex_unlock(&initMutex);

	return 0;
}

long
shvpu_driver_init(shvpu_driver_t **ppDriver)
{
	long ret = 0;

	pthread_mutex_lock(&initMutex);

	/* pass the pointer if the driver was already initialized */
	if (nCodecInstances > 0)
		goto init_already;

	pDriver = (shvpu_driver_t *)calloc(1, sizeof(shvpu_driver_t));
	if (pDriver == NULL) {
		ret = -1;
		goto init_failed;
	}
	memset((void *)pDriver, 0, sizeof(shvpu_driver_t));

	/*** initialize vpu ***/
	pDriver->wbufVpu5.work_size = MCIPH_HG_WORKAREA_SIZE;
	pDriver->wbufVpu5.work_area_addr =
		malloc_aligned(pDriver->wbufVpu5.work_size, 4);
	logd("work_area_addr = %p\n", pDriver->wbufVpu5.work_area_addr);
	if ((pDriver->wbufVpu5.work_area_addr == NULL) ||
	    ((unsigned int)pDriver->wbufVpu5.work_area_addr & 0x03U)) {
		ret = -1;
		goto init_failed;
	}

	pDriver->vpu5Init.vpu_base_address		= 0xfe900000;
	pDriver->vpu5Init.vpu_image_endian		= MCIPH_LIT;
	pDriver->vpu5Init.vpu_stream_endian		= MCIPH_LIT;
	pDriver->vpu5Init.vpu_firmware_endian		= MCIPH_LIT;
	pDriver->vpu5Init.vpu_interrupt_enable		= MCIPH_ON;
	pDriver->vpu5Init.vpu_clock_supply_control	= MCIPH_CLK_CTRL;
	pDriver->vpu5Init.vpu_constrained_mode		= MCIPH_OFF;
	pDriver->vpu5Init.vpu_address_mode		= MCIPH_ADDR_32BIT;
	pDriver->vpu5Init.vpu_reset_mode			= MCIPH_RESET_SOFT;
	logd("----- invoke mciph_vpu5Init() -----\n");
	ret = mciph_vpu5_init(&(pDriver->wbufVpu5),
			      (MCIPH_API_T *)&mciph_hg_api_tbl,
			      &(pDriver->vpu5Init),
			      &(pDriver->pDrvInfo));
	logd("----- resume from mciph_vpu5_init() -----\n");

	if (ret != MCIPH_NML_END)
		goto init_failed;

	/* register an interrupt handler */
	tsem_init(&pDriver->uioSem, 0);
	ret = uio_create_int_handle(&pDriver->intrHandler,
				    handle_shvpu5_interrupt,
				    pDriver->pDrvInfo,
				    &pDriver->uioSem, &pDriver->isExit);
	if (ret < 0)
		goto init_failed;

init_already:
	*ppDriver = pDriver;
	nCodecInstances++;
init_failed:
	pthread_mutex_unlock(&initMutex);
	return ret;
}

unsigned long
shvpu5_load_firmware(char *filename, int *size)
{
	void *vaddr;
	unsigned char *p;
	unsigned long paddr;
	int fd;
	size_t len;
	ssize_t ret;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("firmware open");
		goto fail_open;
	}
	len = lseek(fd, 0, SEEK_END);
	logd("size of %s = %x\n", filename, len);

	*size = len;
	vaddr = p = pmem_alloc(len, 32, &paddr);
	if (vaddr == NULL) {
		fprintf(stderr, "pmem alloc failed.\n");
		goto fail_pmem_alloc;
	}

	lseek(fd, 0, SEEK_SET);
	do {
		ret = read(fd, p, len);
		if (ret <= 0) {
			perror("firmware read");
			goto fail_read;
		}
		len -= ret;
		p += ret;
	} while (len > 0);
	return paddr;
fail_read:
	pmem_free(vaddr, lseek(fd, 0, SEEK_END));
fail_pmem_alloc:
	close(fd);
fail_open:
	return -1;
}