#include "ESP32-targz-lib.h"
// mkdir, mkpath, dirname


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
  #define FILE_WRITE "w+"
#endif
#ifndef SPI_FLASH_SEC_SIZE
  #define SPI_FLASH_SEC_SIZE 4096
#endif

fs::File untarredFile;
fs::FS *tarFS = nullptr;
const char* tarDestFolder = nullptr;
entry_callbacks_t tarCallbacks;
static bool firstblock;
static uint16_t gzTarBlockPos = 0;
static uint16_t blockmod = GZIP_BUFF_SIZE / TAR_BLOCK_SIZE;
static int32_t untarredBytesCount = 0;
static size_t tarReadGzStreamBytes = 0;
static size_t totalFiles = 0;
static size_t totalFolders = 0;
bool unTarDoHealthChecks = true; // set to false for faster writes

// stores the gzip dictionary, will eat 32KB ram and be freed afterwards
unsigned char *uzlib_gzip_dict = nullptr;
uint8_t *uzlib_buffer = nullptr;
int64_t uzlib_bytesleft = 0;




// todo : malloc this
#define OUTPUT_BUFFER_SIZE 4096
static uint32_t output_position = 0;  //position in output_buffer
//static unsigned char output_buffer[OUTPUT_BUFFER_SIZE+1];
static unsigned char *output_buffer = nullptr;


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

uint8_t *getGzBufferUint8()
{
  return (uint8_t *)uzlib_read_cb_buff;
}

struct TINF_DATA uzLibDecompressor;

struct TarGzStream
{
  Stream *gz;
  Stream *tar;
  Stream *output;
  size_t gz_size;
  size_t tar_size;
  size_t output_size;
};

static TarGzStream tarGzStream;

// show progress
void (*gzProgressCallback)( uint8_t progress );
void (*tarProgressCallback)( uint8_t progress );
void (*tarMessageCallback)( const char* format, ...);
bool (*gzWriteCallback)( unsigned char* buff, size_t buffsize );

size_t (*fstotalBytes)();
size_t (*fsfreeBytes)();
void (*fsSetupSizeTools)( fsTotalBytesCb cbt, fsFreeBytesCb cbf );




// progress callback for TAR, leave empty for less console output
__attribute__((unused))
void tarNullProgressCallback( uint8_t progress )
{
  // print( message );
}
// progress callback for GZ, leave empty for less console output
__attribute__((unused))
void targzNullProgressCallback( uint8_t progress )
{
  // printf("Progress: %d", progress );
}
// error/warning/info NULL logger, for less console output
__attribute__((unused))
void targzNullLoggerCallback(const char* format, ...)
{
  //va_list args;
  //va_start(args, format);
  //vprintf(format, args);
  //va_end(args);
}

// error/warning/info FULL logger, for more console output
__attribute__((unused))
void targzPrintLoggerCallback(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}

// set totalBytes() function callback
void setFsTotalBytesCb( fsTotalBytesCb cb )
{
  fstotalBytes = cb;
}
// set freelBytes() function callback
void setFsFreeBytesCb( fsFreeBytesCb cb )
{
  fsfreeBytes = cb;
}

// set progress callback for GZ
void setProgressCallback( genericProgressCallback cb )
{
  gzProgressCallback = cb;
}
// set progress callback for TAR
void setTarProgressCallback( genericProgressCallback cb )
{
  tarProgressCallback = cb;
}
// set logger callback
void setLoggerCallback( genericLoggerCallback cb )
{
  tgzLogger = cb;
}

// set tar unpacker message callback
void setTarMessageCallback( genericLoggerCallback cb )
{
  tarMessageCallback = cb;
}
// safer but slower
void setTarVerify( bool verify )
{
  unTarDoHealthChecks = verify;
}


// private (enables)
void setupFSCallbacks()
{
  if( fsSetupSizeTools && fstotalBytes && fsfreeBytes ) {
    fsSetupSizeTools( fstotalBytes, fsfreeBytes );
  }
}

// public (assigns)
void setupFSCallbacks(  fsTotalBytesCb cbt, fsFreeBytesCb cbf )
{
  setFsTotalBytesCb( cbt );
  setFsFreeBytesCb( cbf );
  fsSetupSizeTools = setupFSCallbacks;
}

// gzWriteCallback
static bool gzUpdateWriteCallback( unsigned char* buff, size_t buffsize )
{
  if( Update.write( buff, buffsize ) ) return true;
  return false;
}

