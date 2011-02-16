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

struct state_info {
	/* variables set/cleared by main thread: */
	int init;
	int open;
	int first_block;
	/* variables cleared by main thread and set by interrupt thread: */
	int decode_end;
	int decode_really_end;
};

static short *pcmbuf, *pcmaddr;
static RSACPDS_AAC *paac;
static unsigned char *aacbuf, *aacaddr;
static pthread_mutex_t transfer_lock, transfer_done;
static int transfer_flag;
static struct state_info state;
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
static long decode_end_status;
static struct spu_aac_decode_fmt format_current, format_next;
static int next_sampling_frequency_index;
static long output_remain, output_remain_add;

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
	static const int freqlist[2][0xc] = {
		{
			96000, 88200, 64000, 48000,
			44100, 32000, 24000, 22050,
			16000, 12000, 11025, 8000,
		},
		{
			0, 0, 0, 0,
			0, 0, 24000 * 2, 22050 * 2,
			16000 * 2, 12000 * 2, 11025 * 2, 8000 * 2,
		},
	};
	long end_block_size;

	decode_end_status = paac->statusCode;
	end_block_size = 1024;
	if (format_current.aacplus != 0)
		end_block_size = 2048;
	if (ret == 0) {
		if (n >= 1) {
			switch (pcnt - (n - 1) * end_block_size) {
			case 1024:
				format_next.aacplus = 0;
				break;
			case 2048:
				format_next.aacplus = 1;
				break;
			default:
				ERR ("unknown block size");
				format_next.aacplus = 0;
				break;
			}
		}
		if (next_sampling_frequency_index >= 0 &&
		    next_sampling_frequency_index < 0xc)
			format_next.sampling_frequency = freqlist
				[format_next.aacplus]
				[next_sampling_frequency_index];
		else
			format_next.sampling_frequency = 0;
		if (format_next.sampling_frequency == 0)
			ERR ("sampling frequency error");
		switch (outInfo.channelMode) {
		case 0:		/* monaural */
			format_next.channel = 1;
			break;
		case 1:		/* stereo */
		case 2:		/* dual monaural */
		case 3:		/* parametric stereo */
			format_next.channel = 2;
			break;
		case 4:		/* 3/0 */
			format_next.channel = 3;
			break;
		case 5:		/* 3/1 */
			format_next.channel = 4;
			break;
		case 6:		/* 3/2 */
			format_next.channel = 5;
			break;
		case 7:		/* 3/2 + LFE (5.1) */
			format_next.channel = 6;
			break;
		case 8:		/* 2/1 */
		case 9:		/* 2/2 */
			ERR ("channel mode 2/1 or 2/2 not supported");
			format_next.channel = 0;
			break;
		case -1:
			ERR ("channel mode error");
			format_next.channel = 0;
			break;
		}
	}
	if (ret == -1)
		n++;		/* last block */
	if (state.first_block != 0 && n >= 1)
		n--;		/* first block */
	pthread_mutex_lock (&transfer_lock);
	output_remain_add += n * format_current.channel * end_block_size * 2;
	state.decode_end = 1;
	if (ret == -1 && decode_end_status == RSACPDS_ERR_DATA_EMPTY)
		state.decode_really_end = 1;
	if (transfer_flag != 0) {
		transfer_flag = 0;
		pthread_mutex_unlock (&transfer_done);
	}
	pthread_mutex_unlock (&transfer_lock);
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

	bufc = (uint8_t *)buf;
	*buflist = NULL;
	buf_free->next = NULL;
	buf_free->prevnext = &buf_free->next;
	pthread_mutex_init (&buf_free->lock, NULL);
	buf_used->next = NULL;
	buf_used->prevnext = &buf_used->next;
	pthread_mutex_init (&buf_used->lock, NULL);
	while (totalsize >= blocksize) {
		/* skip chunks which lie across BUFALIGNMENT boundary */
		if (BUFALIGNMENT - (addr % BUFALIGNMENT) >= blocksize) {
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

static long
decoder_close (int decoding)
{
	long err;

	if (decoding != 0 && RSACPDS_DecoderStop (paac) != 0)
		ERR ("RSACPDS_DecoderStop error");
	err = middleware_close ();
	return err;
}

static long
get_header_and_pce (long status)
{
	RSACPDS_AdtsHeader adtsheader;
	RSACPDS_AdifHeader adifheader;
	unsigned long bcnt;
	struct buflist *p;
	RSACPDS_PCE pce;

	switch (format_type) {
	case SPU_AAC_DECODE_SETFMT_TYPE_ADTS:
		if (state.first_block == 0 && (status & 0x10) != 0)
			break;
		if (RSACPDS_GetAdtsHeader (paac, &adtsheader, &bcnt)
		    < RSACPDS_RTN_GOOD) {
			ERR ("RSACPDS_GetAdtsHeader error");
			return -1;
		}
		next_sampling_frequency_index =
			adtsheader.sampling_frequency_index;
		break;
	case SPU_AAC_DECODE_SETFMT_TYPE_ADIF:
		if (state.first_block == 0)
			break;
		if (RSACPDS_GetAdifHeader (paac, &adifheader, &bcnt)
		    < RSACPDS_RTN_GOOD) {
			ERR ("RSACPDS_GetAdifHeader error");
			return -1;
		}
		/* No outbuf should be used before decoding the first block. */
		p = buflist_pop (&outbuf_free);
		if (p == NULL) {
			ERR ("No buffers available for GetPce");
			return -1;
		}
		if (sizeof pce > p->alen) {
			ERR ("Buffer is too small for GetPce");
			return -1;
		}
		if (RSACPDS_GetPce (paac, (RSACPDS_PCE *)p->addr)
		    < RSACPDS_RTN_GOOD) {
			ERR ("RSACPDS_GetPce error");
			return -1;
		}
		memcpy (&pce, p->buf, sizeof pce);
		buflist_add (&outbuf_free, p);
		next_sampling_frequency_index = pce.sampling_frequency_index;
		break;
	default:
		if (state.first_block == 0)
			break;
		if (RSACPDS_SetFormat (paac, sampling_frequency_index)
		    < RSACPDS_RTN_GOOD) {
			ERR ("RSACPDS_SetFormat error");
			return -1;
		}
		next_sampling_frequency_index = sampling_frequency_index;
		break;
	}
	return 0;
}

static int
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
		if (output_remain > 0 && copylen > output_remain)
			copylen = copylen > output_remain;
		memcpy (_dstbuf, (uint8_t *)outbuf_copying->buf +
			outbuf_copied, copylen);
		_dstbuf += copylen;
		_dstlen -= copylen;
		outbuf_copied += copylen;
		if (output_remain == 0)
			format_current = format_next;
		output_remain -= copylen;
		if (output_remain == 0)
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
		   int *pinbuf_added)
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
	}
	if (*srcbuf == (void *)_srcbuf && *srcbuf != srcend)
		return 0;
	*srcbuf = (void *)_srcbuf;
	return 1;
}

