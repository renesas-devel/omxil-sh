/*
 * simple_avcdec: simple_avcdec.h
 * Copyright (C) 2010 IGEL Co., Ltd
 */
#ifndef __SIMPLE_AVCDEC_H_
#define __SIMPLE_AVCDEC_H_
#include "mcvdec.h"

int logd(const char *format, ...);
int loge(const char *format, ...);

void *
pmem_alloc(size_t size, int align, unsigned long *paddr);
void
pmem_free(void *vaddr, size_t size);

unsigned long
uio_virt_to_phys(void *context, long mode, unsigned long addr);
void *
uio_phys_to_virt(unsigned long paddr);

#if 0
long
decode_init(MCVDEC_CONTEXT_T **context);
#endif
int
decode_prepare(void *context);
int
decode_main(void *context, int fd);
int
decode_finalize(void *context);

#endif /* __SIMPLE_AVCDEC_H_ */
