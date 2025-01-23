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


#include "../ESP32-targz-log.hpp" // import log_e(), log_w(), log_d() and log_i(), all behaving like printf()

#define UID "root"
#define GID "root"

char * tar_block;


// init tar for archive creation
int tar_init(TAR *t, const char *pathname, tar_callback_t *io, void *opaque)
{
  if (t == NULL) {
    log_e("Failed to alloc %d files for tar", sizeof(TAR));
    return -1;
  }

  t->pathname = (char *)pathname;
  t->io = io;
  t->opaque = opaque;

  tar_block = (char*)calloc(1, T_BLOCKSIZE);
  if( tar_block==NULL ) {
    log_e("Failed to alloc %d files for tar_block", sizeof(TAR));
    return -1;
  }

  if( io==NULL ){
    log_e("No attached callbacks");
    return -1;
  }

  log_d("Tar init done");

  return 0;
}

// open a new tarfile handle
int tar_open(TAR *t, const char *pathname, tar_callback_t *io, const char *mode, void *opaque)
{
  if (tar_init(t, pathname, io, opaque) == -1)
    return -1;
  log_d("Tar open %s with '%s' flag", pathname, mode);
  t->fd = (*(t->io->openfunc))(t->opaque, pathname, mode);
  if (t->fd == (void *)-1) {
    log_e("Failed to open file %s with %s flag", pathname, mode);
    // free(*t);
    return -1;
  }
  return 0;
}



// close tarfile handle
int tar_close(TAR *t)
{
  int i = -1;
  if(t->io->closefunc)
    i = t->io->closefunc(t->opaque, t->fd);
  free(tar_block);
  tar_block = NULL;
  return i;
}



typedef enum {
  OPEN_FILE,
  WRITE_BLOCK,
  WRITE_LAST_BLOCK,
  CLOSE_FILE
} regfile_steps_t;


regfile_steps_t regfile_step = OPEN_FILE;
int regfile_iterator = 0;
ssize_t regfile_read_bytes = 0;
ssize_t regfile_size = 0;
void * regfile_fd = NULL;
int regfile_rv = -1;

// forward declaration
int tar_append_regfile_step(TAR *t, const char *realname, ssize_t *written_bytes);



typedef enum {
  TREE_OPEN,
  TREE_APPEND_STEP,
  TREE_APPEND_EOF,
  TREE_CLOSE
} tree_packer_step_t;


static struct stat entity_stat;


int tar_append_entity_step(entity_t *e)
{
  assert(e);
  switch( e->step )
  {
    case ENTITY_STAT:
      e->step_rv = -1;
      if(e->tar == NULL || e->tar->io == NULL || e->tar->io->statfunc == NULL) {
        // malformed entity
        return -1;
      }
      if( e->tar->io->statfunc(e->tar->opaque, e->realname, &entity_stat) != 0) {
        // file not found
        return -1;
      }
      memset(&(e->tar->th_buf), 0, sizeof(struct tar_header)); // clear header buffer
      th_set_from_stat(e->tar, &entity_stat); // set header block
      th_set_path(e->tar, (e->savename ? e->savename : e->realname)); // set the header path
      e->step = ENTITY_HEADER;
      // fallthrough
    case ENTITY_HEADER: log_d("Entity header(%s)", e->realname);
      if (th_write(e->tar, e->written_bytes) != 0) { // header write failed?
        return -1;
      }
      e->step = th_is_regfile(e->tar) ? ENTITY_REGFILE : ENTITY_END;
      return 0;
    case ENTITY_REGFILE:
      regfile_step = OPEN_FILE;
      e->step = ENTITY_REGFILE_STEP;
      // fallthrough
    case ENTITY_REGFILE_STEP:
      if( tar_append_regfile_step(e->tar, e->realname, e->written_bytes) == -1 )
        return -1;
      if(regfile_step==CLOSE_FILE) {
        e->step = ENTITY_END;
        e->step_rv = 0;
      }
      return 0;
    case ENTITY_END:
      return e->step_rv;
  }

  return 0;

}





// appends a dir entity (file or folder) to the tar archive
int tar_append_entity(entity_t* entity)
{
  assert(entity);
  int ret_value = -1;
  entity->step = ENTITY_STAT;
  do  {
    ret_value = tar_append_entity_step(entity);
    if( ret_value == -1 )
      return -1;
  } while(entity->step!=ENTITY_END);

  return ret_value;
}


