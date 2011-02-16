/**
   src/vpu5/shvpu5_common_log.c

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

#include <stdio.h>
#include <stdarg.h>
#include "shvpu5_common_log.h"

#ifdef HAVE_ANDROID_OS
#include <utils/Log.h>
#endif
int
logd(const char *format, ...)
{
#if 0
	int ret = 0;
	va_list ap;

	va_start(ap, format);
#ifdef HAVE_ANDROID_OS
        LOG_PRI_VA(ANDROID_LOG_DEBUG, LOG_TAG, format, ap);
#else
	ret = vfprintf(stderr, format, ap);
#endif
	va_end(ap);

	return ret;
#else
	return 0;
#endif
}

int
loge(const char* format, ...)
{
#if 1
	int ret = 0;
	va_list ap;

	va_start(ap, format);
#ifdef HAVE_ANDROID_OS
        LOG_PRI_VA(ANDROID_LOG_ERROR, LOG_TAG, format, ap);
#else
	ret = vfprintf(stderr, format, ap);
#endif
	va_end(ap);

	return ret;
#else
	return 0;
#endif
}
