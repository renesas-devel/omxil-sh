#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "spu.h"
#include "spuaacdec.h"
#include "RSACPDS_API.h"

#define AACSIZE 1536
#define PCMSIZE 2048
#define BUFALIGNMENT 0x800000
#define ERR(msg) fprintf (stderr, "spuaacdec: %s\n", msg)

struct buflist {
	struct buflist *next, **prevnext;
	struct buflist *anext;
	void *buf;
	uint32_t addr;
	int alen;
	int flen;
};

struct buflist_head {
	struct buflist *next, **prevnext;
	pthread_mutex_t lock;
	int popnull;
};

static short *pcmbuf, *pcmaddr;
static RSACPDS_AAC *paac;
static unsigned char *aacbuf, *aacaddr;
static pthread_mutex_t transfer_lock, transfer_done;
static int transfer_flag;
static int initflag = 0;
static struct buflist *inbuflist, *outbuflist;
static struct buflist *inbuf_current, *outbuf_current;
static struct buflist *inbuf_copying, *outbuf_copying;
static struct buflist_head inbuf_free, inbuf_used;
static struct buflist_head outbuf_free, outbuf_used;
static int inbuf_copied, outbuf_copied;
static int inbuf_end, outbuf_end;
static enum spu_aac_decode_setfmt_type format_type;
static int sampling_frequency_index;
static unsigned long channel = 2;
static int callbk_err = 0;

static int
buflist_add (struct buflist_head *buf, struct buflist *p)
{
	int popnull;

	p->next = NULL;
	pthread_mutex_lock (&buf->lock);
	popnull = buf->popnull;
	buf->popnull = 0;
	p->prevnext = buf->prevnext;
	*buf->prevnext = p;
	buf->prevnext = &p->next;
	pthread_mutex_unlock (&buf->lock);
	return popnull;
}

static struct buflist *
buflist_pop (struct buflist_head *buf)
{
	struct buflist *p;

	pthread_mutex_lock (&buf->lock);
	p = buf->next;
	if (p != NULL) {
		buf->next = p->next;
		if (buf->next == NULL)
			buf->prevnext = &buf->next;
	} else {
		buf->popnull = 1;
	}
	pthread_mutex_unlock (&buf->lock);
	return p;
}

static struct buflist *
buflist_poll (struct buflist_head *buf)
{
	struct buflist *p;

	pthread_mutex_lock (&buf->lock);
	p = buf->next;
	pthread_mutex_unlock (&buf->lock);
	return p;
}

static void
irq_callback (void *data, unsigned int id)
{
	RSACPDS_Interrupt ();
}

static void
stream_input_end_cb (unsigned long size)
{
	struct buflist *p;

	if (inbuf_current != NULL) {
		buflist_add (&inbuf_free, inbuf_current);
		inbuf_current = NULL;
	}
	p = inbuf_current = buflist_pop (&inbuf_used);
	if (p == NULL) {
		pthread_mutex_lock (&transfer_lock);
		if (transfer_flag != 0) {
			transfer_flag = 0;
			pthread_mutex_unlock (&transfer_done);
		}
		pthread_mutex_unlock (&transfer_lock);
		return;
	}
	if (RSACPDS_TransferStream (paac, (unsigned char *)p->addr, p->flen)
	    < RSACPDS_RTN_GOOD)
		ERR ("RSACPDS_TransferStream error");
}

static void
pcm_output_end_cb (unsigned long size, unsigned long flush)
{
	struct buflist *p;

	if (outbuf_current != NULL) {
		outbuf_current->flen = size * 2;
		buflist_add (&outbuf_used, outbuf_current);
		outbuf_current = NULL;
	}
	if (flush == 0) {
		p = outbuf_current = buflist_pop (&outbuf_free);
		if (p == NULL) {
			pthread_mutex_lock (&transfer_lock);
			if (transfer_flag != 0) {
				transfer_flag = 0;
				pthread_mutex_unlock (&transfer_done);
			}
			pthread_mutex_unlock (&transfer_lock);
			return;
		}
		if (RSACPDS_TransferPcm (paac, (short *)p->addr, p->alen / 2)
		    < RSACPDS_RTN_GOOD)
			ERR ("RSACPDS_TransferPcm error");
	} else {
		if (size == 0) {
			ERR ("decode error, decoded size is zero");
			callbk_err = 1;
			fprintf(stderr, "StatusCode=%08ld\n", RSACPDS_GetStatusCode(paac));
		}
		pthread_mutex_lock (&transfer_lock);
		if (transfer_flag != 0) {
			transfer_flag = 0;
			pthread_mutex_unlock (&transfer_done);
		}
		outbuf_end = 1;
		pthread_mutex_unlock (&transfer_lock);
	}
}

