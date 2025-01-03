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
 */
#include <stdint.h>
#include <string.h>
#include "uzlib.h"

#if 0
#define HASH_BITS 12
#else
#define HASH_BITS data->hash_bits
#endif

#define HASH_SIZE (1<<HASH_BITS)

/* Minimum and maximum length of matches to look for, inclusive */
#define MIN_MATCH 3
#define MAX_MATCH 258

/* Max offset of the match to look for, inclusive */
#if 0
#define MAX_OFFSET 32768
#else
#define MAX_OFFSET data->dict_size
#endif

/* Hash function can be defined as macro or as inline function */

/*#define HASH(p) (p[0] + p[1] + p[2])*/

/* This is hash function from liblzf */
static inline int HASH(struct uzlib_comp *data, const uint8_t *p) {
    int v = (p[0] << 16) | (p[1] << 8) | p[2];
    int hash = ((v >> (3*8 - HASH_BITS)) - v) & (HASH_SIZE - 1);
    return hash;
}

#ifdef DUMP_LZTXT

#include <stdio.h>

/* Counter for approximate compressed length in LZTXT mode. */
/* Literal is counted as 1, copy as 2 bytes. */
unsigned approx_compressed_len;

void literal(void *data, uint8_t val)
{
    printf("L%02x # %c\n", val, (val >= 0x20 && val <= 0x7e) ? val : '?');
    approx_compressed_len++;
}

void copy(void *data, unsigned offset, unsigned len)
{
    printf("C-%u,%u\n", offset, len);
    approx_compressed_len += 2;
}

#else

static inline void literal(void *data, uint8_t val)
{
    zlib_literal(data, val);
}

static inline void copy(void *data, unsigned offset, unsigned len)
{
    zlib_match(data, offset, len);
}

#endif


// pointer increment with callback for crc
const uint8_t *uzlib_walk_buf(struct uzlib_comp *data, const uint8_t *buf, size_t bytes)
{
    if( data->checksum_cb )
        data->checksum = data->checksum_cb(buf, bytes, data->checksum);

    buf += bytes;
    data->readOffset += bytes;
    return buf;
}


void uzlib_align_buffer(struct uzlib_comp *data, const uint8_t *buf)
{
    // if( data->bufferPosition + MAX_OFFSET > data->bufferSize )
    {
        // move buf[data->bufferPosition] to buf[0]
        // read (data->bufferPosition - data->bufferEnd) bytes
        // append bytes at buf[data->bufferPosition]
        // data->bufferPosition = 0;
        // *buf =
    }
}

void uzlib_compress(struct uzlib_comp *data, const uint8_t *src, unsigned slen)
{
    data->readOffset = 0;
    size_t readEnd = slen - MIN_MATCH;

    if( data->progress )
        data->progress(0, slen);

    while( data->readOffset < readEnd )
    {
        if( data->progress )
            data->progress(data->readOffset, slen);

        if( data->readSourceBytes )
            uzlib_align_buffer( data, src );

        int h = HASH(data, src);
        const uint8_t **bucket = &data->hash_table[h & (HASH_SIZE - 1)];
        const uint8_t *subs = *bucket;
        *bucket = src;

        if (subs && src > subs && (src - subs) <= MAX_OFFSET && !memcmp(src, subs, MIN_MATCH)) {
            src = uzlib_walk_buf(data,src,MIN_MATCH);
            const uint8_t *m = subs + MIN_MATCH;
            int len = MIN_MATCH;
            while (*src == *m && len < MAX_MATCH && data->readOffset < readEnd /*src < top*/) {
                src = uzlib_walk_buf(data,src,1);
                m++; len++;
            }
            copy(data, src - len - subs, len);
        } else {
            literal(data, *src);
            src = uzlib_walk_buf(data,src,1);
        }
    }
    // Process buffer tail, which is less than MIN_MATCH
    // (and so it doesn't make sense to look for matches there)
    readEnd += MIN_MATCH;
    while( data->readOffset < readEnd )
    {
        literal(data, *src);
        src = uzlib_walk_buf(data,src,1);
    }

    if( data->progress )
        data->progress(slen, slen);
}






// // Callback function to read the next byte from the file
// int file_read_cb(uzlib_stream_reader *reader, size_t num_bytes)
// {
//     // Move the data in the buffer to make space for new data
//     if (reader->pos >= reader->slen) {
//         return -1;
//     }
//
//     // Read the next byte from the file
//     int next_byte = 1; // stream->read();
//     if (next_byte != EOF) {
//         reader->buf[reader->slen++] = (uint8_t)next_byte;
//         return next_byte;
//     }
//     return -1; // End of file
// }