// write EOF indicator
int tar_append_eof(TAR *t, ssize_t *written_bytes)
{
  int i, j;

  memset(tar_block, 0, T_BLOCKSIZE);
  for (j = 0; j < 2; j++) {
    i = t->io->writefunc(t->opaque, t->fd, tar_block, T_BLOCKSIZE);
    *written_bytes += i;
    if (i != T_BLOCKSIZE) {
      if (i != -1)
        errno = EINVAL;
      log_e("written bytes (%d) don't match tar block size (%d)", i, T_BLOCKSIZE);
      return -1;
    }
  }
  // trigger last write signal (reminder: writefunc may be async)
  if( t->io->closewritefunc(t->opaque, t->fd) == -1 )
    return -1;
  log_v("Wrote %d leftover bytes", leftover_bytes);
  return 0;
}







int tar_append_regfile_step(TAR *t, const char *realname, ssize_t *written_bytes)
{
  ssize_t block_size = 0;

  switch(regfile_step)
  {
    case OPEN_FILE:
      regfile_iterator = 0;
      regfile_rv = -1;
      log_d("Adding %s", realname);
      regfile_fd = t->io->openfunc(t->opaque, realname, "r");
      if (regfile_fd == (void *)-1)
        return -1;
      regfile_size = (unsigned int)oct_to_int((t)->th_buf.size);
      memset(tar_block, 0, T_BLOCKSIZE);
      regfile_iterator = regfile_size;
      log_v("Assigning size (%d)", regfile_size);
      regfile_step = WRITE_BLOCK;
      // fallthrough
    case WRITE_BLOCK:
    {
      log_v("WRITE_BLOCK");
      if(regfile_iterator <= T_BLOCKSIZE ) {
        regfile_step = WRITE_LAST_BLOCK;
        return 0;
      }
      regfile_read_bytes = t->io->readfunc(t->opaque, regfile_fd, tar_block, T_BLOCKSIZE);
      if (regfile_read_bytes != T_BLOCKSIZE) {
        if (regfile_read_bytes != -1) {
          errno = EINVAL;
        }
        regfile_step = CLOSE_FILE;
        return 0;
      }
      block_size = t->io->writefunc(t->opaque, t->fd, tar_block, T_BLOCKSIZE);
      if (block_size == -1) {
        regfile_step = CLOSE_FILE;
        return 0;
      }
      regfile_iterator -= block_size;
      log_v("Wrote %lu bytes, regfile_iterator=%d", T_BLOCKSIZE, regfile_iterator);
      *written_bytes += block_size;
    }
    break;
    case WRITE_LAST_BLOCK:
      {
        log_v("case WRITE_LAST_BLOCK");
        regfile_step = CLOSE_FILE;
        if (regfile_iterator > 0) {
          regfile_read_bytes = t->io->readfunc(t->opaque, regfile_fd, tar_block, regfile_iterator);
          if (regfile_read_bytes == -1) {
            return 0;
          }
          memset(&tar_block[regfile_iterator], 0, T_BLOCKSIZE - regfile_iterator);
          block_size = t->io->writefunc(t->opaque, (t)->fd, tar_block, T_BLOCKSIZE);
          if ( block_size == -1) {
            return 0;
          }
          log_v("Wrote %lu last bytes", T_BLOCKSIZE);
          *written_bytes += block_size;
        }
      }
      regfile_rv = 0;
      // fallthrough
    case CLOSE_FILE:
      log_v("case CLOSE_FILE");
      t->io->closefunc(t->opaque, regfile_fd);
      regfile_fd = NULL;
      return regfile_rv;
    break;
  }

  return 0;
}



// add file contents to a tarchive
int tar_append_regfile(TAR *t, const char *realname, ssize_t *written_bytes)
{

  regfile_step = OPEN_FILE;
  int ret_value;
  do  {
    ret_value = tar_append_regfile_step(t, realname, written_bytes);
    if( ret_value == -1 )
      return -1;
  } while(regfile_step!=CLOSE_FILE);

  return ret_value;
}


// write a header block
int th_write(TAR *t, ssize_t *written_bytes)
{
  int i;//, j;
  th_set_ustar(t);

  i = t->io->writefunc(t->opaque, (t)->fd, &(t->th_buf), T_BLOCKSIZE);
  if (i != T_BLOCKSIZE) {
    log_e("ERROR in th_write, returned block size %d didn't match expexted size %d", i, (int)T_BLOCKSIZE);
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
  strncpy(t->th_buf.version, TVERSION, TVERSLEN);
  strncpy(t->th_buf.magic, TMAGIC, TMAGLEN);
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
      log_e("ERROR: no '/' found in \"%s\"", pathname);
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
void th_set_size(TAR* t, ssize_t fsize) {
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


// utils


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

