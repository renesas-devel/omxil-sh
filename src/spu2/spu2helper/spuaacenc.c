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
#define PCMSIZE 4096
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

struct state_info {
	/* variables set/cleared by main thread: */
	int init;
	int open;
	int first_block;
	/* variables cleared by main thread and set by interrupt thread: */
	int encode_end;
	int encode_really_end;
};

struct datalist {
	struct datalist *next, *prev;
	void *data;
	int datalen;
	int len;
};

struct datalist_head {
	struct datalist *head, *tail;
	pthread_mutex_t lock;
};

static short *pcmbuf, *pcmaddr;
static unsigned char *aacbuf, *aacaddr;
static pthread_mutex_t transfer_lock, transfer_done;
static int transfer_flag;
static RAACES_AAC *paac2;
static struct state_info state;
static struct buflist *inbuflist, *outbuflist;
static struct buflist *inbuf_current, *outbuf_current;
static struct buflist *inbuf_copying, *outbuf_copying;
static struct buflist_head inbuf_free, inbuf_used;
static struct buflist_head outbuf_free, outbuf_used;
static int inbuf_copied, outbuf_copied;
static int inbuf_end, outbuf_end;
static RAACES_AACInfo aacinfo;
static int callbk_err = 0;
static long encode_end_status;
static struct datalist_head indata, outdata, delaydata;

static void
datalist_init (struct datalist_head *d)
{
	pthread_mutex_init (&d->lock, NULL);
	d->head = NULL;
	d->tail = NULL;
}

static void
datalist_add (struct datalist_head *d, void *data, int datalen, int addlen,
	      int extend)
{
	pthread_mutex_lock (&d->lock);
	if (d->tail != NULL) {
		if (extend != 0 && d->tail->datalen == datalen) {
			if (datalen == 0)
				goto add;
			if (memcmp (d->tail->data, data, datalen) == 0)
				goto add;
		}
		d->tail->next = malloc (sizeof *d->tail->next);
		d->tail->next->next = NULL;
		d->tail->next->prev = d->tail;
		d->tail = d->tail->next;
		goto listinit;
	add:
		d->tail->len += addlen;
	} else {
		d->tail = malloc (sizeof *d->tail);
		d->head = d->tail;
		d->tail->next = NULL;
		d->tail->prev = NULL;
	listinit:
		d->tail->data = NULL;
		d->tail->datalen = datalen;
		d->tail->len = addlen;
		if (datalen != 0) {
			d->tail->data = malloc (datalen);
			memcpy (d->tail->data, data, datalen);
		}
	}
	pthread_mutex_unlock (&d->lock);
}

static void
datalist_sub (struct datalist_head *d, void **data, int *datalen, int sublen)
{
	struct datalist *p;

	pthread_mutex_lock (&d->lock);
	p = d->head;
	if (p != NULL) {
		if (datalen != NULL) {
			*data = NULL;
			*datalen = p->datalen;
			if (p->datalen != 0) {
				*data = malloc (p->datalen);
				memcpy (*data, p->data, p->datalen);
			}
		}
	} else {
		ERR ("No datalist exists!");
	}
	while (sublen > 0 && p != NULL) {
		if (p->len > sublen) {
			p->len -= sublen;
			sublen = 0;
			break;
		}
		sublen -= p->len;
		if (d->head == d->tail) {
			d->head = NULL;
			d->tail = NULL;
		} else {
			d->head = p->next;
			d->head->prev = NULL;
		}
		if (p->datalen != 0)
			free (p->data);
		free (p);
		p = d->head;
	}
	pthread_mutex_unlock (&d->lock);
}

