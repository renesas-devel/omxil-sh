#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include "spu.h"
#include "spu_dsp.h"

#define ERR(msg) fprintf (stderr, "spu: %s\n", msg)

struct spu2_dsp {
	pthread_mutex_t lock;
	pthread_mutex_t sem;
	pthread_t thread;
	struct spu_dsp *dsp;
	unsigned int id;
	void (*irq_callback) (void *data, unsigned int id);
	void *irq_callback_data;
};

static void interrupt_callback (void *args);
static struct spu2_dsp dsp0;

static u32
__read (struct spu2_dsp *_this, u32 addr)
{
	return spudsp_read (_this->id, addr);
}

static void
__write (struct spu2_dsp *_this, u32 addr, u32 data)
{
	return spudsp_write (_this->id, addr, data);
}

static long
_enter_critical_section (struct spu2_dsp *_this)
{
	pthread_mutex_lock (&_this->lock);
	return 0;
}

static long
_leave_critical_section (struct spu2_dsp *_this)
{
	pthread_mutex_unlock (&_this->lock);
	return 0;
}

static long
_set_event (struct spu2_dsp *_this)
{
	pthread_mutex_unlock (&_this->sem);
	return 0;
}

static long
_wait_event (struct spu2_dsp *_this, unsigned long timeout)
{
#ifndef ANDROID
	struct timespec to;
	struct timeval tv;
	time_t time_s = 0;
	if (timeout < 0) {
		timeout = 100;
	} else if (timeout >= 10000) {
		time_s = 10;
		timeout = 0;
	} else {
		time_s = timeout/1000;
		timeout = timeout%1000;
	}
	if (!gettimeofday(&tv, NULL)) {
		to.tv_sec = time_s + tv.tv_sec;
		to.tv_nsec = (tv.tv_usec + timeout * 1000 ) * 1000;
		return pthread_mutex_timedlock (&_this->sem, &to);
	} else {
		return -1;
	}
#else
	/* FIXME:
	   Android does not implement pthread_mutex_timedlock(). */
	return pthread_mutex_lock(&_this->sem);
#endif
}

static void
_read (struct spu2_dsp *_this, u32 * addr, u32 * data, u32 size)
{
	int i;

	for (i = 0; i < size; i++)
		data[i] = __read (_this, (u32) addr + (u32) (i * 4));
}

static void
_write (struct spu2_dsp *_this, u32 * addr, u32 * data, u32 size)
{
	int i;

	for (i = 0; i < size; i++)
		__write (_this, (u32) addr + (u32) (i * 4), data[i]);
}

static void
_interrupt_callback (struct spu2_dsp *_this)
{
	_this->irq_callback (_this->irq_callback_data, _this->id);
}

static int
_open (struct spu2_dsp *_this, unsigned int id)
{
	_this->id = id;
	pthread_mutex_init (&_this->lock, NULL);
	pthread_mutex_init (&_this->sem, NULL);
	pthread_mutex_lock (&_this->sem);
	_this->dsp = spudsp_open ((u32)_this->id, interrupt_callback, _this);
	if (_this->dsp == NULL) {
		ERR ("dsp open error");
		return -1;
	}
	return 0;
}

static void
_close (struct spu2_dsp *_this)
{
	spudsp_close (_this->dsp);
}

static void
_get_workarea (struct spu2_dsp *_this, uint32_t *addr, uint32_t *size,
	       void **io)
{
	*addr = spudsp_get_workarea_addr (_this->dsp);
	*size = spudsp_get_workarea_size (_this->dsp);
	*io = (void *)spudsp_get_workarea_io (_this->dsp);
}

void
spu_get_workarea (uint32_t *addr, uint32_t *size, void **io)
{
	_get_workarea (&dsp0, addr, size, io);
}

static void
interrupt_callback (void *args)
{
	struct spu2_dsp *dsp;

	dsp = args;
	_interrupt_callback (dsp);
}

long
spu_enter_critical_section (void)
{
	return _enter_critical_section (&dsp0);
}

long
spu_leave_critical_section (void)
{
	return _leave_critical_section (&dsp0);
}

long
spu_set_event (void)
{
	return _set_event (&dsp0);
}

long
spu_wait_event (unsigned long timeout)
{
	return _wait_event (&dsp0, timeout);
}

void
spu_read (unsigned long *addr, unsigned long *data, unsigned long size)
{
	return _read (&dsp0, addr, data, size);
}

void
spu_write (unsigned long *addr, unsigned long *data, unsigned long size)
{
	return _write (&dsp0, addr, data, size);
}

int
spu_init (void (*irq_callback) (void *data, unsigned int id), void *data)
{
	dsp0.irq_callback = irq_callback;
	dsp0.irq_callback_data = data;
	if (spudsp_init () < 0)
		return -1;
	if (_open (&dsp0, 0) < 0) {
		spudsp_quit ();
		return -1;
	}
	return 0;
}

void
spu_deinit (void)
{
	_close (&dsp0);
	spudsp_quit ();
}
