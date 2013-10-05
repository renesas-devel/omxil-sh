/**
   src/vpu5/shvpu5_common_log.h

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
#ifndef __SHVPU5_COMMON_LOG_H_
#define __SHVPU5_COMMON_LOG_H_

#ifdef HAVE_ANDROID_OS
#include <utils/Log.h>
#endif

int _shvpu5_logout(int level, const char *format, ...);

#ifdef HAVE_ANDROID_OS
#define logd(_f, ...)	 {} /* _shvpu5_logout(ANDROID_LOG_DEBUG, (_f), ##__VA_ARGS__) */
#define logi(_f, ...)	_shvpu5_logout(ANDROID_LOG_INFO, (_f), ##__VA_ARGS__)
#define loge(_f, ...)	_shvpu5_logout(ANDROID_LOG_ERROR, (_f), ##__VA_ARGS__)
#else /* HAVE_ANDROID_OS */
#define logd(_l, ...)	{} /* _shvpu5_logout(0, (_f), ##__VA_ARGS__) */
#define logi(_f, ...)	_shvpu5_logout(0, (_f), ##__VA_ARGS__)
#define loge(_f, ...)	_shvpu5_logout(0, (_f), ##__VA_ARGS__)
#endif /* HAVE_ANDROID_OS */

#endif /* __SHVPU5_COMMON_LOG_H_ */
