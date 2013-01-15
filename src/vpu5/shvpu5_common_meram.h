/**
   src/vpu55/shvpu5_common_meram.c

   This component implements H.264 / MPEG-4 AVC video codec.
   The H.264 / MPEG-4 AVC video encoder/decoder is implemented
   on the Renesas's VPU5HG middleware library.

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
#ifndef SHVPU_AVCDEC_MERAM_H
#define SHVPU_AVCDEC_MERAM_H

#if defined(MERAM_ENABLE) || defined (TL_CONV_ENABLE)
#include <meram/meram.h>
typedef struct {
	MERAM *meram;
	ICB *decY_icb;
	ICB *decC_icb;
} shvpu_meram_t;

int open_meram(shvpu_meram_t *mdata);
void close_meram(shvpu_meram_t *mdata);
unsigned long setup_icb(shvpu_meram_t *mdata,
	  ICB **icb,
	  unsigned long pitch,
	  unsigned long lines,
	  int res_lines,
	  int block_lines,
	  int rdnwr,
	  int index);
void set_meram_address(shvpu_meram_t *mdata, ICB *icb, unsigned long address);
void finish_meram_write(shvpu_meram_t *mdata, ICB *icb);
void finish_meram_read(shvpu_meram_t *mdata, ICB *icb);
#else
typedef struct {
	char dummy;
} shvpu_meram_t;
#define open_meram(x) (0)
#define close_meram()
#define setup_icb(a,b,c,d,e,f,g,h) (0)
#define set_meram_address(x, y, z)
#define finish_meram_write(x, y)
#define finish_meram_read(x, y)
#endif
#endif /* SHVPU_AVCDEC_MERAM_H */