// gzWriteCallback
static bool gzStreamWriteCallback( unsigned char* buff, size_t buffsize )
{
  if( ! tarGzStream.output->write( buff, buffsize ) ) {
    tgzLogger("\n[GZ ERROR] failed to write %d bytes\n", buffsize );
    _error = ESP32_TARGZ_STREAM_ERROR;
    return false;
  }
  return true;
}

// tinyUntarReadCallback
int unTarStreamReadCallback( unsigned char* buff, size_t buffsize )
{
  return tarGzStream.tar->readBytes( buff, buffsize );
}


int unTarStreamWriteCallback(CC_UNUSED header_translated_t *proper, CC_UNUSED int entry_index, CC_UNUSED void *context_data, unsigned char *block, int length)
{
  if( tarGzStream.output ) {
    int wlen = tarGzStream.output->write( block, length );
    if( wlen != length ) {
      tgzLogger("\n");
      tgzLogger("[TAR ERROR] Written length differs from buffer length (unpacked bytes:%d, expected: %d, returned: %d)!\n", untarredBytesCount, length, wlen );
      return ESP32_TARGZ_FS_ERROR;
    }
    untarredBytesCount+=length;
    log_v("Wrote %d bytes", length);
    // TODO: file unpack progress
    //tgzLogger("[TAR INFO] unTarStreamWriteCallback wrote %d bytes to %s\n", length, proper->filename );
    /*
    if( gzProgressCallback == nullptr ) {
      if( untarredBytesCount%(length*80) == 0 ) {
        tgzLogger("\n");
      } else {
        tgzLogger("T");
      }
    }*/
  }
  return ESP32_TARGZ_OK;
}

// gzWriteCallback
static bool gzProcessTarBuffer( CC_UNUSED unsigned char* buff, CC_UNUSED size_t buffsize )
{
  if( firstblock ) {
    tar_setup(&tarCallbacks, NULL);
    firstblock = false;
  }
  gzTarBlockPos = 0;
  while( gzTarBlockPos < blockmod ) {
    int response = read_tar_step(); // warn: this may fire more than 1 read_cb()
    if( response != TAR_OK ) {
      _error = ESP32_TARGZ_TAR_ERR_GZREAD_FAIL;
      tgzLogger("[DEBUG] gzProcessTarBuffer failed reading %d bytes (buffsize=%d) in gzip block #%d/%d, got response %d\n", TAR_BLOCK_SIZE, buffsize, gzTarBlockPos%blockmod, blockmod, response);
      return false;
    }
  }
  //tgzLogger("[TGZ INFO][*] gzProcessTarBuffer processed %d tar steps\n", steps );
  return true;
}



// tinyUntarReadCallback
int tarReadGzStream( unsigned char* buff, size_t buffsize )
{
  if( buffsize%TAR_BLOCK_SIZE !=0 ) {
    tgzLogger("[ERROR] tarReadGzStream Can't unmerge tar blocks (%d bytes) from gz block (%d bytes)\n", buffsize, GZIP_BUFF_SIZE);
    _error = ESP32_TARGZ_TAR_ERR_GZDEFL_FAIL;
    return 0;
  }
  size_t i;
  for( i=0; i<buffsize; i++) {
    uzLibDecompressor.dest = &buff[i];
    int res = uzlib_uncompress_chksum(&uzLibDecompressor);
    if (res != TINF_OK) {
      // uncompress done or aborted, no need to go further
      break;
    }
  }

  tarReadGzStreamBytes += i;

  uzlib_bytesleft = tarGzStream.output_size - tarReadGzStreamBytes;
  int32_t progress = 100*(tarGzStream.output_size-uzlib_bytesleft) / tarGzStream.output_size;
  gzProgressCallback( progress );

  return i;
}


// tinyUntarReadCallback
int gzFeedTarBuffer( unsigned char* buff, size_t buffsize )
{
  if( buffsize%TAR_BLOCK_SIZE !=0 ) {
    tgzLogger("[ERROR] gzFeedTarBuffer Can't unmerge tar blocks (%d bytes) from gz block (%d bytes)\n", buffsize, GZIP_BUFF_SIZE);
    _error = ESP32_TARGZ_TAR_ERR_GZDEFL_FAIL;
    return 0;
  }
  uint32_t blockpos = gzTarBlockPos%blockmod;
  memcpy( buff, output_buffer/*uzlib_buffer*/+(TAR_BLOCK_SIZE*blockpos), TAR_BLOCK_SIZE );
  //tgzLogger("[TGZ INFO][tarbuf<-gzbuf] block #%d at output_buffer[%d] (%d bytes)\n", gzTarBlockPos, (TAR_BLOCK_SIZE*blockpos), buffsize );
  gzTarBlockPos++;
  return TAR_BLOCK_SIZE;
}

