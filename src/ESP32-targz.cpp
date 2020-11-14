#include "ESP32-targz.h"
#include "uzlib/uzlib.h"     // https://github.com/pfalcon/uzlib
extern "C" {
  #include "TinyUntar/untar.h" // https://github.com/dsoprea/TinyUntar
}

// some compiler sweetener
#define CC_UNUSED __attribute__((unused))
#define GZIP_DICT_SIZE 32768
#define GZIP_BUFF_SIZE 4096

#define FOLDER_SEPARATOR "/"

#ifndef FILE_READ
  #define FILE_READ "r"
#endif
#ifndef FILE_WRITE
  #define FILE_WRITE "w"
#endif
#ifndef SPI_FLASH_SEC_SIZE
  #define SPI_FLASH_SEC_SIZE 4096
#endif

fs::File untarredFile;
fs::FS *tarFS = nullptr;
const char* tarDestFolder = nullptr;
entry_callbacks_t tarCallbacks;
bool firstblock;
int gzTarBlockPos = 0;
byte blockmod = GZIP_BUFF_SIZE / TAR_BLOCK_SIZE;
static uint32_t untarredBytesCount = 0;

// stores the gzip dictionary, will eat 32KB ram and be freed afterwards
unsigned char *uzlib_gzip_dict = nullptr;
uint8_t *uzlib_buffer = nullptr;
int32_t uzlib_bytesleft = 0;
//int8_t uzLibLastProgress = -1;
unsigned char __attribute__((aligned(4))) uzlib_read_cb_buff[GZIP_BUFF_SIZE];

tarGzErrorCode _error = ESP32_TARGZ_OK;

int8_t tarGzGetError()
{
  return (int8_t)_error;
}

void tarGzClearError()
{
  _error = ESP32_TARGZ_OK;
}

bool tarGzHasError()
{
  return _error != ESP32_TARGZ_OK;
}

uint8_t *getGzBufferUint8() {
  return (uint8_t *)uzlib_read_cb_buff;
}

struct uzlib_uncomp uzLibDecompressor;

struct TarGzStream {
  Stream *gz;
  Stream *tar;
  Stream *output;
  int32_t gz_size;
  int32_t output_size;
};

TarGzStream tarGzStream;

// show progress
void (*gzProgressCallback)( uint8_t progress );
void (*gzWriteCallback)( unsigned char* buff, size_t buffsize );
void (*tgzLogger)( const char* format, ...);

void tgzNullLogger(CC_UNUSED const char* format, ...) {
  //
}

void tgzPrintLogger(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}


/* dirname - return directory part of PATH.
   Copyright (C) 1996-2014 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1996.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */
char *dirname(char *path) {
  static const char dot[] = ".";
  char *last_slash;
  /* Find last '/'.  */
  last_slash = path != NULL ? strrchr (path, '/') : NULL;
  if (last_slash != NULL && last_slash != path && last_slash[1] == '\0') {
    /* Determine whether all remaining characters are slashes.  */
    char *runp;
    for (runp = last_slash; runp != path; --runp)
      if (runp[-1] != '/')
        break;
    /* The '/' is the last character, we have to look further.  */
    if (runp != path)
      last_slash = (char*)memrchr(path, '/', runp - path);
  }
  if (last_slash != NULL) {
    /* Determine whether all remaining characters are slashes.  */
    char *runp;
    for (runp = last_slash; runp != path; --runp)
      if (runp[-1] != '/')
        break;
    /* Terminate the path.  */
    if (runp == path) {
      /* The last slash is the first character in the string.  We have to
          return "/".  As a special case we have to return "//" if there
          are exactly two slashes at the beginning of the string.  See
          XBD 4.10 Path Name Resolution for more information.  */
      if (last_slash == path + 1)
        ++last_slash;
      else
        last_slash = path + 1;
    } else
      last_slash = runp;
    last_slash[0] = '\0';
  } else
    /* This assignment is ill-designed but the XPG specs require to
       return a string containing "." in any case no directory part is
       found and so a static and constant string is required.  */
    path = (char *) dot;
  return path;
}


