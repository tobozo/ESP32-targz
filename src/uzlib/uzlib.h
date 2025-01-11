/*
 * uzlib  -  tiny deflate/inflate library (deflate, gzip, zlib)
 *
 * Copyright (c) 2003 by Joergen Ibsen / Jibz
 * All Rights Reserved
 * http://www.ibsensoftware.com/
 *
 * Copyright (c) 2014-2018 by Paul Sokolovsky
 *
 * Edited by Tobozo for ESP32-targz
 *  - Added logging to inflate
 *  - Added stream support to deflate
 *
 */

#ifndef UZLIB_H_INCLUDED
#define UZLIB_H_INCLUDED

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>


/* calling convention */
#ifndef TINFCC
 #ifdef __WATCOMC__
  #define TINFCC __cdecl
 #else
  #define TINFCC
 #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "uzlib_conf.h"


/* ok status, more data produced */
#define TINF_OK             0
/* end of compressed stream reached */
#define TINF_DONE           1
#define TINF_DATA_ERROR    (-3)
#define TINF_CHKSUM_ERROR  (-4)
#define TINF_DICT_ERROR    (-5)

/* checksum types */
#define TINF_CHKSUM_NONE  0
#define TINF_CHKSUM_ADLER 1
#define TINF_CHKSUM_CRC   2

/* helper macros */
#define TINF_ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*(arr)))

#define TINF_CHKSUM_TYPE(t) ( t==TINF_CHKSUM_CRC?"crc32":(t==TINF_CHKSUM_ADLER?"adler32":"none") )


/* data structures */

typedef struct {
   unsigned short table[16];  /* table of code length counts */
   unsigned short trans[288]; /* code -> symbol translation table */
} TINF_TREE;

struct TINF_DATA;
typedef struct TINF_DATA  {
    /* Pointer to the next byte in the input buffer */
    const unsigned char *source;
    /* Pointer to the next byte past the input buffer (source_limit = source + len) */
    const unsigned char *source_limit;
    /* If source_limit == NULL, or source >= source_limit, this function
       will be used to read next byte from source stream. The function may
       also return -1 in case of EOF (or irrecoverable error). Note that
       besides returning the next byte, it may also update source and
       source_limit fields, thus allowing for buffered operation. */
    int (*source_read_cb)(struct TINF_DATA *uncomp);
    unsigned int (*readSourceByte)(struct TINF_DATA *data, unsigned char *out);
    //unsigned int readSourceErrors = 0;
    unsigned int readSourceErrors;

    void (*log)( const char* format, ... );

    unsigned int tag;
    unsigned int bitcount;

    /* Destination (output) buffer start */
    //unsigned char *dest_start;
    /* Current pointer in dest buffer */
    //unsigned char *dest;
    /* Pointer past the end of the dest buffer, similar to source_limit */
    //unsigned char *dest_limit;

    /* Buffer start */
    unsigned char *destStart;
    /* Buffer total size */
    unsigned int destSize;
    /* Current pointer in buffer */
    unsigned char *dest;
    /* Remaining bytes in buffer */
    unsigned int destRemaining;

    /* if readDest is provided, it will use this function for
       reading from the output stream, rather than assuming
       'dest' contains the entire output stream in memory
    */
    unsigned int (*readDestByte)(int offset, unsigned char *out);
    unsigned int (*writeDestWord)(unsigned long data);

    /* Accumulating checksum */
    unsigned int checksum;
    char checksum_type;
    bool eof;

    int btype;
    int bfinal;
    unsigned int curlen;
    int lzOff;
    unsigned char *dict_ring;
    unsigned int dict_size;
    unsigned int dict_idx;

    TINF_TREE ltree; /* dynamic length/symbol tree */
    TINF_TREE dtree; /* dynamic distance tree */
} TINF_DATA;


#define TINF_PUT(d, c) \
    { \
        *d->dest++ = c; \
        if (d->dict_ring) { d->dict_ring[d->dict_idx++] = c; if (d->dict_idx == d->dict_size) d->dict_idx = 0; } \
    }

unsigned char TINFCC uzlib_get_byte(TINF_DATA *d);

/* Decompression API */

void TINFCC uzlib_init(void);
void TINFCC uzlib_uncompress_init(TINF_DATA *d, void *dict, unsigned int dictLen);
int  TINFCC uzlib_uncompress(TINF_DATA *d);
int  TINFCC uzlib_uncompress_chksum(TINF_DATA *d);

int TINFCC uzlib_zlib_parse_header(TINF_DATA *d);
int TINFCC uzlib_gzip_parse_header(TINF_DATA *d);

/* Compression API */

// some defines from zlib used for state or error
#define Z_NO_FLUSH      0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH    2
#define Z_FULL_FLUSH    3
#define Z_FINISH        4
#define Z_BLOCK         5
#define Z_TREES         6
#define Z_NULL          0
#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO        (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)

typedef const uint8_t *uzlib_hash_entry_t;


struct uzlib_comp {
    unsigned char *outbuf;
    int outlen, outsize;
    unsigned long outbits;
    int noutbits;
    int comp_disabled;

    uzlib_hash_entry_t *hash_table;
    unsigned int hash_bits;
    unsigned int dict_size;

    // additional (non uzlib) changes:
    size_t slen; // input size uncompressed
    uint32_t checksum;

    // uzlib_crc32() or uzlib_adler32() will be attached here
    uint32_t (*checksum_cb)(const void *data, unsigned int length, uint32_t checksum);
    // optional progress user callback
    void (*progress_cb)( size_t progress, size_t total );
    // output stream byte writer
    unsigned int (*writeDestByte)(struct uzlib_comp *data, unsigned char byte);

    char checksum_type; // crc32 or adler32
    char grow_buffer; // 1 = enables realloc() in outbits() and out4bytes() functions
    char is_stream; // 1 = disables calling progress_cb() from uzlib_compress()
    unsigned char reserved[1];

};


typedef struct uzlib_pipe {
    unsigned char* next;
    unsigned int   avail;
    unsigned long  total;
} uzlib_pipe;

// stream to stream decompression
typedef struct uzlib_stream {
    uzlib_pipe in;
    uzlib_pipe out;
    struct uzlib_comp* ctx;

} uzlib_stream;


void TINFCC uzlib_compress(struct uzlib_comp *c, const uint8_t *src, unsigned slen);
int TINFCC uzlib_deflate_init_stream(struct uzlib_comp* ctx, uzlib_stream* strm);
int TINFCC uzlib_deflate_stream(struct uzlib_stream* strm, int flush);

#include "defl_static.h"

/* Checksum API */

/* prev_sum is previous value for incremental computation, 1 initially */
uint32_t TINFCC uzlib_adler32(const void *data, unsigned int length, uint32_t prev_sum);
/* crc is previous value for incremental computation, 0xffffffff initially */
uint32_t TINFCC uzlib_crc32(const void *data, unsigned int length, uint32_t crc);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UZLIB_H_INCLUDED */