// unpack sourceFS://fileName.tar contents to destFS::/destFolder/
void defaultProgressCallback( uint8_t progress )
{
  static int8_t uzLibLastProgress = -1;
  if( uzLibLastProgress != progress ) {
    uzLibLastProgress = progress;
    if( progress == 0 ) {
      Serial.print("Progress:\n[0%");
    } else if( progress == 100 ) {
      Serial.println("100%]\n");
    } else {
      switch( progress ) {
        case 25: Serial.print(" 25% ");break;
        case 50: Serial.print(" 50% ");break;
        case 75: Serial.print(" 75% ");break;
        default: Serial.print("Z"); break; // assert the lack of precision by using a decimal sign :-)
      }
    }
  }
}



void tarGzExpanderCleanup()
{
  if( uzlib_gzip_dict != nullptr ) {
    delete( uzlib_gzip_dict );
    //uzlib_gzip_dict = nullptr;
  }
  if( uzlib_buffer != nullptr ) {
    delete( uzlib_buffer );
    //uzlib_buffer = nullptr;
  }
}


uint8_t gzReadByte(fs::File &file, const int32_t addr, fs::SeekMode mode=fs::SeekSet)
{
  file.seek( addr, mode );
  return file.read();
}

// 1) check if a file has valid gzip headers
// 2) calculate space needed for decompression
// 2) check if enough space is available on device
bool readGzHeaders(fs::File &gzFile)
{
  tarGzStream.output_size = 0;
  tarGzStream.gz_size = gzFile.size();
  bool ret = false;
  if ((gzReadByte(gzFile, 0) == 0x1f) && (gzReadByte(gzFile, 1) == 0x8b)) {
    // GZIP signature matched.  Find real size as encoded at the end
    tarGzStream.output_size =  gzReadByte(gzFile, tarGzStream.gz_size - 4);
    tarGzStream.output_size += gzReadByte(gzFile, tarGzStream.gz_size - 3)<<8;
    tarGzStream.output_size += gzReadByte(gzFile, tarGzStream.gz_size - 2)<<16;
    tarGzStream.output_size += gzReadByte(gzFile, tarGzStream.gz_size - 1)<<24;
    tgzLogger("[GZ INFO] valid gzip file detected! gz size: %d bytes, expanded size:%d bytes\n", tarGzStream.gz_size, tarGzStream.output_size);
    // Check for free space left on device before writing
    if( fstotalBytes &&  fsfreeBytes ) {
      size_t freeBytes  = fsfreeBytes();
      if( freeBytes < tarGzStream.output_size ) {
        // not enough space on device
        tgzLogger("[GZ ERROR] Target medium will be out of space (required:%d, free:%d), aborting!\n", tarGzStream.output_size, freeBytes);
        return false;
      } else {
        tgzLogger("[GZ INFO] Available space:%d bytes\n", freeBytes);
      }
    } else {
      #if defined WARN_LIMITED_FS
        tgzLogger("[GZ WARNING] Can't check target medium for free space (required:%d, free:\?\?), will try to expand anyway\n", tarGzStream.output_size );
      #endif
    }
    ret = true;
  }
  gzFile.seek(0);
  return ret;
}



// read a byte from the decompressed destination file, at 'offset' from the current position.
// offset will be the negative offset back into the written output stream.
// note: this does not ever write to the output stream; it simply reads from it.
static unsigned int readDestByte(int offset, unsigned char *out)
{
  unsigned char data;
  //delta between our position in output_buffer, and the desired offset in the output stream
  int delta = output_position + offset;
  if (delta >= 0) {
    //we haven't written output_buffer to persistent storage yet; we need to read from output_buffer
    data = output_buffer[delta];
  } else {
    fs::File *f = (fs::File*)tarGzStream.output;
    //we need to read from persistent storage
    //save where we are in the file
    long last_pos = f->position();
    f->seek( last_pos+delta, fs::SeekSet );
    data = f->read();//gzReadByte(*f, last_pos+delta, fs::SeekSet);
    f->seek( last_pos, fs::SeekSet );
  }
  *out = data;

  return 0;
}

