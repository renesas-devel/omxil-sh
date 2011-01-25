#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include "shvpu5_common_log.h"

#include <meram/ipmmui.h>

#define TB 5 /*log2 (block width) minimum value = 4*/
#define VB 5 /*log2 (block height)*/

IPMMUI *ipmmui;
PMB *pmb0;
unsigned long ipmmui_vaddr;
unsigned long ipmmui_mask;

int
init_ipmmu(int pmb, unsigned long phys_base, int log2_stride, int *align) {
	ipmmui = ipmmui_open();
	IPMMUI_REG *reg;
	int ipmmui_size;
	int ipmmui_size_code;

	if (!ipmmui)
		return -1;

	if (log2_stride < TB)
		return -1;

	if (ipmmui_get_vaddr(ipmmui, "vpu", &ipmmui_vaddr, &ipmmui_size) < 0)
		return -1;
	switch (ipmmui_size) {
		case 16:
			ipmmui_size_code = 0x0;
			break;
		case 64:
			ipmmui_size_code = 0x10;
			break;
		case 128:
			ipmmui_size_code = 0x80;
			break;
		case 256:
			ipmmui_size_code = 0x90;
			break;
		default:
			return -1;
	}
	ipmmui_mask = ~((ipmmui_size << 20) - 1);

	pmb0 = ipmmui_lock_pmb(ipmmui, 0);
	ipmmui_write_pmb(ipmmui, pmb0, IMPMBA, ipmmui_vaddr | (1 << 8));
	ipmmui_write_pmb(ipmmui, pmb0, IMPMBD,
                (phys_base & ipmmui_mask) |
                (1 << 8) | ipmmui_size_code | ((VB - 1) << 20) |
                ((log2_stride - TB - 1) << 16) | ((TB - 4) << 12)
                | (1 << 9));

	reg = ipmmui_lock_reg(ipmmui);
	ipmmui_write_reg(ipmmui, reg, IMCTR1, 2);
	ipmmui_write_reg(ipmmui, reg, IMCTR2, 1);
	ipmmui_unlock_reg(ipmmui, reg);

	*align = log2_stride + VB;
	return 0;
}
void
deinit_ipmmu() {
	IPMMUI_REG *reg;
	if (!ipmmui)
		return;

	ipmmui_write_pmb(ipmmui, pmb0, IMPMBA, 0);
	ipmmui_write_pmb(ipmmui, pmb0, IMPMBD, 0);
	ipmmui_unlock_pmb(ipmmui, pmb0);

	reg = ipmmui_lock_reg(ipmmui);
	ipmmui_write_reg(ipmmui, reg, IMCTR2, 0);
	ipmmui_write_reg(ipmmui, reg, IMCTR1, 2);
	ipmmui_unlock_reg(ipmmui, reg);

	ipmmui_close(ipmmui);

}
unsigned long
phys_to_ipmmui(unsigned long address) {
	return (address & ~ipmmui_mask) | ipmmui_vaddr;
}

unsigned long
ipmmui_to_phys(unsigned long ipmmu, unsigned long phys_base) {
	return (ipmmu & ~ipmmui_mask) | (phys_base & ipmmui_mask);
}