void setProgressCallback( genericProgressCallback cb ) {
  gzProgressCallback = cb;
}

void setLoggerCallback( genericLoggerCallback cb ) {
  tgzLogger = cb;
}

static void gzUpdateWriteCallback( unsigned char* buff, size_t buffsize ) {
  Update.write( buff, buffsize );
}


static void gzStreamWriteCallback( unsigned char* buff, size_t buffsize ) {
  if( ! tarGzStream.output->write( buff, buffsize ) ) {
    _error = ESP32_TARGZ_STREAM_ERROR;
  }
}


static void gzProcessTarBuffer( CC_UNUSED unsigned char* buff, CC_UNUSED size_t buffsize ) {
  if( firstblock ) {
    tar_setup(&tarCallbacks, NULL);
    firstblock = false;
  }
  for( byte i=0;i<blockmod;i++) {
    int response = read_tar_step();
    if( response != TAR_OK ) {
      _error = ESP32_TARGZ_TAR_ERR_GZREAD_FAIL;
      tgzLogger("[DEBUG] Failed reading %d bytes in gzip block #%d, got response %d", TAR_BLOCK_SIZE, blockmod, response);
      break;
    }
  }
}


int gzFeedTarBuffer( unsigned char* buff, size_t buffsize ) {
  if( buffsize%TAR_BLOCK_SIZE !=0 ) {
    tgzLogger("[ERROR] gzFeedTarBuffer Can't unmerge tar blocks (%d bytes) from gz block (%d bytes)", buffsize, GZIP_BUFF_SIZE);
    _error = ESP32_TARGZ_TAR_ERR_GZDEFL_FAIL;
    return 0;
  }
  byte blockpos = gzTarBlockPos%blockmod;
  memcpy( buff, uzlib_buffer+(TAR_BLOCK_SIZE*blockpos), TAR_BLOCK_SIZE );
  gzTarBlockPos++;
  return TAR_BLOCK_SIZE;
}

// unpack sourceFS://fileName.tar contents to destFS::/destFolder/
void defaultProgressCallback( uint8_t progress ) {
  static int8_t uzLibLastProgress = -1;
  if( uzLibLastProgress != progress ) {
    uzLibLastProgress = progress;
    if( progress == 0 ) {
      Serial.print("Progress:\n[0%");
    } else if( progress == 100 ) {
      Serial.println("100%]");
    } else {
      Serial.print("Z"); // assert the lack of precision by using a decimal sign :-)
    }
  }
}


int gzStreamReadCallback( struct uzlib_uncomp *m ) {
  m->source = uzlib_read_cb_buff;
  m->source_limit = uzlib_read_cb_buff + GZIP_BUFF_SIZE;
  tarGzStream.gz->readBytes( uzlib_read_cb_buff, GZIP_BUFF_SIZE );
  return *( m->source++ );
}


uint8_t gzReadByte(fs::File &file, const uint32_t addr) {
  file.seek( addr );
  return file.read();
}

void tarGzExpanderCleanup() {
  if( uzlib_gzip_dict != nullptr ) {
    delete( uzlib_gzip_dict );
    //uzlib_gzip_dict = nullptr;
  }
  if( uzlib_buffer != nullptr ) {
    delete( uzlib_buffer );
    //uzlib_buffer = nullptr;
  }
}


// check if a file has gzip headers, if so read its projected uncompressed size
bool readGzHeaders(fs::File &gzFile) {
  tarGzStream.output_size = 0;
  tarGzStream.gz_size = gzFile.size();
  bool ret = false;
  if ((gzReadByte(gzFile, 0) == 0x1f) && (gzReadByte(gzFile, 1) == 0x8b)) {
    // GZIP signature matched.  Find real size as encoded at the end
    tarGzStream.output_size =  gzReadByte(gzFile, tarGzStream.gz_size - 4);
    tarGzStream.output_size += gzReadByte(gzFile, tarGzStream.gz_size - 3)<<8;
    tarGzStream.output_size += gzReadByte(gzFile, tarGzStream.gz_size - 2)<<16;
    tarGzStream.output_size += gzReadByte(gzFile, tarGzStream.gz_size - 1)<<24;
    tgzLogger("gzip file detected ! gz size: %d bytes, expanded size:%d bytes\n", tarGzStream.gz_size, tarGzStream.output_size);

    //available()


    //TODO: check for free space left on device even though doing this SPIFFS/SD and SD_MMC totally differs ?
    ret = true;
  }
  gzFile.seek(0);
  return ret;
}