static int
datalist_headlen (struct datalist_head *d)
{
	int len = 0;

	pthread_mutex_lock (&d->lock);
	if (d->head != NULL)
		len = d->head->len;
	pthread_mutex_unlock (&d->lock);
	return len;
}

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
	void *data;
	int datalen;
	int inputlen;
	int extend;

	if (aacinfo.outputFormat == 2) /* if the output format is raw */
		extend = 0;
	else
		extend = 1;
	if (pcnt != 0) {
		if (aacinfo.channelMode == 1) /* stereo */
			inputlen = pcnt * 2 * 2;
		else
			inputlen = pcnt * 2;
		datalist_sub (&indata, &data, &datalen, inputlen);
		if (state.first_block != 0)
			datalist_add (&delaydata, data, datalen, 1, 1);
		datalist_add (&delaydata, data, datalen, 1, 1);
		if (datalen != 0)
			free (data);
	}
	if (bcnt != 0) {
		datalist_sub (&delaydata, &data, &datalen, 1);
		datalist_add (&outdata, data, datalen, bcnt, extend);
		if (datalen != 0)
			free (data);
	}
	encode_end_status = paac2->statusCode;
	pthread_mutex_lock (&transfer_lock);
	state.encode_end = 1;
	if (result == -1 &&
	    (encode_end_status == RAACES_C_END_OF_FILE ||
	     encode_end_status == RAACES_C_BEFORE_END_OF_FILE ||
	     encode_end_status == RAACES_C_LACK_FRAME))
		state.encode_really_end = 1;
	if (transfer_flag != 0) {
		transfer_flag = 0;
		pthread_mutex_unlock (&transfer_done);
	}
	pthread_mutex_unlock (&transfer_lock);
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

	buflist_init (pcmbuf, (uint32_t)pcmaddr, &inbuflist, &inbuf_free,
		      &inbuf_used, PCMSIZE, dsp0_size / 2);
	buflist_init (aacbuf, (uint32_t)aacaddr, &outbuflist, &outbuf_free,
		      &outbuf_used, AACSIZE, dsp0_size / 2);
	inbuf_current = NULL;
	outbuf_current = NULL;
	inbuf_copying = NULL;
	outbuf_copying = NULL;

	datalist_init (&indata);
	datalist_init (&outdata);
	datalist_init (&delaydata);

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
	long err = RAACES_R_GOOD;

	if (paac2 != NULL)
		return 0;
	paac2 = &aac;
	cb.pcm_input_end_cb = pcm_input_end_cb;
	cb.stream_output_end_cb = stream_output_end_cb;
	cb.encode_end_cb = encode_end_cb;
	if ((err = RAACES_Open (&aac, &aacinfo, 0, 0, 0x10200, &cb))
		 < RAACES_R_GOOD) {
		ERR ("RAACES_Open error");
		if (paac2->statusCode < 0) {
			err = paac2->statusCode;
		} else {
			fprintf (stderr, "Odd statusCode %08lx\n", paac2->statusCode);
		}
		return err;
	}
	paac2 = &aac;
	return err;
}

static long
middleware_close (void)
{
	long err = RAACES_R_GOOD;
	if (paac2 != NULL) {
		if ((err = RAACES_Close (paac2)) < RAACES_R_GOOD) {
			ERR ("RAACES_Close error");
			if (paac2->statusCode < 0) {
				err = paac2->statusCode;
			} else {
				fprintf (stderr, "Odd statusCode %08lx\n", paac2->statusCode);
			}
		}
		paac2 = NULL;
	}
	return err;
}

static long
encoder_close (int encoding)
{
	long err;

	if (encoding != 0 && RAACES_EncoderStop (paac2) < RAACES_R_GOOD) {
		ERR ("RAACES_EncoderStop error");
		fprintf (stderr, "statusCode %08lx\n", paac2->statusCode);
	}
	err = middleware_close ();
	return err;
}

