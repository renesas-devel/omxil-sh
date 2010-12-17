struct spu_aac_decode_setfmt_data {
	enum spu_aac_decode_setfmt_type {
		SPU_AAC_DECODE_SETFMT_TYPE_ADTS,
		SPU_AAC_DECODE_SETFMT_TYPE_ADIF,
		SPU_AAC_DECODE_SETFMT_TYPE_RAW_AAC,
		SPU_AAC_DECODE_SETFMT_TYPE_RAW_AACPLUS,
	} type;
	int sampling_frequency;	/* for raw input only */
	int channel;
};

long spu_aac_decode (void **destbuf, void *destend, void **srcbuf,
		    void *srcend);
long spu_aac_decode_stop (void);
int spu_aac_decode_init (void);
long spu_aac_decode_deinit (void);
long spu_aac_decode_setfmt (struct spu_aac_decode_setfmt_data *format);