int read_stream( struct uzlib_comp *data)
{
    if (!data->readSourceBytes) {
        printf("ERROR: no stream reader callback attached\n");
        return -1;
    }

    struct uzlib_stream_reader *reader = data->streamReader;
    if(reader->pos > 0 && reader->bpos + MAX_MATCH + 1 < reader->buflen ) {
        //printf("Stream buffer hit at pos %d (bpos %d)\n", reader->pos, reader->bpos);
        int ret = reader->buf[reader->pos];
        reader->pos++;
        reader->bpos++;
        return ret;
    }

    //printf("Stream read hit at pos %d (bpos %d) %d+%d > %d  \n", reader->pos, reader->bpos, reader->bpos, MAX_MATCH, reader->buflen);
    uint8_t* src_buf = reader->bufnum==0?reader->buf1:reader->buf2;
    uint8_t* dst_buf = reader->bufnum==0?reader->buf2:reader->buf1;
    size_t bytes_to_read = reader->buflen;
    size_t bytes_read = 0;

    if( reader->pos > 0 ) { // first read occured
        if( reader->bpos > 0 ) { // some data chunk remains
            char a[5] = { reader->buf[reader->bpos-4], reader->buf[reader->bpos-3], reader->buf[reader->bpos-2], reader->buf[reader->bpos-1], 0};
            printf("\n**** Swapping buffers **** (last char='%s')\n\n", a);
            size_t chunk_len = reader->buflen - (reader->bpos); // leftover unprocessed data
            bytes_to_read = reader->buflen - (chunk_len+1); // bytes to read to fill the buffer (include the last byte too)
            memcpy(dst_buf, &src_buf[reader->bpos-1], chunk_len+1); // move leftover unprocessed data to other buffer
            bytes_read = data->readSourceBytes(data, &dst_buf[chunk_len+1], bytes_to_read);
            printf("\n**** read %d bytes at offset %d****\n\n", bytes_read, chunk_len+1);
            reader->bpos = 1; // reset buffer pos
            // swap buffers
            *reader->buf = *dst_buf;
            reader->bufnum = 1-reader->bufnum;
        } else {
            printf("reader->bpos at zero, halting\n");
            while(1);
        }
    } else {
        printf("reader->bpos at zero\n");
        bytes_read = data->readSourceBytes(data, src_buf, bytes_to_read);
        printf("\n**** read %d bytes at offset %d****\n\n", bytes_read, reader->bpos);
        *reader->buf = *src_buf;
        reader->bufnum = 0;

    }



    if( bytes_read>0 ) {
        if( data->checksum_cb ) {
            data->checksum = data->checksum_cb(&reader->buf[reader->bpos], bytes_read, data->checksum);
        }

        int ret = reader->buf[reader->bpos];

        reader->pos++;
        reader->bpos++;

        return ret;
    }

    printf("END OF STREAM");
    while(1);

    // int next_byte = reader->read_cb(data, len_remaining);
    // if (next_byte != -1) {
    //     //reader->buf[reader->pos++] = (uint8_t)next_byte;
    //     //reader->slen++;
    //     reader->pos++;
    //     reader->bpos++;
    //     return next_byte;
    // }

    return -1; // End of stream
}


int uzlib_stream_get_next_byte( struct uzlib_comp *data, unsigned char* current_byte )
{
    // printf("Entered uzlib_stream_compress\n");
    int next_byte = read_stream(data);
    *current_byte = next_byte;
    //printf("got next byte: 0x%02x\n", (uint8_t)next_byte);

    char a[2] = {next_byte,0};
    printf("%s", a);

    return next_byte;
}


void uzlib_stream_compress(struct uzlib_comp *data)
{
    unsigned char current_byte;
    struct uzlib_stream_reader *reader = data->streamReader;

    printf("Entered uzlib_stream_compress\n");

    //while ((current_byte = read_stream(data, reader)) != -1)
    while( uzlib_stream_get_next_byte(data, &current_byte) != -1 )
    {
        int h = HASH(data, &reader->buf[reader->bpos - 1]);
        const uint8_t **bucket = &data->hash_table[h & (HASH_SIZE - 1)];
        const uint8_t *subs = *bucket;
        *bucket = reader->buf + reader->bpos - 1;
        if (subs && (reader->buf + reader->bpos - 1) > subs && ((reader->buf + reader->bpos - 1) - subs) <= MAX_OFFSET &&
            !memcmp(reader->buf + reader->bpos - 1, subs, MIN_MATCH)) {
            reader->bpos += MIN_MATCH - 1;
            const uint8_t *m = subs + MIN_MATCH;
            int len = MIN_MATCH;
            //while ((current_byte = read_stream(data, reader)) != -1 && *m == current_byte && len < MAX_MATCH)
            while( uzlib_stream_get_next_byte(data, &current_byte) != -1 && *m == current_byte && len < MAX_MATCH)
            {
                m++;
                len++;
            }
            // printf("zlib match len %d at bpos %d \n", len, reader->bpos );
            //copy(data, (reader->buf + reader->bpos - len - 1) - subs, len);
            copy(data, &reader->buf[reader->bpos-1] - len - subs, len);
        } else {
            literal(data, current_byte);
        }
    }
}


