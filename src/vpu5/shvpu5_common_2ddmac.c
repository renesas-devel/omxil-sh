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
#include "uio.h"
#include "shvpu5_common_log.h"
#include "shvpu5_avcdec.h" // should be moved to shvpu_common_<something>.h

#define CHSTCLR		0x10
#define CHnCTRL		0x20
#define CTRL_DMAEN	(1 << 0)
#define CTRL_TE		(1 << 4)
#define CHnSWAP		0x30
#define SWAP_OLS	(1 << 6)
#define SWAP_OWS	(1 << 5)
#define SWAP_OBS	(1 << 4)
#define SWAP_ILS	(1 << 2)
#define SWAP_IWS	(1 << 1)
#define SWAP_IBS	(1 << 0)
#define CHnSAR		0x80
#define CHnDAR		0x84
#define CHnDPXL		0x88
#define CHnSFMT		0x8C
#define HBYTES_OFFSET	16
#define HBYTES_MASK 	(0xFFFF << HBYTES_OFFSET)
#define FMT_RGB		(0 << 5)
#define FMT_Y		(1 << 5)
#define FMT_CbCr_12	(2 << 5)
#define FMT_CbCr_16	(3 << 5)
#define FMT_MASK	(3 << 5)
#define CHnDFMT		0x90
#define CHnSARE		0x94
#define CHnDARE		0x98
#define CHnDPXLE	0x9C

#define VALIGN(x) ((x + 15) & ~15)

#define DMAC_OFFSET(dmac, offset)	\
	((offset == CHnCTRL || offset == CHnSWAP) ? ((dmac & 4) << 6) | \
		((dmac << 2) & 0xf) | offset : offset + (dmac << 5))

static struct uio dmac_uio;

