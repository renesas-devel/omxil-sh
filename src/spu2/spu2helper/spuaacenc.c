#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "spu.h"
#include "spuaacenc.h"
#include "RAACES_API.h"

#define AACSIZE 1536
#define PCMSIZE 2048
#define BUFALIGNMENT 0x800000
#define ERR(msg) fprintf (stderr, "spuaacenc: %s\n", msg)

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
static unsigned char *aacbuf, *aacaddr;
static pthread_mutex_t transfer_lock, transfer_done;
static int transfer_flag;
static RAACES_AAC *paac2;
static int initflag = 0; /* 0:befor initialize, 1:initialized, 2:decoding */
static struct buflist *inbuflist, *outbuflist;
static struct buflist *inbuf_current, *outbuf_current;
static struct buflist *inbuf_copying, *outbuf_copying;
static struct buflist_head inbuf_free, inbuf_used;
static struct buflist_head outbuf_free, outbuf_used;
static int inbuf_copied, outbuf_copied;
static int inbuf_end, outbuf_end;
static RAACES_AACInfo aacinfo;
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
	RAACES_Interrupt ();
}

static void
pcm_input_end_cb (unsigned long size)
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
	if (RAACES_TransferPcm (paac2, (short *)p->addr, p->flen / 2)
	    < RAACES_R_GOOD)
		ERR ("RAACES_TransferPcm error");
}