static void
decode_end_cb (RSACPDS_AAC *aac, long ret, unsigned long bcnt,
	       RSACPDS_OUT_INFO outInfo, unsigned long pcnt, unsigned long n)
{
}

static void
skip_end_cb (RSACPDS_AAC *aac, long ret, unsigned long bcnt, unsigned long n)
{
}

static void
buflist_init (void *buf, uint32_t addr, struct buflist **buflist,
	      struct buflist_head *buf_free, struct buflist_head *buf_used,
	      int blocksize, int totalsize)
{
	uint8_t *bufc;
	struct buflist *p;
	uint32_t naddr, naddrblk;

	bufc = (uint8_t *)buf;
	*buflist = NULL;
	buf_free->next = NULL;
	buf_free->prevnext = &buf_free->next;
	pthread_mutex_init (&buf_free->lock, NULL);
	buf_used->next = NULL;
	buf_used->prevnext = &buf_used->next;
	pthread_mutex_init (&buf_used->lock, NULL);
	while (totalsize >= blocksize) {
		naddr = (uint32_t)(addr / BUFALIGNMENT);
		naddrblk = (uint32_t)((addr + blocksize) / BUFALIGNMENT);
		if (naddr < naddrblk
			&& (addr + blocksize) % BUFALIGNMENT) {
			fprintf(stderr, "Avoided the over 8MB alignment "
				"of the middleware limit.(addr=%08x)\n",
				addr);
		} else {
			p = malloc (sizeof *p);
			if (p == NULL)
				abort ();
			p->buf = (void *)bufc;
			p->addr = addr;
			p->alen = blocksize;
			p->anext = *buflist;
			*buflist = p;
			buflist_add (buf_free, p);
		}
		bufc += blocksize;
		addr += blocksize;
		totalsize -= blocksize;
	}
}

static void
buflist_free (struct buflist **buflist)
{
	struct buflist *p;

	while ((p = *buflist) != NULL) {
		*buflist = p->anext;
		free (p);
	}
}

static int
init2 (void)
{
	RSACPDS_Func func;

	func.reg_read		     = spu_read;
	func.reg_write		     = spu_write;
	func.set_event		     = spu_set_event;
	func.wait_for_event	     = spu_wait_event;
	func.enter_critical_section  = spu_enter_critical_section;
	func.leave_critical_section  = spu_leave_critical_section;

	if (spu_init (irq_callback, NULL) < 0) {
		ERR ("spu_init error");
		return -1;
	}

	if (RSACPDS_Init ( 0/*use_dsp*/, &func, RSACPDS_ADDRESS_MODE_32BIT )
	    < RSACPDS_RTN_GOOD) {
		ERR ("RSACPDS_Init error");
		spu_deinit ();
		return -1;
	}

	pthread_mutex_init (&transfer_lock, NULL);
	pthread_mutex_init (&transfer_done, NULL);
	transfer_flag = 0;

	uint32_t dsp0_addr, dsp0_size;
	void *dsp0_io;

	spu_get_workarea (&dsp0_addr, &dsp0_size, &dsp0_io);

	paac = NULL;
	aacbuf = (unsigned char *)dsp0_io;
	aacaddr = (unsigned char *)dsp0_addr;
	pcmbuf = (short *)dsp0_io;
	pcmbuf += dsp0_size / 4;
	pcmaddr = (short *)(dsp0_addr + dsp0_size / 2);

	buflist_init (aacbuf, (uint32_t)aacaddr, &inbuflist, &inbuf_free,
		      &inbuf_used, AACSIZE, dsp0_size / 2);
	buflist_init (pcmbuf, (uint32_t)pcmaddr, &outbuflist, &outbuf_free,
		      &outbuf_used, PCMSIZE, dsp0_size / 2);
	inbuf_current = NULL;
	outbuf_current = NULL;
	inbuf_copying = NULL;
	outbuf_copying = NULL;

	format_type = SPU_AAC_DECODE_SETFMT_TYPE_ADTS;
	pthread_mutex_lock (&transfer_done);
	return 0;
}