int gzProcessBlock( bool isupdate ) {
  uzLibDecompressor.dest_start = uzlib_buffer;
  uzLibDecompressor.dest = uzlib_buffer;
  int to_read = (uzlib_bytesleft > SPI_FLASH_SEC_SIZE) ? SPI_FLASH_SEC_SIZE : uzlib_bytesleft;
  uzLibDecompressor.dest_limit = uzlib_buffer + to_read;
  int res = uzlib_uncompress(&uzLibDecompressor);
  if ((res != TINF_DONE) && (res != TINF_OK)) {
    tgzLogger("[ERROR] in gzProcessBlock while uncompressing data");
    gzProgressCallback( 0 );
    return res; // Error uncompress body
  } else {
    gzProgressCallback( 100*(tarGzStream.output_size-uzlib_bytesleft)/tarGzStream.output_size );
  }
  /*
  // Fill any remaining with 0x00
  for (int i = to_read; i < SPI_FLASH_SEC_SIZE; i++) {
      uzlib_buffer[i] = 0x00;
  }*/
  if( !isupdate ) {
    gzWriteCallback( uzlib_buffer, to_read );
    uzlib_bytesleft -= to_read;
  } else {
    gzWriteCallback( uzlib_buffer, SPI_FLASH_SEC_SIZE );
    uzlib_bytesleft -= SPI_FLASH_SEC_SIZE;
  }
  return ESP32_TARGZ_OK;
}


int gzUncompress( bool isupdate = false ) {
  if( !tarGzStream.gz->available() ) {
    tgzLogger("[ERROR] in gzUncompress: gz resource doesn't exist!");
    return ESP32_TARGZ_STREAM_ERROR;
  }
  uzlib_gzip_dict = new unsigned char[GZIP_DICT_SIZE];
  uzlib_bytesleft  = tarGzStream.output_size;
  uzlib_buffer = new uint8_t [SPI_FLASH_SEC_SIZE];
  uzlib_init();
  uzLibDecompressor.source = NULL;
  uzLibDecompressor.source_limit = NULL;
  // TODO: malloc() uzlib_read_cb_buff
  uzLibDecompressor.source_read_cb = gzStreamReadCallback;
  uzlib_uncompress_init(&uzLibDecompressor, uzlib_gzip_dict, GZIP_DICT_SIZE);
  int res = uzlib_gzip_parse_header(&uzLibDecompressor);
  if (res != TINF_OK) {
    tgzLogger("[ERROR] in gzUncompress: uzlib_gzip_parse_header failed!");
    tarGzExpanderCleanup();
    return res; // Error uncompress header read
  }
  gzProgressCallback( 0 );
  while( uzlib_bytesleft>0 ) {
    int res = gzProcessBlock( isupdate );
    if (res!= TINF_OK ) {
      tarGzExpanderCleanup();
      return res; // Error processing block
    }
  }
  gzProgressCallback( 100 );
  tarGzExpanderCleanup();
  //uzlib_buffer = nullptr;
  return ESP32_TARGZ_OK;
}




