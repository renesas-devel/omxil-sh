/**
   src/vpu5/shvpu5_avcdec_bufcalc.h

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
static int maxCPB[] = {
	210,
	420,
	600,
	1200,
	2400,
	2400,
	4800,
	4800,
	12000,
	16800,
	24000,
	30000,
	75000,
	75000
};

static int minCR[] = {
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	2,
	4,
	4,
	4,
	2,
	2
};

#define MAXFPS 30
#define VPU_UNIT(x) ((((x) + 255) /256) * 256)
#define ALIGN_MASK 0xfffff000
#define ALIGN(x) (x + ~ALIGN_MASK) & ALIGN_MASK;

static inline unsigned int
inb_buf_size_calc(int level, int width, int height, int num_views) {
	int vbv_size = maxCPB[level] * 1000 / 8;
	int mss = width*height* 3 / 2 / minCR[level];
	int mb_width = ((width + 15) / 16) * 16 / 16;
	int mb_height = ((height + 15) / 16) * 16 / 16;
	int imd_fr_cnt = vbv_size < 5000000 ?
		MAXFPS + 2 : vbv_size * MAXFPS / 5000000 + 2;
	return ALIGN(vbv_size * 4 + mss * 8 +
		     VPU_UNIT(mb_width * (mb_height + 3) * 8 / 4) *
		     4 * imd_fr_cnt * num_views);

}
static inline unsigned int
ir_info_size_calc(int level, int max_slice_cnt, int num_views) {
	int vbv_size = maxCPB[level] * 1000 / 8;
	int hdr_fr_cnt = vbv_size < 5000000 ?
		MAXFPS + 2 : vbv_size * MAXFPS / 5000000 + 2;
	return  ALIGN(512 * hdr_fr_cnt * 2 +
		      VPU_UNIT( 992 * max_slice_cnt / 2) *
		      2 * hdr_fr_cnt * num_views + 3072);
}

static inline unsigned int
mv_info_size_calc(int width, int height, int ref_frame_cnt_plus1,
		int num_views) {
	int mb_width = ((width + 15) / 16) * 16 / 16;
	int mb_height = ((height + 15) / 16) * 16 / 16;
	return ALIGN(VPU_UNIT(64 * mb_width * ((mb_height + 3) / 4)) *
		     4 * (ref_frame_cnt_plus1 * num_views));
}
