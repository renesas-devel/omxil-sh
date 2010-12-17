struct spu_aac_encode_setfmt_data {
	enum spu_aac_encode_setfmt_type {
		SPU_AAC_ENCODE_SETFMT_TYPE_ADTS,
		SPU_AAC_ENCODE_SETFMT_TYPE_ADIF,
		SPU_AAC_ENCODE_SETFMT_TYPE_RAW,
	} type;
	enum spu_aac_encode_setfmt_channel {
		SPU_AAC_ENCODE_SETFMT_CHANNEL_MONO = 1,
		SPU_AAC_ENCODE_SETFMT_CHANNEL_STEREO,
		SPU_AAC_ENCODE_SETFMT_CHANNEL_DUAL_MONO,
	} channel;
	int samplerate;
	int bitrate;
	int cbrmode;		/* 0=VBR 1=CBR */
	int home;
	int original;
	int usecopyright;
	char copyright[9];
};

long spu_aac_encode (void **destbuf, void *destend, void **srcbuf,
		    void *srcend);
long spu_aac_encode_stop (void);
int spu_aac_encode_init (void);
long  spu_aac_encode_deinit (void);
int spu_aac_encode_setfmt (struct spu_aac_encode_setfmt_data *format);