int
spu_aac_decode_init (void)
{
	if (state.init == 0) {
		if (init2 () < 0)
			return -1;
		state.init = 1;
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
	if (state.open != 0)
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
spu_aac_decode (void **destbuf, void *destend, void **srcbuf, void *srcend,
		struct spu_aac_decode_fmt *format)
{
	long status;
	int need_input, need_output;
	int inbuf_added;
	int endflag;
	int state_decode;
	int state_decode_end, state_decode_really_end;
	int incopy, outcopy;
	unsigned long nblk;
	long err = RSACPDS_RTN_GOOD;

once_again:
	need_input = 0;
	need_output = 0;
	inbuf_added = 0;
	if (state.init == 0) {
		if (init2 () < 0)
			return -1;
		state.init = 1;
	}
	pthread_mutex_lock (&transfer_lock);
	output_remain += output_remain_add;
	output_remain_add = 0;
	endflag = outbuf_end;
	state_decode_end = state.decode_end;
	state_decode_really_end = state.decode_really_end;
	transfer_flag = 1;
	pthread_mutex_unlock (&transfer_lock);

	outcopy = copy_output_buffer (destbuf, destend, &need_output);
	incopy = copy_input_buffer (srcbuf, srcend, &need_input, &inbuf_added);
	*format = format_current;

	if (state.open == 0) {
		state_decode = 0;
		state.decode_end = 0;
		state.decode_really_end = 0;
		state_decode_end = 0;
		state_decode_really_end = 0;
		callbk_err = 0;
		inbuf_end = 0;
		pthread_mutex_lock (&transfer_lock);
		outbuf_end = 0;
		pthread_mutex_unlock (&transfer_lock);
		if (inbuf_added == 0)
			goto ret;
		if ((err = middleware_open ()) < 0)
			goto ret;
		if ((err = RSACPDS_SetDecOpt(paac, 0)) < RSACPDS_RTN_GOOD) {
			ERR ("RSACPDS_SetDecOpt error");
			goto close_and_ret;
		}
		err = RSACPDS_RTN_GOOD;
		state.first_block = 1;
		state.open = 1;
		need_input = 1;
		need_output = 0;
		output_remain = 0;
		output_remain_add = 0;
	} else {
		state_decode = 1;
		if (state_decode_really_end != 0)
			state_decode = 0;
	}
	status = 0;
	if (state_decode_end != 0) {
		if (state.first_block != 0) {
			state.first_block = 0;
			need_output = 1;
		}
		if (output_remain <= 0) {
			state_decode = 0;
			state.decode_end = 0;
			err = RSACPDS_DecodeStatus (paac, &status);
			if (err < RSACPDS_RTN_GOOD) {
				ERR ("RSACPDS_DecodeStatus error");
				goto close_and_ret;
			}
		}
	}
	if (need_input != 0)
		stream_input_end_cb (0);
	if (need_output != 0)
		pcm_output_end_cb (0, 0);
	if (state_decode == 0 && state_decode_really_end == 0) {
		err = get_header_and_pce (status);
		if (err < 0)
			goto close_and_ret;
		nblk = 0;
		if (state.first_block != 0)
			nblk = 1;
		if (RSACPDS_Decode (paac, nblk) < RSACPDS_RTN_GOOD) {
			err = RSACPDS_GetStatusCode (paac);
			ERR ("RSACPDS_Decode error");
			goto close_and_ret;
		}
		state_decode = 1;
	}
	if (inbuf_end == 2 && endflag != 0 && outbuf_copying == NULL &&
	    buflist_poll (&outbuf_used) == NULL) {
		if (decode_end_status != RSACPDS_ERR_DATA_EMPTY)
			ERR ("strange statusCode; ignored");
		err = decoder_close (0);
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
	if (state.decode_end != 0 || state.decode_really_end != 0)
		state_decode = 0;
	decoder_close (state_decode);
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
spu_aac_decode_stop (void)
{
	long err = RSACPDS_RTN_GOOD;
	if (state.open != 0) {
		err = decoder_close (1);
		state.open = 0;
	}
	return err;
}

long
spu_aac_decode_deinit (void)
{
	long err = RSACPDS_RTN_GOOD;
	if (state.init != 0) {
		err = spu_aac_decode_stop ();
		RSACPDS_Quit ();
		spu_deinit ();
		buflist_free (&inbuflist);
		buflist_free (&outbuflist);
		state.init = 0;
	}
	return err;
}