typedef struct {
	int	w;
	int	h;
	int	pitch;
	int	enabled;
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

static void dmac_write32(struct uio_map *ump, u32 val, int dmac, int offset)
{
	uio_write32(ump, val, DMAC_OFFSET(dmac, offset));
}

static u32 dmac_read32(struct uio_map *ump, int dmac, int offset)
{
	return uio_read32(ump, DMAC_OFFSET(dmac, offset));
}

static void DMAC_clear_all_done_bits(struct uio_map *ump)
{
	uio_write32(ump, uio_read32(ump, CHSTCLR), CHSTCLR);
}

int DMAC_init()
{
	td_uio_init(&dmac_uio, "2DDMAC");

	if (!dmac_uio.mmio.iomem) {
		loge("2D DMAC uio not initialized\n");
		return -1;
	}

	memset(&DMAC_data, 0, sizeof(DMAC_data));

	/* Y buffer */
	dmac_write32(&dmac_uio.mmio, 0, 0, CHnCTRL);
	dmac_write32(&dmac_uio.mmio, SWAP_OLS | SWAP_OWS | SWAP_OBS |
		SWAP_ILS | SWAP_IWS | SWAP_IBS, 0, CHnSWAP);
	dmac_write32(&dmac_uio.mmio, FMT_Y, 0, CHnSFMT);
	dmac_write32(&dmac_uio.mmio, FMT_Y, 0, CHnDFMT);

	/* CbCr (4:2:0) buffer */
	dmac_write32(&dmac_uio.mmio, 0, 1, CHnCTRL);
	dmac_write32(&dmac_uio.mmio, SWAP_OLS | SWAP_OWS | SWAP_OBS |
		SWAP_ILS | SWAP_IWS | SWAP_IBS, 1, CHnSWAP);
	dmac_write32(&dmac_uio.mmio, FMT_CbCr_12, 1, CHnSFMT);
	dmac_write32(&dmac_uio.mmio, FMT_CbCr_12, 1, CHnDFMT);

	return 0;
}

void DMAC_deinit()
{
#ifdef TL_CONV_ENABLE
	close_meram(&DMAC_data.meram_data);
#endif
	td_uio_deinit(&dmac_uio);
}

static int DMAC_copy(unsigned long to,
	unsigned long fromY,
	unsigned long fromC)
{
	int w, h, pitch;
	if (!dmac_uio.mmio.iomem) {
		loge("2D DMAC uio not initialized\n");
		return -1;
	}

	if (!DMAC_data.enabled) {
		loge("2D DMAC buffers not initialized\n");
		return -1;
	}

	w = DMAC_data.w;
	h = DMAC_data.h;
	pitch = DMAC_data.pitch;

	/* Y buffer */
	dmac_write32(&dmac_uio.mmio, fromY, 0, CHnSAR);
	dmac_write32(&dmac_uio.mmio, to, 0, CHnDAR);

	/* CbCr (4:2:0) buffer */
	dmac_write32(&dmac_uio.mmio, fromC, 1, CHnSAR);
	dmac_write32(&dmac_uio.mmio, to + h * w, 1, CHnDAR);

	/* Start transfers */
	dmac_write32(&dmac_uio.mmio, dmac_read32(&dmac_uio.mmio, 0, CHnCTRL) |
		 CTRL_DMAEN, 0, CHnCTRL);
	dmac_write32(&dmac_uio.mmio, dmac_read32(&dmac_uio.mmio, 1, CHnCTRL) |
		 CTRL_DMAEN, 1, CHnCTRL);

	/* Wait for done */
	/* Busy wait for now */

	while (!(dmac_read32(&dmac_uio.mmio, 0, CHnCTRL) & CTRL_TE))
		;


	while (!(dmac_read32(&dmac_uio.mmio, 1, CHnCTRL) & CTRL_TE))
		;

	DMAC_clear_all_done_bits(&dmac_uio.mmio);

	return 0;
}

int DMAC_setup_buffers(int w, int h)
{
	int pitch;
	u32 temp;
	if (!dmac_uio.mmio.iomem) {
		loge("2D DMAC uio not initialized\n");
		return -1;
	}

	DMAC_data.w = w;
	DMAC_data.h = h;

#ifdef TL_CONV_ENABLE
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
#else
	pitch = w;
	DMAC_data.pitch = pitch;
#endif

	/* Y buffer */
	temp = dmac_read32(&dmac_uio.mmio, 0, CHnSFMT);
	dmac_write32(&dmac_uio.mmio, (pitch << HBYTES_OFFSET) |
		(temp & ~HBYTES_MASK), 0, CHnSFMT);
	temp = dmac_read32(&dmac_uio.mmio, 0, CHnDFMT);
	dmac_write32(&dmac_uio.mmio, (w << HBYTES_OFFSET) |
		(temp & ~HBYTES_MASK), 0, CHnDFMT);
	dmac_write32(&dmac_uio.mmio, (w << 16) | h, 0, CHnDPXL);

	/* CbCr (4:2:0) buffer */
	temp = dmac_read32(&dmac_uio.mmio, 1, CHnSFMT);
	dmac_write32(&dmac_uio.mmio, (pitch << HBYTES_OFFSET) |
		(temp & ~HBYTES_MASK), 1, CHnSFMT);
	temp = dmac_read32(&dmac_uio.mmio, 1, CHnDFMT);
	dmac_write32(&dmac_uio.mmio, (w << HBYTES_OFFSET) |
		(temp & ~HBYTES_MASK), 1, CHnDFMT);
	dmac_write32(&dmac_uio.mmio, ((w / 2) << 16) | (h / 2), 1, CHnDPXL);

	DMAC_data.enabled = 1;
	return 0;
}

int DMAC_copy_buffer(unsigned long to, unsigned long from)
{
	unsigned long copy_fromY;
	unsigned long copy_fromC;

	int h, pitch;
	int ret;

	h = DMAC_data.h;
	pitch = DMAC_data.pitch;

#ifdef TL_CONV_ENABLE
	set_meram_address(&DMAC_data.meram_data,
		DMAC_data.meram_data.decY_icb, from);
	set_meram_address(&DMAC_data.meram_data,
		DMAC_data.meram_data.decC_icb, from + VALIGN(h) * pitch);

	copy_fromY = meram_get_icb_address(
			DMAC_data.meram_data.meram,
			DMAC_data.meram_data.decY_icb, 0);
	copy_fromC = meram_get_icb_address(
			DMAC_data.meram_data.meram,
			DMAC_data.meram_data.decC_icb, 0);
#else
	copy_fromY = from;
	copy_fromC = from + VALIGN(h) * pitch;
#endif
	ret = DMAC_copy(to, copy_fromY, copy_fromC);
	if (ret)
		return ret;

#ifdef TL_CONV_ENABLE
	finish_meram_read(&DMAC_data.meram_data,
		DMAC_data.meram_data.decY_icb);
	finish_meram_read(&DMAC_data.meram_data,
		DMAC_data.meram_data.decC_icb);
#endif

	return ret;
}
