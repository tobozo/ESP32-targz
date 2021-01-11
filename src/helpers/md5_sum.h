#ifndef _MD5_SUM_H_
#define _MD5_SUM_H_


#if defined ESP32

#include "mbedtls/md5.h"


class MD5Sum {


  public:

    static char* fromFile(fs::File &file )
    {

      static char md5result[33];
      mbedtls_md5_context _ctx;

      uint8_t i;
      uint8_t * _buf = (uint8_t*)malloc(16);

      if(_buf == NULL) {
        log_e("Error can't malloc 16 bytes for md5 hashing");
        return md5result;
      }

      int len = file.size();
      file.seek(0); // make sure to read from the start

      memset(_buf, 0x00, 16);
      mbedtls_md5_init(&_ctx);
      mbedtls_md5_starts(&_ctx);

      size_t bufSize = len > 4096 ? 4096 : len;
      uint8_t *fbuf = (uint8_t*)malloc(bufSize+1);
      size_t bytes_read = file.read( fbuf, bufSize );

      do {
        len -= bytes_read;
        if( bufSize > len ) bufSize = len;
        mbedtls_md5_update(&_ctx, (const uint8_t *)fbuf, bytes_read );
        if( len == 0 ) break;
        bytes_read = file.read( fbuf, bufSize );
      } while( bytes_read > 0 );

      mbedtls_md5_finish(&_ctx, _buf);

      for(i = 0; i < 16; i++) {
        sprintf(md5result + (i * 2), "%02x", _buf[i]);
      }

      md5result[32] = 0;

      free(_buf);
      free(fbuf);

      return md5result;
    }

};


#elif defined ESP8266


#include <MD5Builder.h>

static MD5Builder _md5;


class MD5Sum {

  public:

    static char* fromFile(fs::File &file ) {

      static char md5result[33];
      uint8_t i;
      uint8_t * _buf = (uint8_t*)malloc(16);

      if(_buf == NULL) {
        log_e("Error can't malloc 16 bytes for md5 hashing");
        return md5result;
      }

      int len = file.size();
      file.seek(0); // make sure to read from the start

      _md5.begin();

      size_t bufSize = len > 4096 ? 4096 : len;
      uint8_t *fbuf = (uint8_t*)malloc(bufSize+1);
      size_t bytes_read = file.read( fbuf, bufSize );

      do {
        len -= bytes_read;
        if( bufSize > len ) bufSize = len;
        //mbedtls_md5_update(&_ctx, (const uint8_t *)fbuf, bytes_read );
        _md5.add( fbuf, bytes_read );
        if( len == 0 ) break;
        bytes_read = file.read( fbuf, bufSize );
      } while( bytes_read > 0 );

      _md5.calculate();

      snprintf( md5result, 33, "%s", _md5.toString().c_str() );

      return md5result;

    }
};


#endif


#endif
