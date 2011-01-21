#ifdef MERAM_ENABLE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include "shvpu5_avcdec_meram.h"
static void *meram_base = NULL;
static int mem_fd;
write_meram_icb(void *base, int ind, int off, unsigned long val)
{
	*(unsigned long *)(base + 0x400 + ((ind) * 0x20) + off) = val;
}

read_meram_icb(void *base, int ind, int off, unsigned long *dest)
{
	*dest = *(unsigned long *)(base + 0x400 + ((ind) * 0x20) + off);
}
write_meram_reg(void *base, int off, unsigned long val)
{
	*(unsigned long *)(base + off) = val;
}

read_meram_reg(void *base, int off, unsigned long *dest)
{
	*dest = *(unsigned long *)(base + off);
}
int
meram_open_mem()
{
        mem_fd = open ("/dev/mem", O_RDWR);
        if (mem_fd < 0) {
                perror("open");
                exit(-1);
        }

        meram_base = mmap(NULL, MERAM_REG_SIZE, PROT_READ | PROT_WRITE,
                MAP_SHARED, mem_fd, MERAM_REG_BASE);
        if (meram_base == MAP_FAILED) {
                perror("mmap");
		return -1;
	}
}
void
meram_close_mem()
{

	munmap(meram_base, MERAM_REG_SIZE);
	close(mem_fd);
}
unsigned long
setup_icb(unsigned long address,
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
	int md, res;
	int memblk;

	md = rdnwr == 0 ? 1 : 2;
        res = rdnwr == 0 ? 2 : 1;
	if (index == 21)
		memblk = 512;
	else
		memblk = ((index - 20 )/ 2) * 512 + (index - 21 ) / 2 *
			 128 + 512;
	write_meram_reg(meram_base, 0x4, 0x20000000);
	write_meram_icb(meram_base, index, MExxCTL, (block_lines << 28) |
		(memblk  << 16) | 0x708 | md);

	write_meram_icb(meram_base, index, MExxSIZE, (lines-1) << 16 |
		(pitch -1));

	write_meram_icb(meram_base, index, MExxMNCF, (res_lines-1 << 16) |
		(res << 28) | (0 << 15 ));

	write_meram_icb(meram_base, index, MExxSARA, address);
	write_meram_icb(meram_base, index, MExxBSIZE, pitch | 0x90000000);

	return MERAM_START(index, 0);
}
void
meram_set_address(unsigned long address, int index)
{
	write_meram_icb(meram_base, index, MExxSARA, address);
}
void
meram_write_done(int index) {
	unsigned long tmp;
	read_meram_icb(meram_base, index, MExxCTL, &tmp);
	write_meram_icb(meram_base, index, MExxCTL, tmp | 0x20);
}

void
meram_read_done(int index) {
	unsigned long tmp;
	read_meram_icb(meram_base, index, MExxCTL, &tmp);
	write_meram_icb(meram_base, index, MExxCTL, tmp | 0x10);
}
#endif
