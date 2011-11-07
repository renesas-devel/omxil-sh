#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include "shvpu5_common_log.h"

#include <meram/ipmmui.h>
#include "shvpu5_common_ipmmu.h"

#define TB 5 /*log2 (block width) minimum value = 4*/
#define VB 5 /*log2 (block height)*/

int
init_ipmmu(shvpu_ipmmui_t *ipmmui_data,
	   int pmb,
	   unsigned long phys_base,
	   int log2_stride,
	   int *align) {
	IPMMUI_REG *reg;
	int ipmmui_size;
	int ipmmui_size_code;
	IPMMUI *ipmmui;
	PMB *pmb0;

	ipmmui = ipmmui_data->ipmmui = ipmmui_open();
	if (!ipmmui)
		return -1;

	if (log2_stride < TB)
		return -1;

	if (ipmmui_get_vaddr(ipmmui, "vpu", &ipmmui_data->ipmmui_vaddr,
		&ipmmui_size) < 0)
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
	ipmmui_data->ipmmui_mask = ~((ipmmui_size << 20) - 1);

	pmb0 = ipmmui_data->pmb = ipmmui_lock_pmb(ipmmui, 0);
	ipmmui_write_pmb(ipmmui, pmb0, IMPMBA, ipmmui_data->ipmmui_vaddr
		| (1 << 8));
	ipmmui_write_pmb(ipmmui, pmb0, IMPMBD,
                (phys_base & ipmmui_data->ipmmui_mask) |
                (1 << 8) | ipmmui_size_code | ((VB - 1) << 20) |
                ((log2_stride - TB - 1) << 16) | ((TB - 4) << 12)
                | (1 << 9));

	reg = ipmmui_lock_reg(ipmmui);
	ipmmui_write_reg(ipmmui, reg, IMCTR2, 1);
	ipmmui_unlock_reg(ipmmui, reg);

	*align = log2_stride + VB;
	return 0;
}
void
deinit_ipmmu(shvpu_ipmmui_t *ipmmui_data) {
	IPMMUI_REG *reg;
	PMB *pmb0;
	if (!ipmmui_data->ipmmui)
		return;

	if (ipmmui_data->pmb) {
		pmb0 = ipmmui_data->pmb;
		ipmmui_write_pmb(ipmmui_data->ipmmui, pmb0, IMPMBA, 0);
		ipmmui_write_pmb(ipmmui_data->ipmmui, pmb0, IMPMBD, 0);
		ipmmui_unlock_pmb(ipmmui_data->ipmmui, pmb0);
	}

	reg = ipmmui_lock_reg(ipmmui_data->ipmmui);
	ipmmui_write_reg(ipmmui_data->ipmmui, reg, IMCTR2, 0);
	ipmmui_unlock_reg(ipmmui_data->ipmmui, reg);

	ipmmui_close(ipmmui_data->ipmmui);

}
unsigned long
phys_to_ipmmui(shvpu_ipmmui_t *ipmmui_data, unsigned long address) {
	return (address & ~ipmmui_data->ipmmui_mask) |
		ipmmui_data->ipmmui_vaddr;
}

unsigned long
ipmmui_to_phys(shvpu_ipmmui_t *ipmmui_data, unsigned long ipmmu,
	unsigned long phys_base) {
	return (ipmmu & ~ipmmui_data->ipmmui_mask) |
		(phys_base & ipmmui_data->ipmmui_mask);
}