// uncompress gz sourceFile to destFile
bool gzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFile ) {
  tarGzClearError();
  if (!tgzLogger ) {
    setLoggerCallback( tgzPrintLogger );
  }
  tgzLogger("uzLib expander start!\n");
  fs::File gz = sourceFS.open( sourceFile, FILE_READ );
  if( !gzProgressCallback ) {
    setProgressCallback( defaultProgressCallback );
  }
  if( !readGzHeaders( gz ) ) {
    tgzLogger("[ERROR] in gzExpander: Not a valid gzip file");
    gz.close();
    _error = ESP32_TARGZ_UZLIB_INVALID_FILE;
    return false;
  }

  //SPIFFSFS
  //SDFS
  //SDMMCFS


  if( &(destFS) == &(SPIFFS) ) {
    //
  }
  // TODO: check for available space on destination FS
  //int fBytes = destFS.toalBytes() - destFS.usedBytes();
  //tgzLogger("Space available on dest vs required: %d vs %d\n", fBytes, tarGzStream.output_size );


  if( destFS.exists( destFile ) ) {
    tgzLogger("[INFO] Deleting %s as it is in the way", destFile);
    destFS.remove( destFile );
  }
  fs::File outfile = destFS.open( destFile, FILE_WRITE );
  tarGzStream.gz = &gz;
  tarGzStream.output = &outfile;
  gzWriteCallback = &gzStreamWriteCallback; // for regular unzipping
  int ret = gzUncompress();
  outfile.close();
  gz.close();
  if( ret!=0 ) {
    tgzLogger("gzUncompress returned error code %d\n", ret);
    _error = (tarGzErrorCode)ret;
    return false;
  }
  tgzLogger("uzLib expander finished!\n");
  return true;
}


// uncompress gz to flash (expected to be a valid Arduino compiled binary sketch)
bool gzUpdater( fs::FS &fs, const char* gz_filename ) {
  tarGzClearError();
  if (!tgzLogger ) {
    setLoggerCallback( tgzPrintLogger );
  }
  tgzLogger("uzLib SPIFFS Updater start!\n");
  fs::File gz = fs.open( gz_filename, FILE_READ );
  if( !readGzHeaders( gz ) ) {
    tgzLogger("[ERROR] in gzUpdater: Not a valid gzip file");
    gz.close();
    _error = ESP32_TARGZ_UZLIB_INVALID_FILE;
    return false;
  }
  if( !gzProgressCallback ) {
    setProgressCallback( defaultProgressCallback );
  }
  tarGzStream.gz = &gz;
  gzWriteCallback = &gzUpdateWriteCallback; // for unzipping direct to flash
  Update.begin( ( ( tarGzStream.output_size + SPI_FLASH_SEC_SIZE-1 ) & ~( SPI_FLASH_SEC_SIZE-1 ) ) );
  int ret = gzUncompress( true );
  gz.close();

  if( ret!=0 ) {
    tgzLogger("gzUncompress returned error code %d\n", ret);
    _error = (tarGzErrorCode)ret;
    return false;
  }

  if ( Update.end() ) {
    tgzLogger( "OTA done!\n" );
    if ( Update.isFinished() ) {
      // yay
      tgzLogger("Update finished !\n");
      ESP.restart();
    } else {
      tgzLogger( "Update not finished? Something went wrong!\n" );
      _error = ESP32_TARGZ_UPDATE_INCOMPLETE;
      return false;
    }
  } else {
    tgzLogger( "Update Error Occurred. Error #: %u\n", Update.getError() );
    _error = (tarGzErrorCode)(Update.getError()-20); // "-20" offset is Update error id to esp32-targz error id
    return false;
  }
  tgzLogger("uzLib filesystem Updater finished!\n");
  _error = (tarGzErrorCode)ret;
  return true;
}