static long
middleware_open (void)
{
	RSACPDS_CallbackFunc cb;
	static RSACPDS_AAC aac;
	long err = RSACPDS_RTN_GOOD;

	if (paac != NULL)
		return 0;
	cb.stream_input_end_cb = stream_input_end_cb;
	cb.pcm_output_end_cb = pcm_output_end_cb;
	cb.decode_end_cb = decode_end_cb;
	cb.skip_end_cb = skip_end_cb;
	if (RSACPDS_Open (&aac, 0, 0, 0, channel, 1, (RSACPDS_SWAP_MODE_BYTE << 16)
			  | (RSACPDS_SWAP_MODE_WORD << 8)
			  | RSACPDS_PCM_INTERLEAVE, 0, 0, 0, 0, &cb)
	    < RSACPDS_RTN_GOOD) {
		long ret = RSACPDS_GetStatusCode(paac);
		if (ret < 0){
			err = ret;
		} else {
			fprintf(stderr, "Odd StatusCode %08lx\n", ret);
			err = -1;
		}
		ERR ("RSACPDS_Open error");
		return err;
	}
	paac = &aac;
	return err;
}

static long
middleware_close (void)
{
	long err = RSACPDS_RTN_GOOD;
	if (paac != NULL) {
		if (RSACPDS_Close (paac) < RSACPDS_RTN_GOOD) {
			ERR ("RSACPDS_Close error");
			err = RSACPDS_GetStatusCode(paac);
		}
		paac = NULL;
	}
	return err;
}

static void
copy_output_buffer (void **destbuf, void *destend, int *pneed_output)
{
	uint8_t *_dstbuf;
	int _dstlen, copylen;

	_dstbuf = (uint8_t *)*destbuf;
	_dstlen = (uint8_t *)destend - _dstbuf;
	if (outbuf_copying == NULL) {
	get_outbuf:
		outbuf_copying = buflist_pop (&outbuf_used);
		outbuf_copied = 0;
	}
	while (outbuf_copying != NULL) {
		copylen = outbuf_copying->flen - outbuf_copied;
		if (copylen == 0) {
			if (buflist_add (&outbuf_free, outbuf_copying))
				*pneed_output = 1;
			goto get_outbuf;
		}
		if (_dstlen == 0)
			break;
		if (copylen > _dstlen)
			copylen = _dstlen;
		memcpy (_dstbuf, (uint8_t *)outbuf_copying->buf +
			outbuf_copied, copylen);
		_dstbuf += copylen;
		_dstlen -= copylen;
		outbuf_copied += copylen;
	}
	*destbuf = (void *)_dstbuf;
}

static void
handle_last_input_buffer (int *pneed_input, int *pinbuf_added)
{
	switch (inbuf_end) {
	case 0:
		if (inbuf_copying != NULL && inbuf_copied > 0) {
			inbuf_copying->flen = inbuf_copied;
			if (buflist_add (&inbuf_used, inbuf_copying))
				*pneed_input = 1;
			*pinbuf_added = 1;
			inbuf_copying = NULL;
		}
		inbuf_end = 1;
		/* fall through */
	case 1:
		if (inbuf_copying == NULL) {
			inbuf_copying = buflist_pop (&inbuf_free);
			if (inbuf_copying == NULL)
				break;
		}
		inbuf_copying->flen = 0;
		if (buflist_add (&inbuf_used, inbuf_copying))
			*pneed_input = 1;
		*pinbuf_added = 1;
		inbuf_copying = NULL;
		inbuf_end = 2;
		/* fall through */
	case 2:
		break;
	}
}

