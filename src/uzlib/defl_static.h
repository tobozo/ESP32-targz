/*
 * Copyright (c) uzlib authors
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

/* This files contains type declaration and prototypes for defl_static.c.
   They may be altered/distinct from the originals used in PuTTY source
   code.


* Edited by Tobozo for ESP32-targz
  - Added byteWriter to outbits()
  - Added out4bytes(), with byteWriter support
  - Added zlib_next_block() and zlib_empty_block() for streamed input

*/

void outbits(struct uzlib_comp *ctx, unsigned long bits, int nbits);
void out4bytes( struct uzlib_comp *out, unsigned char st, unsigned char nd, unsigned char rd, unsigned char th );
void zlib_start_block(struct uzlib_comp *ctx);
void zlib_finish_block(struct uzlib_comp *ctx);
void zlib_literal(struct uzlib_comp *ctx, unsigned char c);
void zlib_match(struct uzlib_comp *ctx, int distance, int len);

void zlib_next_block(struct uzlib_comp *out);
void zlib_empty_block(struct uzlib_comp *out);