int unTarHeaderCallBack(header_translated_t *proper,  CC_UNUSED int entry_index,  CC_UNUSED void *context_data) {
  dump_header(proper);
  if(proper->type == T_NORMAL) {
    char *file_path = new char[256];// = ""; // TODO: normalize this for fs::FS, limit is 32, not 256
    // whoopsie :-)
    // https://www.reddit.com/r/esp32/comments/etzqdr/esp32targz_and_arduino_library_to/fuzl8oi/
    // https://github.com/tobozo/ESP32-targz/issues/3
    file_path[0] = '\0';
    // avoid double slashing root path
    if( strcmp( tarDestFolder, FOLDER_SEPARATOR ) != 0 ) {
      strcat(file_path, tarDestFolder);
    }
    strcat(file_path, FOLDER_SEPARATOR);
    strcat(file_path, proper->filename);

    if( tarFS->exists( file_path ) ) {
      untarredFile = tarFS->open( file_path, FILE_READ );
      bool isdir = untarredFile.isDirectory();
      untarredFile.close();
      if( isdir ) {
        log_d("Keeping %s folder", file_path);
      } else {
        log_d("Deleting %s as it is in the way", file_path);
        tarFS->remove( file_path );
      }
    } else {
      // check if mkdir -d is needed for this file
      char *tmp_path = new char[256];
      memcpy( tmp_path, file_path, 256 );

      char *dir_name = dirname( tmp_path );
      if( !tarFS->exists( dir_name ) ) {
        log_d("Creating %s folder", dir_name);
        tarFS->mkdir( dir_name );
      }
      delete tmp_path;
    }

    if( strlen( file_path ) > 32 ) {
      // WARNING: SPIFFS LIMIT
      tgzLogger("WARNING: file path is longer than 32 chars (SPIFFS limit) and may fail: %s\n", file_path);
      _error = ESP32_TARGZ_TAR_ERR_FILENAME_TOOLONG; // don't break untar for that
    } else {
      tgzLogger("Creating %s\n", file_path);
    }

    untarredFile = tarFS->open(file_path, FILE_WRITE);
    if(!untarredFile) {
      tgzLogger("[ERROR] in unTarHeaderCallBack: Could not open [%s] for write.\n", file_path);
      delete file_path;
      return ESP32_TARGZ_FS_ERROR;
    }
    delete file_path;
    tarGzStream.output = &untarredFile;
  } else {

    switch( proper->type ) {
      case T_HARDLINK:       tgzLogger("Ignoring hard link to %s.\n\n", proper->filename); break;
      case T_SYMBOLIC:       tgzLogger("Ignoring sym link to %s.\n\n", proper->filename); break;
      case T_CHARSPECIAL:    tgzLogger("Ignoring special char.\n\n"); break;
      case T_BLOCKSPECIAL:   tgzLogger("Ignoring special block.\n\n"); break;
      case T_DIRECTORY:      tgzLogger("Entering %s directory.\n\n", proper->filename); break;
      case T_FIFO:           tgzLogger("Ignoring FIFO request.\n\n"); break;
      case T_CONTIGUOUS:     tgzLogger("Ignoring contiguous data to %s.\n\n", proper->filename); break;
      case T_GLOBALEXTENDED: tgzLogger("Ignoring global extended data.\n\n"); break;
      case T_EXTENDED:       tgzLogger("Ignoring extended data.\n\n"); break;
      case T_OTHER: default: tgzLogger("Ignoring unrelevant data.\n\n");       break;
    }

  }

  return ESP32_TARGZ_OK;
}


int unTarStreamReadCallback( unsigned char* buff, size_t buffsize ) {
  return tarGzStream.tar->readBytes( buff, buffsize );
}


int unTarStreamWriteCallback(CC_UNUSED header_translated_t *proper, CC_UNUSED int entry_index, CC_UNUSED void *context_data, unsigned char *block, int length) {
  if( tarGzStream.output ) {
    tarGzStream.output->write(block, length);
    untarredBytesCount+=length;
    log_v("Wrote %d bytes", length);
    if( gzProgressCallback == nullptr ) {
      if( untarredBytesCount%(length*80) == 0 ) {
        tgzLogger("\n");
      } else {
        tgzLogger("T");
      }
    }
  }
  return ESP32_TARGZ_OK;
}


int unTarEndCallBack( CC_UNUSED header_translated_t *proper, CC_UNUSED int entry_index, CC_UNUSED void *context_data) {
  int ret = ESP32_TARGZ_OK;
  if(untarredFile) {
    tgzLogger("\n");
    //log_d("Final size: %d", untarredFile.size() );
    const char* fname = untarredFile.name();
    untarredFile.close();

    untarredFile = tarFS->open(fname, FILE_READ);

    if( proper->filesize != untarredFile.size() ) {
      log_e("Written file size (%d) differs from tar headers file size (%d) !!", proper->filesize, untarredFile.size() );
      ret = ESP32_TARGZ_FS_ERROR;
    }
    untarredFile.close();

  }
  return ret;
}


