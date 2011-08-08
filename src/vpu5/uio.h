/************************************************************************

 Copyright (C) 2009 Renesas Solutions Corp.

 This code is LGPL license

 Kuninori Morimoto <morimoto.kuninori@renesas.com>

************************************************************************/
#ifndef UIO_H
#define UIO_H

#define u32 unsigned long
#define u16 unsigned short
#define u8  unsigned char

struct uio_device {
	void *up;
	void (*callback) (void *arg);
	void *arg;
};

struct uio_map {
	u32	 addr;
	u32	 size;
	void	*iomem;
};

struct uio {
	struct uio_device	dev;
	struct uio_map		mmio;
	struct uio_map		mem;
};

void uio_deinit(struct uio *uio);
int uio_init(struct uio *uio, const char *name);

u32 uio_read32(struct uio_map *ump, u32 ofs);
u16 uio_read16(struct uio_map *ump, u32 ofs);
u8  uio_read8 (struct uio_map *ump, u32 ofs);

void uio_write32(struct uio_map *ump, u32 val, u32 ofs);
void uio_write16(struct uio_map *ump, u16 val, u32 ofs);
void uio_write8 (struct uio_map *ump, u8  val, u32 ofs);

void uio_set_interrupt_callback(struct uio *uio, void (*callback) (void *arg),
				void *arg);

#endif
