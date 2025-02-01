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

#include "libtar.h"
#include <stdio.h> // printf()
#include <fcntl.h>
#include <errno.h>
#include <string.h> // for basename()
#include <ctype.h> // for isprint()
#include <stdlib.h> // for calloc(), because RP2040 wants it

//#include "../ESP32-targz-log.hpp" // import //log_e(), //log_w(), //log_d() and //log_i(), all behaving like printf()

#define UID "root"
#define GID "root"

char * tar_block;


// init tar for archive creation
int tar_init(TAR *t, const char *pathname, tar_callback_t *io)
{
  if (t == NULL) {
    //log_e("Failed to alloc %d files for tar", sizeof(TAR));
    return -1;
  }

  t->pathname = (char *)pathname;
  t->io = io;

  if( t->io->src_fs == NULL ) {
    //log_e("Missing io->src_fs");
    return -1;
  }

  if( t->io->dst_fs == NULL ) {
    //log_d("No dst_fs provided, output may be a stream");
  }

  tar_block = (char*)calloc(1, T_BLOCKSIZE);
  if( tar_block==NULL ) {
    //log_e("Failed to alloc %d files for tar_block", sizeof(TAR));
    return -1;
  }

  if( io==NULL ){
    //log_e("No attached callbacks");
    return -1;
  }

  //log_d("Tar init done");

  return 0;
}


int tar_open(TAR *t, const char *pathname, tar_callback_t *io)
{
  if (tar_init(t, pathname, io) == -1)
    return -1;

  if(t->io->dst_fs == NULL) // not using fs for output
    return 0;

  //log_d("Tar open %s with 'w' flag", pathname);
  t->dst_file = (*(t->io->openfunc))(t->io->dst_fs, pathname, "w");
  if (t->dst_file == (void *)-1) {
    //log_e("Failed to open file %s with 'w' flag", pathname);
    return -1;
  }
  return 0;
}



int tar_close(TAR *t)
{
  int i = -1;
  if(t->io->dst_fs && t->io->closefunc)
    i = t->io->closefunc(t->io->dst_fs, t->dst_file);
  free(tar_block);
  tar_block = NULL;
  return i;
}


// write EOF indicator
int tar_append_eof(TAR *t, int *written_bytes)
{
  int i, j;

  memset(tar_block, 0, T_BLOCKSIZE);
  for (j = 0; j < 2; j++) {
    i = t->io->writefunc(t->io->dst_fs, t->dst_file, tar_block, T_BLOCKSIZE);
    *written_bytes += i;
    if (i != T_BLOCKSIZE) {
      if (i != -1)
        errno = EINVAL;
      //log_e("written bytes (%d) don't match tar block size (%d)", i, T_BLOCKSIZE);
      return -1;
    }
  }
  //log_v("EOF done");
  // trigger last write signal (reminder: writefunc may be async)
  if( t->io->closewritefunc(t->io->dst_fs, t->dst_file) == -1 )
    return -1;
  //log_v("io::closewritefunc done");
  return 0;
}


// write a header block
int th_write(TAR *t, int *written_bytes)
{
  int i;//, j;
  th_set_ustar(t);

  //log_d("Writing header");
  i = t->io->writefunc(t->io->dst_fs, t->dst_file, &(t->th_buf), T_BLOCKSIZE);
  if (i != T_BLOCKSIZE) {
    //log_e("ERROR in th_write, returned block size %d didn't match expexted size %d", i, (int)T_BLOCKSIZE);
    if (i != -1) {
      errno = EINVAL;
    }
    return -1;
  }
  *written_bytes += i;

  return 0;
}


// magic, version, and checksum
void th_set_ustar(TAR *t)
{
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wstringop-truncation"
  strncpy(t->th_buf.version, TVERSION, TVERSLEN);
  strncpy(t->th_buf.magic, TMAGIC, TMAGLEN);
  #pragma GCC diagnostic pop
  int_to_oct(th_crc_calc(t), t->th_buf.chksum, 8);
}