// unpack sourceFS://fileName.tar contents to destFS::/destFolder/
bool tarExpander( fs::FS &sourceFS, const char* fileName, fs::FS &destFS, const char* destFolder ) {
  tarGzClearError();
  tarFS = &destFS;
  tarDestFolder = destFolder;
  if( gzProgressCallback ) {
    setProgressCallback( nullptr );
  }
  if (!tgzLogger ) {
    setLoggerCallback( tgzPrintLogger );
  }
  if( !sourceFS.exists( fileName ) ) {
    tgzLogger("Error: file %s does not exist or is not reachable\n", fileName);
    _error = ESP32_TARGZ_FS_ERROR;
    return false;
  }
  if( !destFS.exists( tarDestFolder ) ) {
    destFS.mkdir( tarDestFolder );
  }
  untarredBytesCount = 0;
  tarCallbacks = {
    unTarHeaderCallBack,
    unTarStreamWriteCallback,
    unTarEndCallBack
  };
  fs::File tarFile = sourceFS.open( fileName, FILE_READ );
  tarGzStream.tar = &tarFile;
  tinyUntarReadCallback = &unTarStreamReadCallback;

  tar_error_logger      = tgzLogger;
  tar_debug_logger      = tgzLogger; // comment this out if too verbose

  int res = read_tar( &tarCallbacks, NULL );
  if( res != TAR_OK ) {
    tgzLogger("Error: tar file %s could not be read (return code #%d\n", fileName, res-30);
    // res
    _error = (tarGzErrorCode)(res-30);
    return false;
  }
  return true;
}


int tarGzExpanderSetup() {
  if (!tgzLogger ) {
    setLoggerCallback( tgzPrintLogger );
  }
  tgzLogger("setup begin\n");
  untarredBytesCount = 0;
  gzTarBlockPos = 0;
  tarCallbacks = {
    unTarHeaderCallBack,
    unTarStreamWriteCallback,
    unTarEndCallBack
  };
  tar_error_logger      = tgzLogger;
  tar_debug_logger      = tgzLogger; // comment this out if too verbose
  tinyUntarReadCallback = &gzFeedTarBuffer;
  gzWriteCallback    = &gzProcessTarBuffer;
  if( !gzProgressCallback ) {
    setProgressCallback( defaultProgressCallback );
  }
  uzlib_gzip_dict = new unsigned char[GZIP_DICT_SIZE];
  uzlib_bytesleft  = tarGzStream.output_size;
  uzlib_buffer = new uint8_t [SPI_FLASH_SEC_SIZE];
  uzlib_init();
  uzLibDecompressor.source = NULL;
  uzLibDecompressor.source_limit = NULL;
  // TODO: malloc() uzlib_read_cb_buff
  uzLibDecompressor.source_read_cb = gzStreamReadCallback;
  uzlib_uncompress_init(&uzLibDecompressor, uzlib_gzip_dict, GZIP_DICT_SIZE);
  tgzLogger("setup end\n");
  return uzlib_gzip_parse_header(&uzLibDecompressor);
}


// unzip sourceFS://sourceFile.tar.gz contents into destFS://destFolder
bool tarGzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFolder ) {
  tarGzClearError();
  const char* tempFile = "/tmp/data/tar";
  // tarGzStream is broken so use an intermediate file until this is fixed
  if( gzExpander(sourceFS, sourceFile, destFS, tempFile) ) {

    if( tarExpander(destFS, tempFile, destFS, destFolder) ) {
      // yay
    }
  }
  delay(100);
  if( destFS.exists( tempFile ) ) destFS.remove( tempFile );

  return !tarGzHasError();

  /*
  tgzLogger("targz expander start!\n");
  fs::File gz = sourceFS.open( sourceFile, FILE_READ );
  tarGzStream.gz = &gz;
  tarDestFolder = destFolder;
  if( !tarGzStream.gz->available() ) {
    tgzLogger("gz resource doesn't exist!");
    return 1;
  }
  if( !readGzHeaders( gz ) ) {
    tgzLogger("Not a valid gzip file");
    gz.close();
    return 2;
  }
  tarFS = &destFS;
  if( !tarFS->exists( tarDestFolder ) ) {
    tgzLogger("creating %s folder\n", tarDestFolder);
    tarFS->mkdir( tarDestFolder );
  }
  int res = tarGzExpanderSetup();
  if (res != TINF_OK) {
    tgzLogger("uzlib_gzip_parse_header failed!");
    tarGzExpanderCleanup();
    return 5; // Error uncompress header read
  }
  gzProgressCallback( 0 );
  firstblock = true;
  while( uzlib_bytesleft>0 ) {
    int res = gzProcessBlock();
    if (res!=0) {
      tarGzExpanderCleanup();
      return res;
    }
  }
  gzProgressCallback( 100 );
  tarGzExpanderCleanup();
  gz.close();
  tgzLogger("success!\n");
  return 0;
  */
}