// consume and return a byte from the source stream into the argument 'out'.
// returns 0 on success, or -1 on error.
static unsigned int readSourceByte(struct TINF_DATA *data, unsigned char *out)
{
  if (tarGzStream.gz->readBytes( out, 1 ) != 1) {
    tgzLogger("readSourceByte read error\n");
    return -1;
  }
  return 0;
}




int gzUncompress( bool isupdate = false, bool stream_to_tar = false, bool use_dict = true )
{
  if( !tarGzStream.gz->available() ) {
    tgzLogger("[ERROR] in gzUncompress: gz resource doesn't exist!\n");
    return ESP32_TARGZ_STREAM_ERROR;
  }

  size_t output_buffer_size = SPI_FLASH_SEC_SIZE; // 4Kb
  int uzlib_dict_size = 0;

  uzlib_init();

  if ( use_dict == true ) {
    uzlib_gzip_dict = new unsigned char[GZIP_DICT_SIZE];
    uzlib_dict_size = GZIP_DICT_SIZE;
    uzLibDecompressor.readDestByte   = NULL;
    tgzLogger("[INFO] gzUncompress tradeoff: faster, used %d bytes of ram (heap after alloc: %d)\n", GZIP_DICT_SIZE+output_buffer_size, ESP.getFreeHeap());
  } else {
    if( stream_to_tar ) {
      tgzLogger("[ERROR] gz->tar->filesystem streaming requires a gzip dictionnnary\n");
      return ESP32_TARGZ_NEEDS_DICT;
    } else {
      uzLibDecompressor.readDestByte   = readDestByte;
      tgzLogger("[INFO] gz output is file\n");
    }
    //output_buffer_size = SPI_FLASH_SEC_SIZE;
    tgzLogger("[INFO] gzUncompress tradeoff: slower will use %d bytes of ram (heap before alloc: %d)\n", output_buffer_size, ESP.getFreeHeap());
    uzlib_gzip_dict = NULL;
    uzlib_dict_size = 0;
  }

  uzLibDecompressor.source         = nullptr;
  uzLibDecompressor.readSourceByte = readSourceByte;
  uzLibDecompressor.destSize       = 1;
  uzLibDecompressor.log            = tgzLogger;

  int res = uzlib_gzip_parse_header(&uzLibDecompressor);
  if (res != TINF_OK) {
    tgzLogger("[ERROR] in gzUncompress: uzlib_gzip_parse_header failed (response code %d!\n", res);
    tarGzExpanderCleanup();
    return res;
  }

  uzlib_uncompress_init(&uzLibDecompressor, uzlib_gzip_dict, uzlib_dict_size);

  output_buffer = (unsigned char *)calloc( output_buffer_size+1, sizeof(unsigned char) );
  if( !output_buffer ) {
    tgzLogger("[ERROR] can't alloc %d bytes for output buffer \n", output_buffer_size );
    return -1; // TODO : Number this error
  }

  blockmod = output_buffer_size / TAR_BLOCK_SIZE;
  tgzLogger("[INFO] output_buffer_size=%d blockmod=%d\n", output_buffer_size, blockmod );

  /* decompress a single byte at a time */
  output_position = 0;
  unsigned int outlen = 0;

  gzProgressCallback( 0 );

  if( stream_to_tar ) {
    // tar will pull bytes from gz for when needed
    tinyUntarReadCallback = &tarReadGzStream;
    tarReadGzStreamBytes = 0;
    tar_setup(&tarCallbacks, NULL);
    while( read_tar_step() == TAR_OK );

  } else {
    // gz will fill a buffer and trigger a write callback

    do {
      // link to gz internals
      uzLibDecompressor.dest = &output_buffer[output_position];
      res = uzlib_uncompress_chksum(&uzLibDecompressor);
      if (res != TINF_OK) {
        // uncompress done or aborted, no need to go further
        break;
      }
      output_position++;
      // when destination buffer is filled, write/stream it
      if (output_position == output_buffer_size) {
        //tgzLogger("[INFO] Buffer full, now writing callback\n");
        gzWriteCallback( output_buffer, output_buffer_size );
        outlen += output_buffer_size;
        output_position = 0;
      }

      uzlib_bytesleft = tarGzStream.output_size - outlen;
      int32_t progress = 100*(tarGzStream.output_size-uzlib_bytesleft) / tarGzStream.output_size;
      gzProgressCallback( progress );

    } while ( res == TINF_OK/*uzlib_bytesleft > 0 */);

    if (res != TINF_DONE) {
      tgzLogger("[GZ WARNING] uzlib_uncompress_chksum had a premature end: %d at position %d while %d bytes left\n", res, output_position, uzlib_bytesleft);
    }

    if( output_position > 0 ) {
      gzWriteCallback( output_buffer, output_position );
      outlen += output_position;
      output_position = 0;
    }

    if( isupdate ) {
      size_t updatable_size = ( tarGzStream.output_size + SPI_FLASH_SEC_SIZE-1 ) & ~( SPI_FLASH_SEC_SIZE-1 );
      size_t zerofill_size  = updatable_size - tarGzStream.output_size;
      if( zerofill_size <= SPI_FLASH_SEC_SIZE ) {
        memset( output_buffer, 0, zerofill_size );
        // zero-fill to fit update.h required binary size
        gzWriteCallback( output_buffer, zerofill_size );
      }
    }

  }

  gzProgressCallback( 100 );

  tgzLogger("decompressed %d bytes\n", outlen + output_position);

  free( output_buffer );
  tarGzExpanderCleanup();

  return ESP32_TARGZ_OK;

}


