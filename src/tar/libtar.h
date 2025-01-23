/*\

  MIT License

  Copyright (c) 2025-now tobozo

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  ESP32-tgz is a wrapper to uzlib.h and untar.h third party libraries.
  Those libraries have been adapted and/or modified to fit this project's needs
  and are bundled with their initial license files.

  - uzlib: https://github.com/pfalcon/uzlib
  - untar: https://github.com/dsoprea/TinyUntar
  - libtar (some functions inspired by): https://repo.or.cz/libtar.git

\*/


#pragma once

#include <sys/stat.h> // needed for mode_t and stat
#include <stdint.h>
#include <tar.h> // import some tar defines from espressif sdk

// #define DEBUG

#ifdef __cplusplus
extern "C"
{
#endif


/* useful constants */
const uint32_t T_BLOCKSIZE = 512;
#define T_NAMELEN   100
#define T_PREFIXLEN 155
#define T_MAXPATHLEN (T_NAMELEN + T_PREFIXLEN)
#define MAXPATHLEN T_MAXPATHLEN // mock #include <sys/param.h> but reduce from 1024 to 255

/* our version of the tar header structure */
struct tar_header
{
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[12];
  char mtime[12];
  char chksum[8];
  char typeflag;
  char linkname[100];
  char magic[6];
  char version[2];
  char uname[32];
  char gname[32];
  char devmajor[8];
  char devminor[8];
  char prefix[155];
  char padding[12];
};


typedef void * (*openfunc_t)(void *fs, const char *filename, const char *mode);
typedef ssize_t (*readfunc_t)(void *fs, void *file, void * buf, size_t count);
typedef int (*statfunc_t)(void *fs, const char*path, void *sptr);

typedef ssize_t (*writefunc_t)(void *fs, void *file, void * buf, size_t count);
typedef int (*closefunc_t)(void *fs, void *file);
typedef ssize_t (*closewritefunc_t)(void *fs, void *file);

// i/o callbacks
typedef struct {
  openfunc_t       openfunc;
  closefunc_t      closefunc;
  readfunc_t       readfunc;
  writefunc_t      writefunc;
  closewritefunc_t closewritefunc;
  statfunc_t       statfunc;
} tar_callback_t;


// main TAR structure
typedef struct {
  tar_callback_t *io;
  char *pathname;
  void *fd;
  struct tar_header th_buf;
  void *opaque;
} TAR;


// state machine for main loop
typedef enum {
  TAR_MAIN_HEADER,
  TAR_ENT_HEADER,
  TAR_ENT_BLOCK,
  TAR_ENT_FOOTER,
  TAR_MAIN_FOOTER,
  FINISHED
} tar_step_t;

// state machine for dir entities
typedef enum {
  ENTITY_STAT,
  ENTITY_HEADER,
  ENTITY_REGFILE,
  ENTITY_REGFILE_STEP,
  ENTITY_END
} entity_steps_t;

// state holder for dir entities state machine
typedef struct entity_t
{
  TAR *tar;
  const char *realname;
  const char *savename;
  ssize_t *written_bytes;
  entity_steps_t step;
  int step_rv; // return value
} entity_t;


// helpers

// calculate header checksum
int th_crc_calc(TAR *t);

// string-octal to integer conversion
int oct_to_int(char *oct);

// integer to NULL-terminated string-octal conversion
#define int_to_oct(num, oct, octlen) snprintf((oct), (octlen), "%*lo ", (octlen) - 2, (unsigned long)(num))

// integer to string-octal conversion, no NULL
void int_to_oct_nonull(int num, char *oct, size_t octlen);


// open new tar instance
int tar_open(TAR *t, const char *pathname, tar_callback_t *io, const char *mode, void *opaque);
// set tar info and allocate block memory
int tar_init(TAR *t, const char *pathname, tar_callback_t *io, void *opaque);

// close tar handle and release block memory
int tar_close(TAR *t);

// Appends a dir entity (file or folder) to the tar archive
int tar_append_entity(entity_t *entity);
int tar_append_entity_step(entity_t *entity); // state machine stepper

// encode entity header block (minus the path)
void th_set_from_stat(TAR *t, struct stat *s);

// encode file path
void th_set_path(TAR *t, const char *pathname);

// write tar header
int th_write(TAR *t, ssize_t *written_bytes);

// add file contents to a tar archive
int tar_append_regfile(TAR *t, const char *realname, ssize_t *written_bytes);

// write EOF indicator
int tar_append_eof(TAR *t, ssize_t *written_bytes);

// determine file type
int th_is_regfile(TAR *t);
int th_is_dir(TAR *t);

// encode file info in tar header
void th_set_type(TAR *t, mode_t mode);
void th_set_user(TAR *t, uid_t uid);
void th_set_group(TAR *t, gid_t gid);
void th_set_mode(TAR *t, mode_t fmode);
void th_set_mtime(TAR* t, time_t fmtime);
void th_set_size(TAR* t, ssize_t fsize);

// encode magic, version, and crc - must be done after everything else is set
void th_set_ustar(TAR *t);


#ifdef __cplusplus
}
#endif
