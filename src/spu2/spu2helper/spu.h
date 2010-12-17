void spu_get_workarea (uint32_t *addr, uint32_t *size, void **io);
long spu_enter_critical_section (void);
long spu_leave_critical_section (void);
long spu_set_event (void);
long spu_wait_event (unsigned long timeout);
void spu_read (unsigned long *addr, unsigned long *data, unsigned long size);
void spu_write (unsigned long *addr, unsigned long *data, unsigned long size);
int spu_init (void (*irq_callback) (void *data, unsigned int id), void *data);
void spu_deinit (void);
