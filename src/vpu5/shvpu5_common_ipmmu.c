#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>

#define IPMMUI_BASE 0xFE951000
#define IPMMUI_LEN 0x100
#define IMCTR1 0x0
#define IMCTR2 0x4
#define IMPMBA0 0x90
#define IMPMBD0 0xD0
#define TB 5 /*log2 (block width) minimum value = 4*/
#define VB 5 /*log2 (block height)*/
#define IPMMUI_PAGE_SIZE 0x10 /*map all VPU accesses through one 64MB page*/
#define IPMMUI_PAGE_MASK 0xFC000000 /*mask for 64MB page*/
#define IMPPUI_ACCESS_BASE 0x80000000
/*meram mapping data necessary set MERAM address mode*/
#define MERAM_BASE 0xE8000000
#define MERAM_LEN 0x8
#define MEVCR1 0x4

static int mem_fd;
static void *ipmmu_base = NULL;

static void
write_ipmmu_reg(void *base, int off, unsigned long val)
{
        *(unsigned long *)(base + off) = val;
}

static void
read_ipmmu_reg(void *base, int off, unsigned long *dest)
{
                *dest = *(unsigned long *)(base + off);
}
int
init_ipmmu(int pmb, unsigned long phys_base, int log2_stride, int *align) {
	void *meram_base;
	if (!ipmmu_base) {
		mem_fd = open ("/dev/mem", O_RDWR);
		if (mem_fd < 0) {
			perror("open");
			return -1;
		}

		ipmmu_base = mmap(NULL, IPMMUI_LEN, PROT_READ | PROT_WRITE,
			MAP_SHARED, mem_fd, IPMMUI_BASE);
		if (ipmmu_base == MAP_FAILED) {
			perror("mmap");
			close(mem_fd);
			return -1;
		}
		/* Set the MERAM address range to 0xC0000000 - 0xDFFFFFFF
		   so that it doesn't overlay the IPMMUI address range*/

		meram_base = mmap(NULL, MERAM_LEN, PROT_READ | PROT_WRITE,
			MAP_SHARED, mem_fd, MERAM_BASE);

		if (meram_base == MAP_FAILED) {
			loge("%s: MERAM mmap error", __FUNCTION__);
			munmap(ipmmu_base, IPMMUI_LEN);
			close(mem_fd);
			return -1;
		}
		write_ipmmu_reg(meram_base, MEVCR1, (1 << 29));
		munmap(meram_base, MERAM_LEN);
	}

	if (log2_stride < TB)
		return -1;

	write_ipmmu_reg(ipmmu_base, IMPMBA0, IMPPUI_ACCESS_BASE | (1 << 8));
	write_ipmmu_reg(ipmmu_base, IMPMBD0,
                (phys_base & IPMMUI_PAGE_MASK) |
                (1 << 8) | IPMMUI_PAGE_SIZE | ((VB - 1) << 20) |
                ((log2_stride - TB - 1) << 16) | ((TB - 4) << 12)
                | (1 << 9));
	write_ipmmu_reg(ipmmu_base, IMCTR1, 2);
	write_ipmmu_reg(ipmmu_base, IMCTR2, 1);

	*align = log2_stride + VB;
	return 0;
}
void
deinit_ipmmu() {
	if (!ipmmu_base)
		return;

	write_ipmmu_reg(ipmmu_base, IMPMBA0, 0);
	write_ipmmu_reg(ipmmu_base, IMPMBD0, 0);
	write_ipmmu_reg(ipmmu_base, IMCTR2, 0);
	write_ipmmu_reg(ipmmu_base, IMCTR1, 2);

        munmap(ipmmu_base, IPMMUI_LEN);
        close(mem_fd);
}
unsigned long
phys_to_ipmmui(unsigned long address) {
	return (address & ~IPMMUI_PAGE_MASK) | IMPPUI_ACCESS_BASE;
}

unsigned long
ipmmui_to_phys(unsigned long ipmmu, unsigned long phys_base) {
	return (ipmmu & ~IPMMUI_PAGE_MASK) | (phys_base & IPMMUI_PAGE_MASK);
}
