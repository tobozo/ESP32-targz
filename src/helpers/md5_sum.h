#ifndef _MD5_SUM_H_
#define _MD5_SUM_H_

extern void (*tgzLogger)( const char* format, ...);
#include <MD5Builder.h>

static char tgz_md5result[33];
static uint8_t tgz_fbuf[256];

class MD5Sum {

  public:

    static char* fromFile(fs::File &file ) {

      int len = file.size();
      if( file.position() != 0 )
        file.seek(0); // make sure to read from the start
      MD5Builder tgz_md5 = MD5Builder();

      tgz_md5.begin();

      int bufSize = len > 256 ? 256 : len;
      //uint8_t *tgz_fbuf = (uint8_t*)malloc(bufSize+1);

      size_t bytes_read = file.read( tgz_fbuf, bufSize );

      do {
        len -= bytes_read;
        if( bufSize > len ) bufSize = len;
        tgz_md5.add( tgz_fbuf, bytes_read );
        if( len == 0 ) break;
        bytes_read = file.read( tgz_fbuf, bufSize );
      } while( bytes_read > 0 );

      tgz_md5.calculate();

      snprintf( tgz_md5result, 33, "%s", tgz_md5.toString().c_str() );

      return tgz_md5result;

    }
};


#endif
