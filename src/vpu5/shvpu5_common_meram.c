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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include "shvpu5_common_meram.h"
#include "shvpu5_decode.h"

#define MERAM_REG_BASE 0xE8000000
#define MERAM_REG_SIZE 0x200000
#define MERAM_ICB0BASE 0x400
#define MERAM_ICB28BASE 0x780
#define MExxCTL 0x0
#define MExxSIZE 0x4
#define MExxMNCF 0x8
#define MExxSARA 0x10
#define MExxSARB 0x14
#define MExxBSIZE 0x18
#define MSAR_OFF 0x3C0

#define MEACTS 0x10
#define MEQSEL1 0x40
#define MEQSEL2 0x44

#define MERAM_START(ind, ab) (0xC0000000 | ((ab & 0x1) << 23) | \
        ((ind & 0x1F) << 24))


static void *meram_base = NULL;
static int mem_fd;
static void
release_icb(shvpu_meram_t *mdata, ICB *icb) {
	if (icb && mdata && mdata->meram) {
		meram_free_icb_memory(mdata->meram, icb);
		meram_unlock_icb(mdata->meram, icb);
	}
}
int
open_meram(shvpu_meram_t *mdata)
{
	MERAM_REG *reg;
	unsigned long tmp;

	if (mdata->meram)
		return 0;

	mdata->meram = meram_open();
	if (mdata->meram == NULL)
		return -1;
	reg = meram_lock_reg(mdata->meram);
	meram_read_reg(mdata->meram, reg, MEVCR1, &tmp);
	meram_write_reg(mdata->meram, reg, MEVCR1, tmp | 0x20000000);
	meram_unlock_reg(mdata->meram, reg);
	return 0;
}
void
close_meram(shvpu_meram_t *mdata)
{
	if (!mdata || !mdata->meram)
		return;
	if (mdata->decY_icb)
		release_icb(mdata, mdata->decY_icb);
	if (mdata->decC_icb)
		release_icb(mdata, mdata->decC_icb);
	meram_close(mdata->meram);
	memset(mdata, 0, sizeof (shvpu_meram_t));
}

unsigned long
setup_icb(shvpu_meram_t *mdata,
	  ICB **icb,
	  unsigned long pitch,
	  unsigned long lines,
	  int res_lines,
	  int block_lines,
	  int rdnwr,
	  int index)
{
	unsigned int total_len = pitch * lines;
	unsigned int xk_lines;
	unsigned int line_len;
	unsigned long tmp;
	int pitch_2n;
	int md, res;
	int memblk;

	MERAM *meram = mdata->meram;

	md = rdnwr == 0 ? 1 : 2;
	res = rdnwr == 0 ? 2 : 0x20;

	if (pitch <= 1024)
		pitch_2n = 1;
	else if (pitch <= 2048)
		pitch_2n = 2;
	else /* Only support up to 4k pixel width frames */
		pitch_2n = 4;


	if ((*icb = meram_lock_icb(meram, index)) == NULL)
		return -1;

	memblk = meram_alloc_icb_memory(meram, *icb, pitch_2n * res_lines);

	meram_write_icb(meram, *icb, MExxCTL, (block_lines << 28) |
		(memblk  << 16) | 0x708 | md);

	meram_write_icb(meram, *icb, MExxSIZE, (lines-1) << 16 |
		(pitch -1));

	meram_write_icb(meram, *icb, MExxMNCF, ((res_lines - 1) << 16) |
		(res << 24) | (0 << 15));

	meram_write_icb(meram, *icb, MExxBSIZE, pitch | 0x90000000);

	return 0;
}
void
set_meram_address(shvpu_meram_t *mdata, ICB *icb, unsigned long address)
{
	if (icb && mdata)
		meram_write_icb(mdata->meram, icb, MExxSARA, address);
}
void
finish_meram_write(shvpu_meram_t *mdata, ICB *icb) {
	unsigned long tmp;
	if (icb && mdata && mdata->meram) {
		meram_read_icb(mdata->meram, icb, MExxCTL, &tmp);
		meram_write_icb(mdata->meram, icb, MExxCTL, tmp | 0x20);
	}
}

void
finish_meram_read(shvpu_meram_t *mdata, ICB *icb) {
	unsigned long tmp;
	if (icb && mdata && mdata->meram) {
		meram_read_icb(mdata->meram, icb, MExxCTL, &tmp);
		meram_write_icb(mdata->meram, icb, MExxCTL, tmp | 0x10);
	}
}