static void
copy_input_buffer (void **srcbuf, void *srcend, int *pneed_input,
		   int *pinbuf_added)
{
	uint8_t *_srcbuf;
	int _srclen, copylen;

	if (srcbuf == NULL) {
		handle_last_input_buffer (pneed_input, pinbuf_added);
		return;
	}
	_srcbuf = (uint8_t *)*srcbuf;
	_srclen = (uint8_t *)srcend - _srcbuf;
	if (inbuf_copying == NULL) {
	get_inbuf:
		inbuf_copying = buflist_pop (&inbuf_free);
		inbuf_copied = 0;
	}
	while (inbuf_copying != NULL) {
		copylen = inbuf_copying->alen - inbuf_copied;
		if (copylen == 0) {
			inbuf_copying->flen = inbuf_copied;
			if (buflist_add (&inbuf_used, inbuf_copying))
				*pneed_input = 1;
			*pinbuf_added = 1;
			goto get_inbuf;
		}
		if (_srclen == 0)
			break;
		if (copylen > _srclen)
			copylen = _srclen;
		memcpy ((uint8_t *)inbuf_copying->buf + inbuf_copied,
			_srcbuf, copylen);
		_srcbuf += copylen;
		_srclen -= copylen;
		inbuf_copied += copylen;
	}
	*srcbuf = (void *)_srcbuf;
}

int
spu_aac_decode_init (void)
{
	if (initflag == 0) {
		if (init2 () < 0)
			return -1;
		initflag = 1;
	}
	return 0;
}

long
spu_aac_decode_setfmt (struct spu_aac_decode_setfmt_data *format)
{
	if (format->channel >= 1 && format->channel <= 6) {
		channel = format->channel;
	} else {
		return -1;
	}
	spu_aac_decode_init ();
	if (initflag == 2)
		return -2; /* need to stop decoding before calling this func */
	switch (format->type) {
	default:
		return -1;
	case SPU_AAC_DECODE_SETFMT_TYPE_ADTS:
	case SPU_AAC_DECODE_SETFMT_TYPE_ADIF:
		format_type = format->type;
		break;
	case SPU_AAC_DECODE_SETFMT_TYPE_RAW_AAC:
		switch (format->sampling_frequency) {
		default:
			return -1;
		case 8000:
			format_type = format->type;
			sampling_frequency_index = 0xb;
			break;
		case 11025:
			format_type = format->type;
			sampling_frequency_index = 0xa;
			break;
		case 12000:
			format_type = format->type;
			sampling_frequency_index = 0x9;
			break;
		case 16000:
			format_type = format->type;
			sampling_frequency_index = 0x8;
			break;
		case 22050:
			format_type = format->type;
			sampling_frequency_index = 0x7;
			break;
		case 24000:
			format_type = format->type;
			sampling_frequency_index = 0x6;
			break;
		case 32000:
			format_type = format->type;
			sampling_frequency_index = 0x5;
			break;
		case 44100:
			format_type = format->type;
			sampling_frequency_index = 0x4;
			break;
		case 48000:
			format_type = format->type;
			sampling_frequency_index = 0x3;
			break;
		case 64000:
			format_type = format->type;
			sampling_frequency_index = 0x2;
			break;
		case 88200:
			format_type = format->type;
			sampling_frequency_index = 0x1;
			break;
		case 96000:
			format_type = format->type;
			sampling_frequency_index = 0x0;
			break;
		}
		break;
	case SPU_AAC_DECODE_SETFMT_TYPE_RAW_AACPLUS:
		switch (format->sampling_frequency) {
		default:
			return -1;
		case 8000:
			format_type = format->type;
			sampling_frequency_index = 0xb;
			break;
		case 11025:
			format_type = format->type;
			sampling_frequency_index = 0xa;
			break;
		case 12000:
			format_type = format->type;
			sampling_frequency_index = 0x9;
			break;
		case 16000:
			format_type = format->type;
			sampling_frequency_index = 0x8;
			break;
		case 22050:
			format_type = format->type;
			sampling_frequency_index = 0x7;
			break;
		case 24000:
			format_type = format->type;
			sampling_frequency_index = 0x6;
			break;
		}
		break;
	}
	return 0;
}

