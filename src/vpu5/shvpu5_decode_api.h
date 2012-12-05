/**
   src/vpu5/shvpu5_decode.c

   This component implements H.264 / MPEG-4 AVC video codec.
   The H.264 / MPEG-4 AVC video encoder/decoder is implemented
   on the Renesas's VPU5HG middleware library.

   Copyright (C) 2012 IGEL Co., Ltd
   Copyright (C) 2012 Renesas Solutions Corp.

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
struct codec_init_ops {
	void (*init_intrinsic_array) (void ***intrinsic);
	void (*deinit_intrinsic_array) (void **intrinsic);
	void (*calc_buf_sizes) (int num_views, shvpu_decode_PrivateType *priv,
					       shvpu_avcdec_codec_t *pCodec,
					       long *imd_size,
					       long *ir_size,
					       long *mv_size);
					       
	long (*intrinsic_func_callback) (MCVDEC_CONTEXT_T *context,
						long data_type,
						void *api_data,
						long data_id,
						long status);
	void (*deinit_codec) (shvpu_codec_params_t *vpu_codec_params);
};

