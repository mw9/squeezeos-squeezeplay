/*
** Copyright 2007-2008 Logitech. All Rights Reserved.
**
** This file is subject to the Logitech Public Source License Version 1.0. Please see the LICENCE file for details.
*/

#define RUNTIME_DEBUG 1

#include "common.h"

#include "audio/streambuf.h"
#include "audio/decode/decode.h"
#include "audio/decode/decode_priv.h"

#include "FLAC/stream_decoder.h"


#define BLOCKSIZE 4096

struct decode_flac {
	FLAC__StreamDecoder *decoder;

	int sample_rate;
	bool_t error_occurred;
};



static FLAC__StreamDecoderReadStatus decode_flac_read_callback(
	const FLAC__StreamDecoder *decoder,
	FLAC__byte buffer[],
	size_t *bytes,
	void *data) {

	struct decode_flac *self = (struct decode_flac *) data;
	const size_t requested_bytes = *bytes;
	bool_t streaming;

	if (self->error_occurred) {
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
	}

	if (!requested_bytes) {
		/* abort to avoid deadlock */
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
	}

	*bytes = streambuf_read(buffer, 0, requested_bytes, &streaming);
	if (*bytes == 0) {
		current_decoder_state |= DECODE_STATE_UNDERRUN;

		if (!streaming) {
			return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		}
	}
	else {
		current_decoder_state &= ~DECODE_STATE_UNDERRUN;
	}

	return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}


static FLAC__StreamDecoderWriteStatus decode_flac_write_callback(
	const FLAC__StreamDecoder *decoder,
	const FLAC__Frame *frame,
	const FLAC__int32 *const buffer[],
	void *data) {

	struct decode_flac *self = (struct decode_flac *) data;
	const FLAC__int32 *lptr, *rptr;
	sample_t *sbuf, *sptr;
	int i;

	if (self->error_occurred) {
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}

	self->sample_rate = frame->header.sample_rate;

	lptr = buffer[0];
	rptr = buffer[1];
	sbuf = sptr = malloc(sizeof(sample_t) * 2 * frame->header.blocksize);

	/* Scale samples, and copy if we have mono input */
	if (frame->header.channels == 1) {
		if (frame->header.bits_per_sample == 16) {
			for (i=0; i<frame->header.blocksize; i++) {
				FLAC__int32 s = *lptr++ << 16;
				*sptr++ = s;
				*sptr++ = s;
			}
		}
		else /* bits_per_sample == 24 */ {
			for (i=0; i<frame->header.blocksize; i++) {
				FLAC__int32 s = *lptr++ << 8;
				*sptr++ = s;
				*sptr++ = s;
			}
		}
	}
	else {
		if (frame->header.bits_per_sample == 16) {
			for (i=0; i<frame->header.blocksize; i++) {
				*sptr++ = *lptr++ << 16;
				*sptr++ = *rptr++ << 16;
			}
		}
		else /* bits_per_sample == 24 */ {
			for (i=0; i<frame->header.blocksize; i++) {
				*sptr++ = *lptr++ << 8;
				*sptr++ = *rptr++ << 8;
			}
		}
	}

	decode_output_samples(sbuf,
			      frame->header.blocksize,
			      frame->header.sample_rate,
			      FALSE,
			      TRUE,
			      FALSE);

	free(sbuf);


	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}


static void decode_flac_metadata_callback(
	const FLAC__StreamDecoder *decoder,
	const FLAC__StreamMetadata *metadata,
	void *data) {

	struct decode_flac *self = (struct decode_flac *) data;

	if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
		self->sample_rate = metadata->data.stream_info.sample_rate;
	}
}


static void decode_flac_error_callback(
	const FLAC__StreamDecoder *decoder,
	const FLAC__StreamDecoderErrorStatus status,
	void *data) {

	DEBUG_TRACE("FLAC error: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
}


static bool_t decode_flac_callback(void *data) {
	struct decode_flac *self = (struct decode_flac *) data;
	FLAC__StreamDecoderState state;

	if (current_decoder_state & DECODE_STATE_ERROR) {
		return FALSE;
	}

	if (! decode_output_can_write(2 * 4608 * sizeof(sample_t), self->sample_rate)) {
		return FALSE;
	}

	FLAC__stream_decoder_process_single(self->decoder);

	state = FLAC__stream_decoder_get_state(self->decoder);
	if (state == FLAC__STREAM_DECODER_ABORTED ||
	    state == FLAC__STREAM_DECODER_END_OF_STREAM) {
		DEBUG_TRACE("FLAC error: %s", FLAC__StreamDecoderStateString[state]);
		current_decoder_state |= DECODE_STATE_ERROR;
	}

	return TRUE;
}		


static u32_t decode_flac_period(void *data) {
	struct decode_flac *self = (struct decode_flac *) data;

	if (self->sample_rate <= 48000) {
		return 8;
	}
	else {
		return 4;
	}
}


static void *decode_flac_start(u8_t *params, u32_t num_params) {
	struct decode_flac *self;

	DEBUG_TRACE("decode_flac_start()");

	self = malloc(sizeof(struct decode_flac));
	memset(self, 0, sizeof(struct decode_flac));

	self->decoder = FLAC__stream_decoder_new();
	// XXXX error handling

	FLAC__stream_decoder_init_stream(
		self->decoder,
		decode_flac_read_callback,
		NULL, /* seek_callback */
		NULL, /* tell_callback */
		NULL, /* length_callback */
		NULL, /* eof_callback */
		decode_flac_write_callback,
		decode_flac_metadata_callback,
		decode_flac_error_callback,
		self
		);
	// XXXX error handling

	/* Assume we aren't changing sample rates until proven wrong */
	self->sample_rate = decode_output_samplerate();
	self->error_occurred = FALSE;
	
	// XXXX this was needed for SB, why?
	//FLAC__stream_decoder_process_until_end_of_metadata(self->decoder);

	return self;
}


static void decode_flac_stop(void *data) {
	struct decode_flac *self = (struct decode_flac *) data;

	DEBUG_TRACE("decode_flac_stop()");

	if (self->decoder) {
		FLAC__stream_decoder_delete(self->decoder);
		self->decoder = NULL;
	}
	
	free(self);
}


struct decode_module decode_flac = {
	'f',
	decode_flac_start,
	decode_flac_stop,
	decode_flac_period,
	decode_flac_callback,
};