// map a file mode to a typeflag (only dir and files supported)
void th_set_type(TAR *t, mode_t mode)
{
  if (S_ISREG(mode)) t->th_buf.typeflag = REGTYPE;
  if (S_ISDIR(mode)) t->th_buf.typeflag = DIRTYPE;
}


// encode file path (gnu longnames not supported)
void th_set_path(TAR *t, const char *pathname)
{
  char suffix[2] = "";
  char *tmp;

  if (pathname[strlen(pathname) - 1] != '/' && th_is_dir(t))
    strcpy(suffix, "/");

  if (strlen(pathname) > T_NAMELEN) { // long path names
    // POSIX-style prefix field
    tmp = (char *)strchr(&(pathname[strlen(pathname) - T_NAMELEN - 1]), '/');
    if (tmp == NULL) {
      //log_e("ERROR: no '/' found in \"%s\"", pathname);
      return;
    }
    snprintf(t->th_buf.name, 100, "%s%s", &(tmp[1]), suffix);
    snprintf(t->th_buf.prefix, ((tmp - pathname + 1) < 155 ? (tmp - pathname + 1) : 155), "%s", pathname);
  } else
    // classic tar format
    snprintf(t->th_buf.name, 100, "%s%s", pathname, suffix);
}


// encode user info
void th_set_user(TAR *t, uid_t uid)
{
  strlcpy(t->th_buf.uname, UID, 5);
  int_to_oct(uid, t->th_buf.uid, 8);
}


// encode group info
void th_set_group(TAR *t, gid_t gid)
{
  strlcpy(t->th_buf.gname, GID, 5);
  int_to_oct(gid, t->th_buf.gid, 8);
}


// encode file mode
void th_set_mode(TAR *t, mode_t fmode)
{
  int_to_oct(fmode, (t)->th_buf.mode, 8);
}


// encode file creation time
void th_set_mtime(TAR* t, time_t fmtime) {
  int_to_oct_nonull((fmtime), (t)->th_buf.mtime, 12);
}


// encode header size
void th_set_size(TAR* t, int fsize) {
  int_to_oct_nonull((fsize), (t)->th_buf.size, 12);
}


// encode file info
void th_set_from_stat(TAR *t, struct stat *s)
{
  th_set_type(t, s->st_mode);
  th_set_user(t, 0);
  th_set_group(t, 0);
  th_set_mode(t, s->st_mode);
  th_set_mtime(t, s->st_mtime);
  if (S_ISREG(s->st_mode))
    th_set_size(t, s->st_size);
  else
    th_set_size(t, 0);
}


// check if entity is a file
int th_is_regfile(TAR *t)
{
  char flag = t->th_buf.typeflag;
  int isreg = S_ISREG((mode_t)oct_to_int(t->th_buf.mode));
  return (flag == REGTYPE || flag == AREGTYPE || flag == CONTTYPE || (isreg && flag != LNKTYPE));
}


// check if entity is a dir
int th_is_dir(TAR *t)
{
  int isdir = S_ISDIR((mode_t)oct_to_int((t)->th_buf.mode));
  char flag = t->th_buf.typeflag;
  char* name = t->th_buf.name;
  return (flag == DIRTYPE  || isdir  || (flag == AREGTYPE  && strlen(name)  && (name[strlen(name) - 1] == '/')));
}



// calculate header checksum
int th_crc_calc(TAR *t)
{
  int i, sum = 0;

  for (i = 0; i < T_BLOCKSIZE; i++)
    sum += ((unsigned char *)(&(t->th_buf)))[i];
  for (i = 0; i < 8; i++)
    sum += (' ' - (unsigned char)t->th_buf.chksum[i]);

  return sum;
}


// string-octal to integer conversion
int oct_to_int(char *oct)
{
  int i;
  return sscanf(oct, "%o", &i) == 1 ? i : 0;
}


// integer to string-octal conversion, no NULL
void int_to_oct_nonull(int num, char *oct, size_t octlen)
{
  snprintf(oct, octlen, "%*lo", octlen - 1, (unsigned long)num);
  oct[octlen - 1] = ' ';
}

