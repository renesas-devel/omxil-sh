/*
 * simple_avcdec: uio.h
 * Copyright (C) 2010 IGEL Co., Ltd
 */
#ifndef __UIO_H_
#define __UIO_H_
#define MAXNAMELEN	256
#define MAXUIOIDS	32

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
#endif /* __UIO_H_ */
