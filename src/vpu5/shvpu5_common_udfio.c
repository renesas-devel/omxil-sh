#include "shvpu5_common_uio.h"
long
mciph_uf_mem_read(unsigned long src_addr,
		  unsigned long dst_addr, long count)
{
	return vpu5_mem_read(src_addr, dst_addr, count);
}

long
mciph_uf_mem_write(unsigned long src_addr,
		   unsigned long dst_addr, long count)
{
	return vpu5_mem_write(src_addr, dst_addr, count);
}
long
mciph_uf_reg_table_read(unsigned long src_addr,
			unsigned long reg_table, long size)
{
	return vpu5_mmio_read(src_addr, reg_table, size);
}
long
mciph_uf_reg_table_write(unsigned long dst_addr,
			 unsigned long reg_table, long size)
{
	return vpu5_mmio_write(dst_addr, reg_table, size);
}
void
mciph_uf_set_imask(long mask_enable, long now_interrupt)
{
	vpu5_set_imask(mask_enable, now_interrupt);
}
