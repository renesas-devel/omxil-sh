/**
   src/vpu5/ipmmuihelper.c

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

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#define MAXBUFS 4

#define MAPPATH "/sys/kernel/ipmmui/vpu5_buf%d/%s"
#define MAPFILE "map"
#define SIZEFILE "mapsize"
#define ADDRFILE "mapaddr"

#define FILENAME_LEN 50

static int bufcount;
int ipmmui_buffer_init()
{
#if 0
	static FILE *mapfile;
	char filename[FILENAME_LEN];
	int i;
	for (i = 0; i < MAXBUFS; i++) {
		snprintf(filename, sizeof(filename), MAPPATH, i+1, MAPFILE);
		mapfile = fopen(filename, "w");
		if (!mapfile) {
			loge("Cannot open %s for write\n", filename);
			return -1;
		}
		if (fprintf(mapfile, "0,0") <= 0) {
			loge("Cannot write to %s\n", filename);
			fclose(mapfile);
			mapfile = NULL;
			return -1;
		}
		fclose(mapfile);
	}
	bufcount = 0;
#endif
	return 0;

}

int ipmmui_buffer_map_vaddr(void *vaddr, unsigned int size,
		unsigned long *paddr)
{
#if 0
	static FILE *mapfile;
	unsigned long mapaddr;
	unsigned int mapsize;
	char filename[FILENAME_LEN];
	char *temp;

	if (!vaddr || !paddr)
		return -EINVAL;
	if (bufcount >= MAXBUFS)
		return -ENOMEM;

	bufcount++;

	/* make the mapping */
	snprintf(filename, sizeof(filename), MAPPATH, bufcount, MAPFILE);
	mapfile = fopen(filename, "w");
	if (!mapfile) {
		loge("Cannot open %s for write\n", filename);
		goto ipmmui_error;
	}
	loge ("writing \"%p,%d\" to %s", vaddr, size, filename);
	if (fprintf(mapfile, "%p,%d", vaddr, size) <= 0) {
		loge("Cannot write to %s\n", filename);
		goto ipmmui_error;
	}
	fclose(mapfile);

	/* check the mapping size */
	snprintf(filename, sizeof(filename), MAPPATH, bufcount, SIZEFILE);
	mapfile = fopen(filename, "r");
	if (!mapfile || getc(mapfile) != '0' || getc(mapfile) != 'x' ||
	    fscanf(mapfile, "%x", &mapsize) != 1 || mapsize < size) {
		loge("Cannot read or bad value from %s\n (%d)", filename,
			mapsize);
		goto ipmmui_error;
	}
	fclose(mapfile);

	/* get the mapped address */
	snprintf(filename, sizeof(filename), MAPPATH, bufcount, ADDRFILE);
	mapfile = fopen(filename, "r");
	if (!mapfile || getc(mapfile) != '0' || getc(mapfile) != 'x' ||
	    fscanf(mapfile, "%lx", &mapaddr) != 1) {
		loge("Cannot read or bad value from %s\n", filename);
		goto ipmmui_error;
	}
	fclose(mapfile);
	loge ("Got %x %d from %s", mapaddr, mapsize, filename);

	*paddr = mapaddr;
	return 0;

ipmmui_error:
	if (mapfile)
		fclose(mapfile);
	return -1;
#else
	FILE *pagemap;
	int pg_size = sysconf(_SC_PAGESIZE);
	unsigned long addr = (unsigned long) vaddr;
	uint64_t val;

	if (!vaddr || !paddr)
		return -EINVAL;

	pagemap = fopen("/proc/self/pagemap", "r");

	addr = addr / pg_size;
	size = (size + pg_size - 1) / pg_size;
	fseek(pagemap, addr * 8, SEEK_SET);
	if (fread(&val, sizeof (uint64_t), 1, pagemap) != 1)
		return -1;

	if (!(val & (1ULL << 63)))
		return -1;

	*paddr = (val & ((1ULL << 54) - 1)) * pg_size;
	fclose(pagemap);
	return 0;
#endif

}

int ipmmui_buffer_unmap_vaddr(void *vaddr)
{
	/* Can we unmap individual buffers? */
	return 0;
}

void ipmmui_buffer_deinit()
{
#if 0
	static FILE *mapfile;
	char filename[FILENAME_LEN];
	int i;
	for (i = 0; i < MAXBUFS; i++) {
		snprintf(filename, sizeof(filename), MAPPATH, i+1, MAPFILE);
		mapfile = fopen(filename, "w");
		if (!mapfile) {
			loge("Cannot open %s for write\n", filename);
			fclose(mapfile);
			continue;
		}
		if (fprintf(mapfile, "0,0") <= 0) {
			loge("Cannot write to %s\n", filename);
		}
		fclose(mapfile);
	}
	bufcount = 0;
#endif
}