static void
stream_output_end_cb (unsigned long size, unsigned long flush)
{
	struct buflist *p;

	if (outbuf_current != NULL) {
		outbuf_current->flen = size;
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
		if (RAACES_TransferStream (paac2, (unsigned char *)p->addr,
					   p->alen) < RAACES_R_GOOD) {
			ERR ("RAACES_TransferStream error");
			return;
		}
	} else {
		if (size == 0) {
			ERR ("Encoded size is zero, check a StatusCode.");
			fprintf(stderr, "StatusCode=%08ld\n", paac2->statusCode);
			if (paac2->statusCode < 0) {
				ERR ("A minus status code indicate a encode error.");
				callbk_err = 1;
			}
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
encode_end_cb (RAACES_AAC *aac, long result, unsigned long pcnt,
	       unsigned long bcnt, unsigned long nframe)
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
init3 (void)
{
	RAACES_Func func;

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

	if (RAACES_Init ( 0/*use_dsp*/, &func, RAACES_ADDRESS_MODE_32BIT )
	    < RAACES_R_GOOD) {
		ERR ("RAACES_Init error");
		spu_deinit ();
		return -1;
	}

	pthread_mutex_init (&transfer_lock, NULL);
	pthread_mutex_init (&transfer_done, NULL);
	transfer_flag = 0;

	uint32_t dsp0_addr, dsp0_size;
	void *dsp0_io;

	spu_get_workarea (&dsp0_addr, &dsp0_size, &dsp0_io);

	paac2 = NULL;
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

	aacinfo.channelMode = 1;
	aacinfo.sampleRate = 44100;
	aacinfo.bitRate = 64000;
	aacinfo.outputFormat = 0; /* 0=ADTS 1=ADIF */
	aacinfo.enable_cbr = 0; /* 0=VBR */
	aacinfo.mode = 0;
	aacinfo.home = 0;
	aacinfo.original_copy = 0;
	aacinfo.copyright_id_present = 0;
	pthread_mutex_lock (&transfer_done);
	return 0;
}

static long
middleware_open (void)
{
	RAACES_CallbackFunc cb;
	static RAACES_AAC aac;
	long ret = RAACES_R_GOOD;

	if (paac2 != NULL)
		return 0;
	paac2 = &aac;
	cb.pcm_input_end_cb = pcm_input_end_cb;
	cb.stream_output_end_cb = stream_output_end_cb;
	cb.encode_end_cb = encode_end_cb;
	if ((ret = RAACES_Open (&aac, &aacinfo, 0, 0, 0x10200, &cb))
		 < RAACES_R_GOOD) {
		ERR ("RAACES_Open error");
		if (paac2->statusCode < 0) {
			ret = paac2->statusCode;
		} else {
			fprintf (stderr, "Odd statusCode %08lx\n", paac2->statusCode);
		}
		return ret;
	}
	paac2 = &aac;
	return ret;
}

static long
middleware_close (void)
{
	long ret = RAACES_R_GOOD;
	if (paac2 != NULL) {
		if ((ret = RAACES_Close (paac2)) < RAACES_R_GOOD) {
			ERR ("RAACES_Close error");
			if (paac2->statusCode < 0) {
				ret = paac2->statusCode;
			} else {
				fprintf (stderr, "Odd statusCode %08lx\n", paac2->statusCode);
			}
		}
		paac2 = NULL;
	}
	return ret;
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
spu_aac_encode_init (void)
{
	if (initflag == 0) {
		if (init3 () < 0)
			return -1;
		initflag = 1;
	}
	return 0;
}

int
spu_aac_encode_setfmt (struct spu_aac_encode_setfmt_data *format)
{
	RAACES_AACInfo ai;

	spu_aac_encode_init ();
	if (initflag == 2)
		return -2; /* need to stop encoding before calling this func */
	switch (format->channel) {
	default:
		return -1;
	case SPU_AAC_ENCODE_SETFMT_CHANNEL_MONO:
		ai.channelMode = 0;
		break;
	case SPU_AAC_ENCODE_SETFMT_CHANNEL_STEREO:
		ai.channelMode = 1;
		break;
	case SPU_AAC_ENCODE_SETFMT_CHANNEL_DUAL_MONO:
		ai.channelMode = 2;
		break;
	}
	switch (format->samplerate) {
	default:
		return -1;
	case 8000:
		if (format->bitrate >= 16000 && format->bitrate <= 48000)
			goto ok;
		return -1;
	case 11025:
		if (format->bitrate >= 22000 && format->bitrate <= 66150)
			goto ok;
		return -1;
	case 12000:
		if (format->bitrate >= 24000 && format->bitrate <= 72000)
			goto ok;
		return -1;
	case 16000:
		if (format->bitrate >= 23250 && format->bitrate <= 96000)
			goto ok;
		return -1;
	case 22050:
		if (format->bitrate >= 32000 && format->bitrate <= 132300)
			goto ok;
		return -1;
	case 24000:
		if (format->bitrate >= 35000 && format->bitrate <= 144000)
			goto ok;
		return -1;
	case 32000:
		if (format->bitrate >= 46500 && format->bitrate <= 192000)
			goto ok;
		return -1;
	case 44100:
		if (format->bitrate >= 64000 && format->bitrate <= 264600)
			goto ok;
		return -1;
	case 48000:
		if (format->bitrate >= 70000 && format->bitrate <= 288000)
			goto ok;
		return -1;
	ok:
		ai.sampleRate = format->samplerate;
		ai.bitRate = format->bitrate;
		break;
	}
	switch (format->type) {
	default:
		return -1;
	case SPU_AAC_ENCODE_SETFMT_TYPE_ADTS:
		ai.outputFormat = 0;
		break;
	case SPU_AAC_ENCODE_SETFMT_TYPE_ADIF:
		ai.outputFormat = 1;
		break;
	case SPU_AAC_ENCODE_SETFMT_TYPE_RAW:
		ai.outputFormat = 2;
		break;
	}
	ai.enable_cbr = (format->cbrmode != 0) ? 1 : 0;
	ai.mode = 0;
	ai.home = (format->home != 0) ? 1 : 0;
	ai.original_copy = (format->original != 0) ? 1 : 0;
	if (format->usecopyright != 0) {
		ai.copyright_id_present = 1;
		memcpy (ai.copyright_id, format->copyright, sizeof (char) * 8);
		ai.copyright_id[8] = '\0';
	} else {
		ai.copyright_id_present = 0;
		ai.copyright_id[0] = '\0';
	}
	memcpy (&aacinfo, &ai, sizeof aacinfo);
	return 0;
}

long
spu_aac_encode (void **destbuf, void *destend, void **srcbuf, void *srcend)
{
	int need_input, need_output;
	int inbuf_added;
	int endflag;
	long ret = RAACES_R_GOOD;

once_again:
	need_input = 0;
	need_output = 0;
	inbuf_added = 0;
	if (initflag == 0) {
		if (init3 () < 0)
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
			return ret;
		if ((ret = middleware_open ()) < 0)
			return ret;
		pcm_input_end_cb (0);
		stream_output_end_cb (0, 0);
		/* Encoding all frames.(2nd param nframe is zero) */
		if ((ret = RAACES_Encode (paac2, 0)) < RAACES_R_GOOD) {
			ERR ("RAACES_Encode error");
			if (paac2->statusCode < 0) {
				ret = paac2->statusCode;
			} else {
				fprintf (stderr, "Odd statusCode %08lx\n", paac2->statusCode);
			}
			middleware_close ();
			return ret;
		}
		initflag = 2;
	} else {
		if (need_input != 0)
			pcm_input_end_cb (0);
		if (need_output != 0)
			stream_output_end_cb (0, 0);
	}
	if (inbuf_end == 2) {
		if (endflag == 0)
			pthread_mutex_lock (&transfer_done);
		else if (outbuf_copying == NULL &&
			 buflist_poll (&outbuf_used) == NULL) {
			if (paac2->statusCode != 0)
				ERR ("strange statusCode; ignored");
			ret = middleware_close ();
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
	return ret;
}

long
spu_aac_encode_stop (void)
{
	long ret = RAACES_R_GOOD;
	if (initflag == 2) {
		if (RAACES_EncoderStop (paac2) < RAACES_R_GOOD) {
			ERR ("RAACES_EncoderStop error");
			fprintf (stderr, "statusCode %08lx\n", paac2->statusCode);
		}
		ret = middleware_close ();
		initflag = 1;
	}
	return ret;
}

long
spu_aac_encode_deinit (void)
{
	long ret = RAACES_R_GOOD;
	if (initflag) {
		ret = spu_aac_encode_stop ();
		RAACES_Quit ();
		spu_deinit ();
		buflist_free (&inbuflist);
		buflist_free (&outbuflist);
		initflag = 0;
	}
	return ret;
}
