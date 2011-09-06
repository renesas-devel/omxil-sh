/**
   src/vpu55/shvpu5_common_2ddmac.c

   This component implements H.264 / MPEG-4 AVC video codec.
   The H.264 / MPEG-4 AVC video encoder/decoder is implemented
   on the Renesas's VPU5HG middleware library.

   Copyright (C) 2011 Renesas Solutions Corp.

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


/* This is a temporary hack to test how the 2DDMAC would integrate with
the system.  A proper implementation is in order */
#include <tddmac/tddmac.h>
#ifdef TL_CONV_ENABLE
#include <meram/meram.h>
#endif
#include "stdint.h"
#include "shvpu5_common_log.h"
#include "shvpu5_avcdec.h" // should be moved to shvpu_common_<something>.h

#define VALIGN(x) ((x + 15) & ~15)


typedef struct {
	TDDMAC *tddmac;
	int	w;
	int	h;
	int	pitch;
	dmac_id_t ydmac;
	dmac_id_t cdmac;
	int	do_tl_conv;
#ifdef TL_CONV_ENABLE
	/* Always use MERAM with T/L conversion */
	shvpu_meram_t meram_data;
#endif
} shvpu_DMAC_t;

#ifdef TL_CONV_ENABLE
#define DMAC_YICB	19
#define DMAC_CICB	20
#endif

static shvpu_DMAC_t DMAC_data;

int DMAC_init()
{
	memset(&DMAC_data, 0, sizeof(DMAC_data));

	DMAC_data.tddmac = tddmac_open();
	if (!DMAC_data.tddmac)
		return -1;
	return 0;
}

void DMAC_deinit()
{
	if (DMAC_data.do_tl_conv)
		close_meram(&DMAC_data.meram_data);
	tddmac_close(DMAC_data.tddmac);
}

int DMAC_setup_buffers(int w, int h, int do_tl_conv)
{
	int pitch;
	struct tddmac_buffer ysrc, ydst;
	struct tddmac_buffer csrc, cdst;

	DMAC_data.w = w;
	DMAC_data.h = h;
	DMAC_data.do_tl_conv = do_tl_conv;

	if (do_tl_conv) {
		pitch = (w - 1);
		pitch |= pitch >> 1;
		pitch |= pitch >> 2;
		pitch |= pitch >> 4;
		pitch ++;

		open_meram(&DMAC_data.meram_data);
		setup_icb(&DMAC_data.meram_data,
			&DMAC_data.meram_data.decY_icb,
			pitch, VALIGN(h), 128, 0xD, 0, DMAC_YICB);
		setup_icb(&DMAC_data.meram_data,
			&DMAC_data.meram_data.decC_icb,
			pitch, VALIGN(h) / 2, 64, 0xC, 0, DMAC_CICB);
		DMAC_data.pitch = pitch;
	if (pitch < 1024)
		pitch = 1024;
	} else {
		pitch = w;
		DMAC_data.pitch = pitch;
	}

	logd("pitch = %d, w = %d", pitch, w);

	ysrc.w = ydst.w = w;
	ysrc.h = ydst.h = h;
	ysrc.pitch = pitch;
	ydst.pitch = w;
	ysrc.fmt = ydst.fmt = TDDMAC_Y;

	csrc.w = cdst.w = w;
	csrc.h = cdst.h = h/2;
	csrc.pitch = pitch;
	cdst.pitch = w;
	csrc.fmt = cdst.fmt = TDDMAC_CbCr420;

	DMAC_data.ydmac = tddmac_setup(DMAC_data.tddmac, &ysrc, &ydst);
	DMAC_data.cdmac = tddmac_setup(DMAC_data.tddmac, &csrc, &cdst);

	return 0;
}

int DMAC_copy_buffer(unsigned long to, unsigned long from)
{
	unsigned long copy_fromY;
	unsigned long copy_fromC;
	int do_tl_conv;

	int h, w, pitch;
	int ret;

	h = DMAC_data.h;
	w = DMAC_data.w;
	pitch = DMAC_data.pitch;

	do_tl_conv = DMAC_data.do_tl_conv;

	if (do_tl_conv) {
		set_meram_address(&DMAC_data.meram_data,
			DMAC_data.meram_data.decY_icb, from);
		set_meram_address(&DMAC_data.meram_data,
			DMAC_data.meram_data.decC_icb,
			from + VALIGN(h) * pitch);

		copy_fromY = meram_get_icb_address(
				DMAC_data.meram_data.meram,
				DMAC_data.meram_data.decY_icb, 0);
		copy_fromC = meram_get_icb_address(
				DMAC_data.meram_data.meram,
				DMAC_data.meram_data.decC_icb, 0);
	} else {
		copy_fromY = from;
		copy_fromC = from + VALIGN(h) * pitch;
	}

	tddmac_start(DMAC_data.tddmac, DMAC_data.ydmac, copy_fromY, to);
	tddmac_start(DMAC_data.tddmac, DMAC_data.cdmac, copy_fromC, to + h * w);

	tddmac_wait(DMAC_data.tddmac, DMAC_data.ydmac);
	tddmac_wait(DMAC_data.tddmac, DMAC_data.cdmac);

	if (do_tl_conv) {
		finish_meram_read(&DMAC_data.meram_data,
			DMAC_data.meram_data.decY_icb);
		finish_meram_read(&DMAC_data.meram_data,
			DMAC_data.meram_data.decC_icb);
	}

	return ret;
}