long
spu_aac_decode (void **destbuf, void *destend, void **srcbuf, void *srcend)
{
	RSACPDS_AdtsHeader adtsheader;
	RSACPDS_AdifHeader adifheader;
	unsigned long bcnt;
	long status;
	int need_input, need_output;
	int inbuf_added;
	int endflag;
	long err = RSACPDS_RTN_GOOD;

once_again:
	need_input = 0;
	need_output = 0;
	inbuf_added = 0;
	if (initflag == 0) {
		if (init2 () < 0)
			return -1;
		initflag = 1;
	}
	pthread_mutex_lock (&transfer_lock);
	endflag = outbuf_end;
	if (endflag == 0)
		transfer_flag = 1;
	pthread_mutex_unlock (&transfer_lock);

	/* transfer output buffers */
	copy_output_buffer (destbuf, destend, &need_output);

	/* transfer input buffers */
	copy_input_buffer (srcbuf, srcend, &need_input, &inbuf_added);

	if (initflag == 1) {
		inbuf_end = 0;
		pthread_mutex_lock (&transfer_lock);
		outbuf_end = 0;
		pthread_mutex_unlock (&transfer_lock);
		if (inbuf_added == 0)
			return err;
		if ((err = middleware_open ()) < 0)
			return err;
		if (RSACPDS_SetDecOpt(paac, 0) < RSACPDS_RTN_GOOD) {
			ERR ("RSACPDS_SetDecOpt error");
			middleware_close ();
			return -1;
		}
		stream_input_end_cb (0);
		pcm_output_end_cb (0, 0);
		switch (format_type) {
		case SPU_AAC_DECODE_SETFMT_TYPE_ADTS:
			if (RSACPDS_GetAdtsHeader (paac, &adtsheader, &bcnt)
			    < RSACPDS_RTN_GOOD) {
				ERR ("RSACPDS_GetAdtsHeader error");
				middleware_close ();
				return -1;
			}
			break;
		case SPU_AAC_DECODE_SETFMT_TYPE_ADIF:
			if (RSACPDS_GetAdifHeader (paac, &adifheader, &bcnt)
			    < RSACPDS_RTN_GOOD) {
				ERR ("RSACPDS_GetAdifHeader error");
				middleware_close ();
				return -1;
			}
			break;
		default:
			if (RSACPDS_SetFormat (paac, sampling_frequency_index)
			    < RSACPDS_RTN_GOOD) {
				ERR ("RSACPDS_SetFormat error");
				middleware_close ();
				return -1;
			}
			break;
		}
		if (RSACPDS_Decode (paac, 0) < RSACPDS_RTN_GOOD) {
			ERR ("RSACPDS_Decode error");
			middleware_close ();
			return RSACPDS_GetStatusCode(paac);
		}
		initflag = 2;
	} else {
		if (need_input != 0)
			stream_input_end_cb (0);
		if (need_output != 0)
			pcm_output_end_cb (0, 0);
	}
	if (inbuf_end == 2) {
		if (endflag == 0)
			pthread_mutex_lock (&transfer_done);
		else if (outbuf_copying == NULL &&
			 buflist_poll (&outbuf_used) == NULL) {
			if (paac->statusCode != 0)
				ERR ("strange statusCode; ignored");
			if (RSACPDS_DecodeStatus (paac, &status)
			    < RSACPDS_RTN_GOOD) {
				ERR ("RSACPDS_DecodeStatus error");
				status = 0;
			}
			if (status != 0)
				/*ERR ("strange status; ignored")*/;
			err = middleware_close ();
			initflag = 1;
		}
	} else if (buflist_poll (&inbuf_used) != NULL &&
		   buflist_poll (&outbuf_free) != NULL) {
		pthread_mutex_lock (&transfer_done);
	} else {
		pthread_mutex_lock (&transfer_lock);
		if (transfer_flag == 0) {
			pthread_mutex_unlock (&transfer_lock);
			pthread_mutex_lock (&transfer_done);
		} else {
			transfer_flag = 0;
			pthread_mutex_unlock (&transfer_lock);
		}
	}
	if (callbk_err != 0) {
		return -1;
	}

	if (inbuf_end != 0 && (uint8_t *)destend - (uint8_t *)*destbuf != 0 &&
	    buflist_poll (&outbuf_used) != NULL)
		goto once_again;
	return err;
}

long
spu_aac_decode_stop (void)
{
	long err = RSACPDS_RTN_GOOD;
	if (initflag == 2) {
		if (RSACPDS_DecoderStop (paac) != 0)
			ERR ("RSACPDS_DecoderStop error");
		err = middleware_close ();
		initflag = 1;
	}
	return err;
}

long
spu_aac_decode_deinit (void)
{
	long err = RSACPDS_RTN_GOOD;
	if (initflag) {
		err = spu_aac_decode_stop ();
		RSACPDS_Quit ();
		spu_deinit ();
		buflist_free (&inbuflist);
		buflist_free (&outbuflist);
		initflag = 0;
	}
	return err;
}
