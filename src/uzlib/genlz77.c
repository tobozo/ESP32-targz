/*
 * genlz77  -  Generic LZ77 compressor
 *
 * Copyright (c) 2014 by Paul Sokolovsky
 *
 * This software is provided 'as-is', without any express
 * or implied warranty.  In no event will the authors be
 * held liable for any damages arising from the use of
 * this software.
 *
 * Permission is granted to anyone to use this software
 * for any purpose, including commercial applications,
 * and to alter it and redistribute it freely, subject to
 * the following restrictions:
 *
 * 1. The origin of this software must not be
 *    misrepresented; you must not claim that you
 *    wrote the original software. If you use this
 *    software in a product, an acknowledgment in
 *    the product documentation would be appreciated
 *    but is not required.
 *
 * 2. Altered source versions must be plainly marked
 *    as such, and must not be misrepresented as
 *    being the original software.
 *
 * 3. This notice may not be removed or altered from
 *    any source distribution.
 *
 *
 * Edited by Tobozo for ESP32-targz
 *  - Added uzlib_checksum_none()
 *  - Added uzlib_deflate_init_stream()
 *  - Added uzlib_deflate_stream()
 *
 */
#include <stdint.h>
#include <string.h>
#include "uzlib.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"

#define HASH_BITS data->hash_bits
#define HASH_SIZE (1<<HASH_BITS)

/* Minimum and maximum length of matches to look for, inclusive */
#define MIN_MATCH 3
#define MAX_MATCH 258

/* Max offset of the match to look for, inclusive */
#define MAX_OFFSET data->dict_size

/* Hash function can be defined as macro or as inline function */

/*#define HASH(p) (p[0] + p[1] + p[2])*/

/* This is hash function from liblzf */
static inline int HASH(struct uzlib_comp *data, const uint8_t *p) {
    int v = (p[0] << 16) | (p[1] << 8) | p[2];
    int hash = ((v >> (3*8 - HASH_BITS)) - v) & (HASH_SIZE - 1);
    return hash;
}


static inline void literal(void *data, uint8_t val)
{
    zlib_literal(data, val);
}

static inline void copy(void *data, unsigned offset, unsigned len)
{
    zlib_match(data, offset, len);
}


// used only when uzlib_compress is in buffer mode (when in stream mode, progress_cb is managed from outside)
#define UZLIB_PROGRESS(b,t) if( data->is_stream == 0 && data->progress_cb ) data->progress_cb(b, t);

void uzlib_compress(struct uzlib_comp *data, const uint8_t *src, unsigned slen)
{
    UZLIB_PROGRESS(0,slen);

    const uint8_t *top = src + slen - MIN_MATCH;
    while (src < top) {
        UZLIB_PROGRESS( slen-(top-src), slen);
        int h = HASH(data, src);
        const uint8_t **bucket = &data->hash_table[h & (HASH_SIZE - 1)];
        const uint8_t *subs = *bucket;
        *bucket = src;
        if (subs && src > subs && (src - subs) <= MAX_OFFSET && !memcmp(src, subs, MIN_MATCH)) {
            src += MIN_MATCH;
            const uint8_t *m = subs + MIN_MATCH;
            int len = MIN_MATCH;
            while (*src == *m && len < MAX_MATCH && src < top) {
                src++; m++; len++;
            }
            copy(data, src - len - subs, len);
        } else {
            literal(data, *src++);
        }
    }
    // Process buffer tail, which is less than MIN_MATCH
    // (and so it doesn't make sense to look for matches there)
    top += MIN_MATCH;
    while (src < top) {
        literal(data, *src++);
    }
    UZLIB_PROGRESS( slen, slen);
}



uint32_t uzlib_checksum_none(__attribute__((unused)) const void *data, __attribute__((unused)) unsigned int length, uint32_t prev_sum)
{
  return prev_sum;
}