// uncompress gz sourceFile to destFile
bool gzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFile )
{
  tarGzClearError();
  setupFSCallbacks();
  if (!tgzLogger ) {
    setLoggerCallback( targzPrintLoggerCallback );
  }

  if( ESP.getFreeHeap() < GZIP_DICT_SIZE+GZIP_BUFF_SIZE*2 ) {
    tgzLogger("Insufficient heap to decompress (available:%d, needed:%d), aborting\n", ESP.getFreeHeap(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
    //_error = ESP32_TARGZ_HEAP_TOO_LOW;
    //return false;
  } else {
    tgzLogger("Current heap budget (available:%d, needed:%d)\n", ESP.getFreeHeap(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
  }

  tgzLogger("uzLib expander start!\n");
  fs::File gz = sourceFS.open( sourceFile, FILE_READ );
  if( !gzProgressCallback ) {
    setProgressCallback( defaultProgressCallback );
  }
  if( !readGzHeaders( gz ) ) {
    tgzLogger("[GZ ERROR] in gzExpander: invalid gzip file or not enough space left on device ?\n");
    gz.close();
    _error = ESP32_TARGZ_UZLIB_INVALID_FILE;
    return false;
  }

  if( destFS.exists( destFile ) ) {
    tgzLogger("[GZ INFO] Deleting %s as it is in the way\n", destFile);
    destFS.remove( destFile );
  }
  fs::File outfile = destFS.open( destFile, "w+" );
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

  outfile = destFS.open( destFile, FILE_READ );
  log_d("Expanded %s to %s (%d bytes)", sourceFile, destFile, outfile.size() );
  outfile.close();

  if( fstotalBytes &&  fsfreeBytes ) {
    tgzLogger("[GZ Info] FreeBytes after expansion=%d\n", fsfreeBytes() );
  }

  return true;
}


// uncompress gz to flash (expected to be a valid Arduino compiled binary sketch)
bool gzUpdater( fs::FS &fs, const char* gz_filename )
{

  tarGzClearError();
  setupFSCallbacks();
  if (!tgzLogger ) {
    setLoggerCallback( targzPrintLoggerCallback );
  }

  if( ESP.getFreeHeap() < GZIP_DICT_SIZE+GZIP_BUFF_SIZE*2 ) {
    tgzLogger("Insufficient heap to decompress (available:%d, needed:%d), aborting\n", ESP.getFreeHeap(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE*2 );
    _error = ESP32_TARGZ_HEAP_TOO_LOW;
    return false;
  }

  tgzLogger("uzLib SPIFFS Updater start!\n");
  fs::File gz = fs.open( gz_filename, FILE_READ );
  if( !readGzHeaders( gz ) ) {
    tgzLogger("[ERROR] in gzUpdater: Not a valid gzip file\n");
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
  // TODO: restore this
  int ret = gzUncompress( true, false, true );
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


int unTarHeaderCallBack(header_translated_t *proper,  CC_UNUSED int entry_index,  CC_UNUSED void *context_data)
{
  dump_header(proper);
  if(proper->type == T_NORMAL) {

    if( fstotalBytes &&  fsfreeBytes ) {
      size_t freeBytes  = fsfreeBytes();
      if( freeBytes < proper->filesize ) {
        // Abort before the partition is smashed!
        tgzLogger("[TAR ERROR] Not enough space left on device (%llu bytes required / %d bytes available)!\n", proper->filesize, freeBytes );
        return ESP32_TARGZ_FS_FULL_ERROR;
      }
    } else {
      #if defined WARN_LIMITED_FS
        tgzLogger("[TAR WARNING] Can't check target medium for free space (required:%llu, free:\?\?), will try to expand anyway\n", proper->filesize );
      #endif
    }

    char *file_path = new char[256];// = ""; // TODO: normalize this for fs::FS, SPIFFS limit is 32, not 256
    // whoopsie :-)
    // https://www.reddit.com/r/esp32/comments/etzqdr/esp32targz_and_arduino_library_to/fuzl8oi/
    // https://github.com/tobozo/ESP32-targz/issues/3
    file_path[0] = '\0';
    // avoid double slashing root path
    if( strcmp( tarDestFolder, FOLDER_SEPARATOR ) != 0 ) {
      strcat(file_path, tarDestFolder);
    }
    // only append slash if destination folder does not end with a slash
    if( file_path[strlen(file_path)-1] != FOLDER_SEPARATOR[0] ) {
      strcat(file_path, FOLDER_SEPARATOR);
    }
    strcat(file_path, proper->filename);

    if( tarFS->exists( file_path ) ) {
      untarredFile = tarFS->open( file_path, FILE_READ );
      bool isdir = untarredFile.isDirectory();
      untarredFile.close();
      if( isdir ) {
        tgzLogger("[TAR DEBUG] Keeping %s folder\n", file_path);
      } else {
        tgzLogger("[TAR DEBUG] Deleting %s as it is in the way\n", file_path);
        tarFS->remove( file_path );
      }
    } else {
      // create directory (recursively if necessary)
      mkdirp( tarFS, file_path );
    }

    //TODO: limit this check to SPIFFS/LittleFS only
    if( strlen( file_path ) > 32 ) {
      // WARNING: SPIFFS LIMIT
      #if defined WARN_LIMITED_FS
        tgzLogger("[TAR WARNING] file path is longer than 32 chars (SPIFFS limit) and may fail: %s\n", file_path);
        _error = ESP32_TARGZ_TAR_ERR_FILENAME_TOOLONG; // don't break untar for that
      #endif
    } else {
      tgzLogger("[TAR] Creating %s\n", file_path);
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
      case T_DIRECTORY:      tgzLogger("Entering %s directory.\n\n", proper->filename);
        //tarMessageCallback( "Entering %s directory\n", proper->filename );
        totalFolders++;
      break;
      case T_FIFO:           tgzLogger("Ignoring FIFO request.\n\n"); break;
      case T_CONTIGUOUS:     tgzLogger("Ignoring contiguous data to %s.\n\n", proper->filename); break;
      case T_GLOBALEXTENDED: tgzLogger("Ignoring global extended data.\n\n"); break;
      case T_EXTENDED:       tgzLogger("Ignoring extended data.\n\n"); break;
      case T_OTHER: default: tgzLogger("Ignoring unrelevant data.\n\n");       break;
    }

  }

  return ESP32_TARGZ_OK;
}



int unTarEndCallBack( CC_UNUSED header_translated_t *proper, CC_UNUSED int entry_index, CC_UNUSED void *context_data)
{
  int ret = ESP32_TARGZ_OK;
  if(untarredFile) {
    //tgzLogger("\n");

    if( unTarDoHealthChecks ) {

      char *tmp_path = new char[256];
      memcpy( tmp_path, untarredFile.name(), 256 );
      size_t pos = untarredFile.position();
      untarredFile.close();

      // health check 1: compare stream buffer position with speculated file size
      // health check 2: file existence
      if( pos != proper->filesize || !tarFS->exists(tmp_path ) ) {
        tgzLogger("[TAR ERROR] File size and data size do not match (%d vs %d)!\n", pos, proper->filesize);
        delete tmp_path;
        return ESP32_TARGZ_FS_WRITE_ERROR;
      }
      // health check 3: reopen file to check size on filesystem
      untarredFile = tarFS->open(tmp_path, FILE_READ);
      size_t tmpsize = untarredFile.size();
      if( !untarredFile ) {
        tgzLogger("[TAR ERROR] Failed to re-open %s for size reading\n", tmp_path);
        delete tmp_path;
        return ESP32_TARGZ_FS_READSIZE_ERROR;
      }
      // health check 4: see if everyone (buffer, stream, filesystem) agree
      if( tmpsize == 0 || proper->filesize != tmpsize || pos != tmpsize ) {
        tgzLogger("[TAR ERROR] Byte sizes differ between written file %s (%d), tar headers (%d) and/or stream buffer (%d) !!\n", tmp_path, tmpsize, proper->filesize, pos );
        untarredFile.close();
        delete tmp_path;
        return ESP32_TARGZ_FS_ERROR;
      } else {
        log_d("Expanded %s (%d bytes)", tmp_path, tmpsize );
      }

      // health check5: prind md5sum
      tgzLogger("[TAR INFO] unTarEndCallBack: Untarred %d bytes md5sum(%s)=%s\n\n", tmpsize, proper->filename, MD5Sum::fromFile( untarredFile ) );


      delete tmp_path;

    }

    untarredFile.close();

    static size_t totalsize = 0;

    if( proper->type != T_DIRECTORY ) {
      totalsize += proper->filesize;
    }

    if( tarGzStream.tar_size > 0 ) {
      int32_t tarprogress = (totalsize*100) / tarGzStream.tar_size;
      //tgzLogger("Tar Progress: %d, file: %s, tarsize: %d, totalsize: %d", tarprogress, proper->filename, tarGzStream.tar_size, totalsize );
      tarProgressCallback( tarprogress );
    }

    tarMessageCallback( "%s", proper->filename );

  }
  totalFiles++;
  // TODO: send signal for created file
  return ret;
}


// unpack sourceFS://fileName.tar contents to destFS::/destFolder/
bool tarExpander( fs::FS &sourceFS, const char* fileName, fs::FS &destFS, const char* destFolder )
{
  tarGzClearError();
  setupFSCallbacks();
  tarFS = &destFS;
  tarDestFolder = destFolder;
  if( gzProgressCallback ) {
    setProgressCallback( nullptr );
  }
  if (!tgzLogger ) {
    setLoggerCallback( targzPrintLoggerCallback );
  }
  if( !tarProgressCallback ) {
    setTarProgressCallback( tarNullProgressCallback );
  }
  if( !tarMessageCallback ) {
    setTarMessageCallback( targzNullLoggerCallback );
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
  tarGzStream.tar_size = tarFile.size();
  tarGzStream.tar = &tarFile;
  tinyUntarReadCallback = &unTarStreamReadCallback;

  tar_error_logger      = tgzLogger;
  tar_debug_logger      = tgzLogger; // comment this out if too verbose

  totalFiles = 0;
  totalFolders = 0;

  tarProgressCallback( 0 );

  int res = read_tar( &tarCallbacks, NULL );
  if( res != TAR_OK ) {
    tgzLogger("[ERROR] operation aborted while expanding tar file %s (return code #%d\n", fileName, res-30);
    _error = (tarGzErrorCode)(res-30);
    return false;
  }

  tarProgressCallback( 100 );

  return true;
}



// uncompress gz sourceFile directly to untar
bool gzStreamExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFolder )
{
  tarGzClearError();
  setupFSCallbacks();
  if (!tgzLogger ) {
    setLoggerCallback( targzPrintLoggerCallback );
  }

  if( ESP.getFreeHeap() < GZIP_DICT_SIZE+GZIP_BUFF_SIZE*2 ) {
    tgzLogger("Insufficient heap to decompress (available:%d, needed:%d), aborting\n", ESP.getFreeHeap(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
    //_error = ESP32_TARGZ_HEAP_TOO_LOW;
    //return false;
  } else {
    tgzLogger("Current heap budget (available:%d, needed:%d)\n", ESP.getFreeHeap(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
  }

  tgzLogger("uzLib expander start!\n");
  fs::File gz = sourceFS.open( sourceFile, FILE_READ );
  if( !gzProgressCallback ) {
    setProgressCallback( defaultProgressCallback );
  }
  if( !readGzHeaders( gz ) ) {
    tgzLogger("[GZ ERROR] in gzStreamExpander: invalid gzip file or not enough space left on device ?\n");
    gz.close();
    _error = ESP32_TARGZ_UZLIB_INVALID_FILE;
    return false;
  }

  tarGzStream.gz = &gz;

  tarFS = &destFS;
  tarDestFolder = destFolder;
  if( !tarProgressCallback ) {
    setTarProgressCallback( tarNullProgressCallback );
  }
  if( !tarMessageCallback ) {
    setTarMessageCallback( targzNullLoggerCallback );
  }
  if( !destFS.exists( tarDestFolder ) ) {
    destFS.mkdir( tarDestFolder );
  }

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
  gzWriteCallback       = &gzProcessTarBuffer;

  totalFiles = 0;
  totalFolders = 0;

  firstblock = true;

  int ret = gzUncompress( false, true );

  gz.close();

  if( ret!=0 ) {
    tgzLogger("gzUncompress returned error code %d\n", ret);
    _error = (tarGzErrorCode)ret;
    return false;
  }
  tgzLogger("uzLib expander finished!\n");

  if( fstotalBytes &&  fsfreeBytes ) {
    tgzLogger("[GZ Info] FreeBytes after expansion=%d\n", fsfreeBytes() );
  }

  return true;
}



// unzip sourceFS://sourceFile.tar.gz contents into destFS://destFolder
bool tarGzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFolder, const char* tempFile )
{
  tarGzClearError();
  setupFSCallbacks();

  if( tempFile != nullptr ) {

    log_v("[INFO] tarGzExpander will use a separate file: %s)\n", tempFile );

    mkdirp( &destFS, tempFile );

    if( gzExpander(sourceFS, sourceFile, destFS, tempFile) ) {
      tgzLogger("[INFO] heap before tar-expanding: %d)\n", ESP.getFreeHeap());
      if( tarExpander(destFS, tempFile, destFS, destFolder) ) {
        // yay
      }
    }
    if( destFS.exists( tempFile ) ) destFS.remove( tempFile );

    return !tarGzHasError();
  } else {

    log_v("[INFO] tarGzExpander will use streams (no intermediate file)\n" );

    return gzStreamExpander( sourceFS, sourceFile, destFS, destFolder );
  }
}


// show the contents of a given file as a hex dump
void hexDumpFile( fs::FS &fs, const char* filename, uint32_t output_size )
{
  File binFile = fs.open( filename, FILE_READ );
  //log_w("File size : %d", binFile.size() );
  // only dump small files
  if( binFile.size() > 0 ) {
    //size_t output_size = 32;
    Serial.printf("Showing file %s (%d bytes) md5: %s\n", filename, binFile.size(), MD5Sum::fromFile( binFile ) );
    binFile.seek(0);
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
          binaryStr += ".";
        }
      }
      sprintf( byteToStr, "[0x%04X - 0x%04X] ",  totalBytes, totalBytes+bytes_read);
      totalBytes += bytes_read;
      if( bytes_read < output_size ) {
        for( int i=0; i<output_size-bytes_read; i++ ) {
          bytesStr  += "-- ";
          binaryStr += ".";
        }
      }
      Serial.println( byteToStr + bytesStr + " " + binaryStr );
      bytes_read = binFile.readBytes( buff, output_size );
    }
  } else {
    Serial.printf("Ignoring file %s (%d bytes)", filename, binFile.size() );
  }
  binFile.close();
}

// get a directory listing of a given filesystem
#if defined( ESP32 )

  void tarGzListDir( fs::FS &fs, const char * dirName, uint8_t levels, bool hexDump )
  {
    File root = fs.open( dirName, FILE_READ );
    if( !root ) {
      tgzLogger("[ERROR] in tarGzListDir: Can't open %s dir\n", dirName );
      return;
    }
    if( !root.isDirectory() ) {
      tgzLogger("[ERROR] in tarGzListDir: %s is not a directory\n", dirName );
      return;
    }
    File file = root.openNextFile();
    while( file ) {
      if( file.isDirectory() ) {
        Serial.printf( "[DIR]  %s\n", file.name() );
        if( levels && levels > 0  ) {
          tarGzListDir( fs, file.name(), levels -1, hexDump );
        }
      } else {
        Serial.printf( "[FILE] %-32s %8d bytes md5:%s\n", file.name(), file.size(), MD5Sum::fromFile( file ) );
        if( hexDump ) {
          hexDumpFile( fs, file.name() );
        }
      }
      file = root.openNextFile();
    }
  }

#elif defined( ESP8266 )

  void tarGzListDir(fs::FS &fs, const char * dirname, uint8_t levels, bool hexDump)
  {
    //void( hexDump ); // not used (yet?) with ESP82
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
        if( hexDump ) {
          hexDumpFile( fs, file.name() );
        }
      /*}*/
      file.close();
    }
    Serial.println("Listing done");
  }

#endif