static int
copy_output_buffer (void **destbuf, void *destend, int *pneed_output,
		    void **data, int *datalen)
{
	uint8_t *_dstbuf;
	int _dstlen, copylen;
	int first = 1;
	int firstdatalen = 0;

	*data = NULL;
	*datalen = 0;
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
		if (aacinfo.outputFormat == 2) { /* if output format is raw */
			firstdatalen = datalist_headlen (&outdata);
			/* In case of copylen < firstdatalen: the next
			   buffer must have the remain data, or the
			   output would be separated. This code simply
			   stops copying if the next buffer is not
			   available. The length of the next buffer is
			   not checked, but it should be enough
			   because AACSIZE is maximum frame size. */
			if (copylen < firstdatalen &&
			    buflist_poll (&outbuf_used) == NULL)
				break;
			if (firstdatalen == 0)
				ERR ("firstdatalen is zero!");
			else if (copylen > firstdatalen)
				copylen = firstdatalen;
		}
		memcpy (_dstbuf, (uint8_t *)outbuf_copying->buf +
			outbuf_copied, copylen);
		_dstbuf += copylen;
		_dstlen -= copylen;
		outbuf_copied += copylen;
		if (first != 0) {
			datalist_sub (&outdata, data, datalen, copylen);
			first = 0;
		} else {
			datalist_sub (&outdata, NULL, NULL, copylen);
		}
		if (copylen == firstdatalen)
			break;
	}
	if (*destbuf == (void *)_dstbuf && *destbuf != destend)
		return 0;
	*destbuf = (void *)_dstbuf;
	return 1;
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

static int
copy_input_buffer (void **srcbuf, void *srcend, int *pneed_input,
		   int *pinbuf_added, void *data, int datalen)
{
	uint8_t *_srcbuf;
	int _srclen, copylen;

	if (srcbuf == NULL) {
		handle_last_input_buffer (pneed_input, pinbuf_added);
		return 1;
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
		datalist_add (&indata, data, datalen, copylen, 1);
	}
	if (*srcbuf == (void *)_srcbuf && *srcbuf != srcend)
		return 0;
	*srcbuf = (void *)_srcbuf;
	return 1;
}

int
spu_aac_encode_init (void)
{
	if (state.init == 0) {
		if (init3 () < 0)
			return -1;
		state.init = 1;
	}
	return 0;
}