// show the contents of a given file as a hex dump
void hexDumpFile( fs::FS &fs, const char* filename ) {
  File binFile = fs.open(filename);
  log_w("File size : %d", binFile.size() );
  if( binFile.size() > 0 ) {
    size_t output_size = 32;
    char* buff = new char[output_size];
    uint8_t bytes_read = binFile.readBytes( buff, output_size );
    String bytesStr  = "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00";
    String binaryStr = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    String addrStr = "[0x0000 - 0x0000] ";
    char byteToStr[32];
    size_t totalBytes = 0;
    while( bytes_read > 0 ) {
      bytesStr = "";
      binaryStr = "";
      for( int i=0; i<bytes_read; i++ ) {
        sprintf( byteToStr, "%02X", buff[i] );
        bytesStr  += String( byteToStr ) + String(" ");
        if( isprint( buff[i] ) ) {
          binaryStr += String( buff[i] );
        } else {
          binaryStr += " ";
        }
      }
      sprintf( byteToStr, "[0x%04X - 0x%04X] ",  totalBytes, totalBytes+bytes_read);
      totalBytes += bytes_read;
      if( bytes_read < output_size ) {
        for( int i=0; i<output_size-bytes_read; i++ ) {
          bytesStr  += "00 ";
          binaryStr += " ";
        }
      }
      Serial.println( byteToStr + bytesStr + " " + binaryStr );
      bytes_read = binFile.readBytes( buff, output_size );
    }
  }
  binFile.close();
}

// get a directory listing of a given filesystem
#if defined( ESP32 )

  void tarGzListDir( fs::FS &fs, const char * dirName, uint8_t levels, bool hexDump ) {
    File root = fs.open( dirName, FILE_READ );
    if( !root ){
      tgzLogger("[ERROR] in tarGzListDir: Can't open %s dir", dirName );
      return;
    }
    if( !root.isDirectory() ){
      tgzLogger("[ERROR] in tarGzListDir: %s is not a directory", dirName );
      return;
    }
    File file = root.openNextFile();
    while( file ){
      if( file.isDirectory() ){
        Serial.printf( "[DIR] %s\n", file.name() );
        if( levels && levels > 0  ){
          tarGzListDir( fs, file.name(), levels -1 );
        }
      } else {
        Serial.printf( "%-32s %8d bytes\n", file.name(), file.size() );
        if( hexDump ) {
          hexDumpFile( fs, file.name() );
        }
      }
      file = root.openNextFile();
    }
  }

#elif defined( ESP8266 )

  void tarGzListDir(fs::FS &fs, const char * dirname, uint8_t levels, bool hexDump) {
    Serial.printf("Listing directory %s with level %d\n", dirname, levels);

    Dir root = fs.openDir(dirname);
    /*
    if( !root.isDirectory() ){
      tgzLogger( "%s is not a directory", dirname );
      return;
    }*/
    while (root.next()) {
      File file = root.openFile("r");
      /*
      if( root.isDirectory() ){
        Serial.printf( "[DIR] %s\n", root.fileName().c_str() );
        if( levels && levels > 0 ){
          tarGzListDir( fs, root.fileName().c_str(), levels -1 );
        }
      } else {*/
        Serial.printf( "%-32s %8d bytes\n", root.fileName().c_str(), file.size() );
      /*}*/
      file.close();
    }
    Serial.printf("Listing done");
  }

#endif
