#ifndef CONFIG_H
#define CONFIG_H

/*#define SPU_DSP_MAX 2*/
#define SPU_DSP_MAX 1	/* SPU2DSP1 cannot be used with uiomux */

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define u32 unsigned long
#define u16 unsigned short
#define u8  unsigned char

#if 1
#define perr(format, arg...)				\
	fprintf(stderr, "%s :: %s (%d) > " format,	\
		__FILE__, __FUNCTION__, __LINE__, ##arg);
#else
#define perr(format, arg...)
#endif

#endif