int uzlib_deflate_init_stream(struct uzlib_comp* ctx, uzlib_stream* uzstream){
    if (uzstream == Z_NULL)
        return Z_STREAM_ERROR;
    if (ctx == Z_NULL)
        return Z_MEM_ERROR;
    if( ctx->hash_table == NULL )
        return Z_MEM_ERROR;
    ctx->comp_disabled = 0;

    switch( ctx->checksum_type ) {
      case TINF_CHKSUM_CRC:
        ctx->checksum_cb = uzlib_crc32;
        ctx->checksum    = ~0;
        break;
      case TINF_CHKSUM_ADLER:
        ctx->checksum_cb = uzlib_adler32;
        ctx->checksum    = 1;
        break;
      case TINF_CHKSUM_NONE:
      default:
        ctx->checksum_cb = uzlib_checksum_none;
        ctx->checksum      = 0;
        break;
    }

    ctx->is_stream = 1; // progress_cb is triggered from outside

    uzstream->ctx = ctx;

    if( ctx->progress_cb )
        ctx->progress_cb(0,ctx->slen);

    return Z_OK;
}



int uzlib_deflate_stream(struct uzlib_stream* uzstream, int flush){
    struct uzlib_comp* ctx = uzstream->ctx;

    // some data is still pending in the output buffer
    if(ctx->outbuf != NULL) {
        if(uzstream->out.avail < ctx->outlen){
            memcpy(uzstream->out.next, ctx->outbuf, uzstream->out.avail);
            ctx->outbuf  += uzstream->out.avail;
            ctx->outlen  -= uzstream->out.avail;
            uzstream->out.next  += uzstream->out.avail;
            uzstream->out.total += uzstream->out.avail;
            uzstream->out.avail = 0;
            return Z_OK;
        }

        memcpy(uzstream->out.next, ctx->outbuf, ctx->outlen);

        ctx->outbuf -= uzstream->out.total;

        uzstream->out.avail -= ctx->outlen;
        uzstream->out.next  += ctx->outlen;
        uzstream->out.total += ctx->outlen;

        free((void*)ctx->outbuf);
        ctx->outbuf = NULL;
        if(uzstream->in.avail == 0) return Z_OK;
    }

    if(uzstream->in.avail == 0 && flush != Z_FINISH)
        return Z_OK;

    ctx->outlen   = 0;
    ctx->outsize  = 0;
    ctx->outbits  = 0;
    ctx->noutbits = 0;

    ctx->checksum = ctx->checksum_cb(uzstream->in.next, uzstream->in.avail, ctx->checksum);

    if(flush != Z_FINISH){
        zlib_next_block(ctx);
        uzlib_compress(ctx, uzstream->in.next, uzstream->in.avail);
        zlib_empty_block(ctx);
    } else {
        zlib_start_block(ctx);
        uzlib_compress(ctx, uzstream->in.next, uzstream->in.avail);
        zlib_finish_block(ctx);
        ctx->comp_disabled = 1;
    }

    uzstream->in.total += uzstream->in.avail;

    uzstream->in.next += uzstream->in.avail;
    uzstream->in.avail = 0;

    // output buffer too small for output ?
    if(uzstream->out.avail < ctx->outlen){
        if(flush == Z_FINISH) {
            free((void*)ctx->outbuf);
            ctx->outbuf = NULL;
            printf("gz buffer ERROR\n");
            return Z_BUF_ERROR;
        }
        memcpy(uzstream->out.next, ctx->outbuf, uzstream->out.avail);
        ctx->outbuf += uzstream->out.avail;
        ctx->outlen -= uzstream->out.avail;
        uzstream->out.next += uzstream->out.avail;
        uzstream->out.total = uzstream->out.avail;
        uzstream->out.avail = 0;
        return Z_OK;
    }

    memcpy(uzstream->out.next, ctx->outbuf, ctx->outlen);

    if(flush == Z_FINISH) {
        uzstream->out.total  = ctx->outlen;
    } else {
        uzstream->out.total += ctx->outlen;
    }

    uzstream->out.avail -= ctx->outlen;
    uzstream->out.next  += ctx->outlen;

    free((void*)ctx->outbuf);
    ctx->outbuf = NULL;

    if( ctx->progress_cb && uzstream->in.total<=ctx->slen)
        ctx->progress_cb(uzstream->in.total, ctx->slen);

    return flush == Z_FINISH ? Z_STREAM_END : Z_OK;
}


#pragma GCC diagnostic pop