int
spu_aac_encode_setfmt (struct spu_aac_encode_setfmt_data *format)
{
	RAACES_AACInfo ai;

	spu_aac_encode_init ();
	if (state.open != 0)
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
spu_aac_encode (void **destbuf, void *destend, void **srcbuf, void *srcend,
		void *dataout, void *datain, int datalen)
{
	int need_input, need_output;
	int inbuf_added;
	int endflag;
	int state_encode;
	int state_encode_end, state_encode_really_end;
	int incopy, outcopy;
	long err = RAACES_R_GOOD;
	void *_dataout;
	int _dataoutlen;

once_again:
	need_input = 0;
	need_output = 0;
	inbuf_added = 0;
	if (state.init == 0) {
		if (init3 () < 0)
			return -1;
		state.init = 1;
	}
	pthread_mutex_lock (&transfer_lock);
	endflag = outbuf_end;
	state_encode_end = state.encode_end;
	state_encode_really_end = state.encode_really_end;
	transfer_flag = 1;
	pthread_mutex_unlock (&transfer_lock);

	outcopy = copy_output_buffer (destbuf, destend, &need_output,
				      &_dataout, &_dataoutlen);
	incopy = copy_input_buffer (srcbuf, srcend, &need_input, &inbuf_added,
				    datain, datalen);
	if (_dataoutlen != 0) {
		if (_dataoutlen > datalen)
			_dataoutlen = datalen;
		memcpy (dataout, _dataout, _dataoutlen);
		free (_dataout);
	}

	if (state.open == 0) {
		state_encode = 0;
		state.encode_end = 0;
		state.encode_really_end = 0;
		state_encode_end = 0;
		state_encode_really_end = 0;
		callbk_err = 0;
		inbuf_end = 0;
		pthread_mutex_lock (&transfer_lock);
		outbuf_end = 0;
		pthread_mutex_unlock (&transfer_lock);
		if (inbuf_added == 0)
			goto unlock_ret;
		if ((err = middleware_open ()) < 0)
			goto ret;
		state.first_block = 1;
		state.open = 1;
		need_input = 1;
		need_output = 1;
	} else {
		state_encode = 1;
		if (state_encode_really_end != 0)
			state_encode = 0;
	}
	if (state_encode_end != 0) {
		state_encode = 0;
		state.encode_end = 0;
		if (state.first_block != 0) {
			state.first_block = 0;
		}
	}
	if (need_input != 0)
		pcm_input_end_cb (0);
	if (need_output != 0)
		stream_output_end_cb (0, 0);
	if (state_encode == 0 && state_encode_really_end == 0) {
		if ((err = RAACES_Encode (paac2, 1)) < RAACES_R_GOOD) {
			ERR ("RAACES_Encode error");
			if (paac2->statusCode < 0) {
				err = paac2->statusCode;
			} else {
				fprintf (stderr, "Odd statusCode %08lx\n", paac2->statusCode);
			}
			goto close_and_ret;
		}
		state_encode = 1;
	}
	if (inbuf_end == 2 && endflag != 0 && outbuf_copying == NULL &&
	    buflist_poll (&outbuf_used) == NULL) {
		if (encode_end_status != RAACES_C_END_OF_FILE &&
		    encode_end_status != RAACES_C_BEFORE_END_OF_FILE &&
		    encode_end_status != RAACES_C_LACK_FRAME)
			ERR ("strange statusCode; ignored");
		err = encoder_close (0);
		state.open = 0;
		goto ret;
	}
	if (callbk_err != 0) {
		err = -1;
		goto close_and_ret;
	}
	if ((inbuf_end == 2 || incopy == 0) && outcopy == 0) {
		pthread_mutex_lock (&transfer_done);
		goto once_again;
	}
	goto unlock_ret;
close_and_ret:
	encoder_close (state_encode);
	state.open = 0;
ret:
	if (inbuf_copying != NULL) {
		buflist_add (&inbuf_free, inbuf_copying);
		inbuf_copying = NULL;
	}
	if (outbuf_current != NULL) {
		buflist_add (&outbuf_free, outbuf_current);
		outbuf_current = NULL;
	}
	do {
		if (inbuf_current != NULL)
			buflist_add (&inbuf_free, inbuf_current);
		inbuf_current = buflist_pop (&inbuf_used);
	} while (inbuf_current != NULL);
	do {
		if (outbuf_copying != NULL)
			buflist_add (&outbuf_free, outbuf_copying);
		outbuf_copying = buflist_pop (&outbuf_used);
	} while (outbuf_copying != NULL);
	while ((_dataoutlen = datalist_headlen (&indata)) != 0)
		datalist_sub (&indata, NULL, NULL, _dataoutlen);
	while ((_dataoutlen = datalist_headlen (&outdata)) != 0)
		datalist_sub (&outdata, NULL, NULL, _dataoutlen);
	while ((_dataoutlen = datalist_headlen (&delaydata)) != 0)
		datalist_sub (&delaydata, NULL, NULL, _dataoutlen);
unlock_ret:
	pthread_mutex_lock (&transfer_lock);
	if (transfer_flag == 0) {
		pthread_mutex_unlock (&transfer_lock);
		pthread_mutex_lock (&transfer_done);
	} else {
		transfer_flag = 0;
		pthread_mutex_unlock (&transfer_lock);
	}
	return err;
}

long
spu_aac_encode_stop (void)
{
	long err = RAACES_R_GOOD;
	if (state.open != 0) {
		err = encoder_close (1);
		state.open = 0;
	}
	return err;
}

long
spu_aac_encode_deinit (void)
{
	long err = RAACES_R_GOOD;
	if (state.init != 0) {
		err = spu_aac_encode_stop ();
		RAACES_Quit ();
		spu_deinit ();
		buflist_free (&inbuflist);
		buflist_free (&outbuflist);
		state.init = 0;
	}
	return err;
}
