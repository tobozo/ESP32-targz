#include "ESP32-targz-lib.h"


#include "uzlib/uzlib.h"     // https://github.com/pfalcon/uzlib
extern "C" {
  #include "TinyUntar/untar.h" // https://github.com/dsoprea/TinyUntar
}


// some compiler sweetener
#define CC_UNUSED __attribute__((unused))


fs::File untarredFile;
fs::FS *tarFS = nullptr;
const char* tarDestFolder = nullptr;
entry_callbacks_t tarCallbacks;
static bool firstblock;
static bool lastblock;
static size_t tarCurrentFileSize = 0;
static size_t tarCurrentFileSizeProgress = 0;
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
//#define OUTPUT_BUFFER_SIZE 4096
static uint32_t output_position = 0;  //position in output_buffer
//static unsigned char output_buffer[OUTPUT_BUFFER_SIZE+1];
static unsigned char *output_buffer = nullptr;


//int8_t uzLibLastProgress = -1;
unsigned char __attribute__((aligned(4))) uzlib_read_cb_buff[GZIP_BUFF_SIZE];

tarGzErrorCode _error = ESP32_TARGZ_OK;

static bool targz_halt_on_error = false;

void tarGzHaltOnError( bool halt )
{
  targz_halt_on_error = halt;
}

void targz_system_halt()
{
  printf("System halted");
  while(1) { yield(); }
}


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

uint8_t *gzGetBufferUint8()
{
  return (uint8_t *)uzlib_read_cb_buff;
}

struct TINF_DATA uzLibDecompressor;


static TarGzStream tarGzStream;

// show progress
static void (*gzProgressCallback)( uint8_t progress );
static void (*tarProgressCallback)( uint8_t progress );
static void (*tarMessageCallback)( const char* format, ...);
static bool (*gzWriteCallback)( unsigned char* buff, size_t buffsize );
static void (*tarStatusProgressCallback)( const char* name, size_t size, size_t total_unpacked );

static size_t (*fstotalBytes)();
static size_t (*fsfreeBytes)();
static void (*fsSetupSizeTools)( fsTotalBytesCb cbt, fsFreeBytesCb cbf );



void gzExpanderCleanup()
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


CC_UNUSED void defaultTarStatusProgressCallback( const char* name, size_t size, size_t total_unpacked )
{
  Serial.printf("[TAR] %-64s %8d bytes - %8d Total bytes\n", name, size, total_unpacked );
}


// unpack sourceFS://fileName.tar contents to destFS::/destFolder/
CC_UNUSED void defaultProgressCallback( uint8_t progress )
{
  static int8_t uzLibLastProgress = -1;
  if( uzLibLastProgress != progress ) {
    uzLibLastProgress = progress;
    if( progress == 0 ) {
      Serial.print("Progress: [0% ");
    } else if( progress == 100 ) {
      Serial.println(" 100%]\n");
    } else {
      switch( progress ) {
        case 25: Serial.print(" 25% ");break;
        case 50: Serial.print(" 50% ");break;
        case 75: Serial.print(" 75% ");break;
        default: Serial.print("Z"); break;
      }
    }
  }
}


// progress callback for TAR, leave empty for less console output
CC_UNUSED void tarNullProgressCallback( uint8_t progress )
{
  // print( message );
}
// progress callback for GZ, leave empty for less console output
CC_UNUSED void targzNullProgressCallback( uint8_t progress )
{
  // printf("Progress: %d", progress );
}
// error/warning/info NULL logger, for less console output
CC_UNUSED void targzNullLoggerCallback(const char* format, ...)
{
  //va_list args;
  //va_start(args, format);
  //vprintf(format, args);
  //va_end(args);
}

// error/warning/info FULL logger, for more console output
CC_UNUSED void targzPrintLoggerCallback(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}


void setTarStatusProgressCallback( tarStatusProgressCb cb )
{
  tarStatusProgressCallback = cb;
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
  if( Update.write( buff, buffsize ) ) {
    log_v("Wrote %d bytes", buffsize );
    return true;
  } else {
    log_e("Failed to write %d bytes", buffsize );
  }
  return false;
}

// gzWriteCallback
static bool gzStreamWriteCallback( unsigned char* buff, size_t buffsize )
{
  if( ! tarGzStream.output->write( buff, buffsize ) ) {
    log_e("\n[GZ ERROR] failed to write %d bytes\n", buffsize );
    _error = ESP32_TARGZ_STREAM_ERROR;
    return false;
  } else {
    log_d("Wrote %d bytes", buffsize );
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
      //tgzLogger("\n");
      log_e("[TAR ERROR] Written length differs from buffer length (unpacked bytes:%d, expected: %d, returned: %d)!\n", untarredBytesCount, length, wlen );
      return ESP32_TARGZ_FS_ERROR;
    }
    untarredBytesCount+=wlen;
    // TODO: file unpack progress
    log_v("[TAR INFO] unTarStreamWriteCallback wrote %d bytes to %s", length, proper->filename );
    tarCurrentFileSizeProgress += wlen;
    if( tarCurrentFileSize > 0 ) {
      // this is a per-file progress, not an overall progress !
      int32_t progress = 100*(tarCurrentFileSizeProgress) / tarCurrentFileSize;
      tarProgressCallback( progress );
    }
  }
  return ESP32_TARGZ_OK;
}

// gzWriteCallback
static bool gzProcessTarBuffer( CC_UNUSED unsigned char* buff, CC_UNUSED size_t buffsize )
{
  if( lastblock ) {
    /*
    log_w("Ignoring %d bytes for block #%d)", buffsize, gzTarBlockPos );
    for(int i=0;i<buffsize;i+=64) {
      hexDumpData( (const char*)buff+i, 64, 64 );
    }*/
    return true;
  }

  if( firstblock ) {
    tar_setup(&tarCallbacks, NULL);
    firstblock = false;
  }
  gzTarBlockPos = 0;
  while( gzTarBlockPos < blockmod ) {
    int response = read_tar_step(); // warn: this may fire more than 1 read_cb()

    if( response == TAR_EXPANDING_DONE ) {
      log_d("[TAR] Expanding done !");
      lastblock = true;
      return true;
    }

    if( gzTarBlockPos > blockmod ) {
      log_e("[ERROR] read_tar_step() fired more too many read_cb()");
      if( targz_halt_on_error ) targz_system_halt();
    }

    if( response < 0 ) {
      _error = ESP32_TARGZ_TAR_ERR_GZREAD_FAIL;
      log_w("[False positive?] gzProcessTarBuffer failed reading %d bytes (buffsize=%d) in gzip block #%d/%d, got response %d", TAR_BLOCK_SIZE, buffsize, gzTarBlockPos%blockmod, blockmod, response);
      if( targz_halt_on_error ) targz_system_halt();
      return false;
    }
  }
  return true;
}



// tinyUntarReadCallback
int tarReadGzStream( unsigned char* buff, size_t buffsize )
{
  if( buffsize%TAR_BLOCK_SIZE !=0 ) {
    log_e("[ERROR] tarReadGzStream Can't unmerge tar blocks (%d bytes) from gz block (%d bytes)\n", buffsize, GZIP_BUFF_SIZE);
    _error = ESP32_TARGZ_TAR_ERR_GZDEFL_FAIL;
    if( targz_halt_on_error ) targz_system_halt();
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
  static size_t bytes_fed = 0;
  if( buffsize%TAR_BLOCK_SIZE !=0 ) {
    log_e("[ERROR] gzFeedTarBuffer Can't unmerge tar blocks (%d bytes) from gz block (%d bytes)\n", buffsize, GZIP_BUFF_SIZE);
    _error = ESP32_TARGZ_TAR_ERR_GZDEFL_FAIL;
    if( targz_halt_on_error ) targz_system_halt();
    return 0;
  }
  uint32_t blockpos = gzTarBlockPos%blockmod;
  memcpy( buff, output_buffer/*uzlib_buffer*/+(TAR_BLOCK_SIZE*blockpos), TAR_BLOCK_SIZE );
  bytes_fed += TAR_BLOCK_SIZE;
  log_d("[TGZ INFO][tarbuf<-gzbuf] block #%d (%d mod %d) at output_buffer[%d] (%d bytes, total %d)", blockpos, gzTarBlockPos, blockmod, (TAR_BLOCK_SIZE*blockpos), buffsize, bytes_fed );
  gzTarBlockPos++;
  //gzTarBlockPos = gzTarBlockPos%blockmod;
  return TAR_BLOCK_SIZE;
}


// gz filesystem helper
uint8_t gzReadByte(fs::File &file, const int32_t addr, fs::SeekMode mode=fs::SeekSet)
{
  file.seek( addr, mode );
  return file.read();
}

// 1) check if a file has valid gzip headers
// 2) calculate space needed for decompression
// 2) check if enough space is available on device
bool gzReadHeader(fs::File &gzFile)
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
    log_i("[GZ INFO] valid gzip file detected! gz size: %d bytes, expanded size:%d bytes\n", tarGzStream.gz_size, tarGzStream.output_size);
    // Check for free space left on device before writing
    if( fstotalBytes &&  fsfreeBytes ) {
      size_t freeBytes  = fsfreeBytes();
      if( freeBytes < tarGzStream.output_size ) {
        // not enough space on device
        log_e("[GZ ERROR] Target medium will be out of space (required:%d, free:%d), aborting!\n", tarGzStream.output_size, freeBytes);
        return false;
      } else {
        log_i("[GZ INFO] Available space:%d bytes\n", freeBytes);
      }
    } else {
      #if defined WARN_LIMITED_FS
        log_w("[GZ WARNING] Can't check target medium for free space (required:%d, free:\?\?), will try to expand anyway\n", tarGzStream.output_size );
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
static unsigned int gzReadDestByte(int offset, unsigned char *out)
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
static unsigned int gzReadDestByte(struct TINF_DATA *data, unsigned char *out)
{
  //if( !tarGzStream.gz->available() ) return -1;
  if (tarGzStream.gz->readBytes( out, 1 ) != 1) {
    log_e("gzReadDestByte read error");
    return -1;
  } else {
    //log_v("read 1 byte: 0x%02x", out[0] );
  }
  return 0;
}





int unTarHeaderCallBack(header_translated_t *proper,  CC_UNUSED int entry_index,  CC_UNUSED void *context_data)
{
  dump_header(proper);
  if(proper->type == T_NORMAL) {

    if( fstotalBytes &&  fsfreeBytes ) {
      size_t freeBytes  = fsfreeBytes();
      if( freeBytes < proper->filesize ) {
        // Abort before the partition is smashed!
        log_e("[TAR ERROR] Not enough space left on device (%llu bytes required / %d bytes available)!", proper->filesize, freeBytes );
        return ESP32_TARGZ_FS_FULL_ERROR;
      }
    } else {
      #if defined WARN_LIMITED_FS
        log_w("[TAR WARNING] Can't check target medium for free space (required:%llu, free:\?\?), will try to expand anyway", proper->filesize );
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
        log_d("[TAR DEBUG] Keeping %s folder", file_path);
      } else {
        log_d("[TAR DEBUG] Deleting %s as it is in the way", file_path);
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
        log_w("[TAR WARNING] file path is longer than 32 chars (SPIFFS limit) and may fail: %s", file_path);
        _error = ESP32_TARGZ_TAR_ERR_FILENAME_TOOLONG; // don't break untar for that
      #endif
    } else {
      log_d("[TAR] Creating %s", file_path);
    }

    untarredFile = tarFS->open(file_path, FILE_WRITE);
    if(!untarredFile) {
      log_e("[ERROR] in unTarHeaderCallBack: Could not open [%s] for write.", file_path);
      if( targz_halt_on_error ) targz_system_halt();
      delete file_path;
      return ESP32_TARGZ_FS_ERROR;
    }
    delete file_path;
    tarGzStream.output = &untarredFile;
    tarCurrentFileSize = proper->filesize; // for progress
    tarCurrentFileSizeProgress = 0; // for progress
    static size_t totalsize = 0;
    totalsize += proper->filesize;
    if( tarStatusProgressCallback ) {
      tarStatusProgressCallback( proper->filename, proper->filesize, totalsize );
    }
    tarProgressCallback( 0 );
  } else {

    switch( proper->type ) {
      case T_HARDLINK:       log_d("Ignoring hard link to %s.", proper->filename); break;
      case T_SYMBOLIC:       log_d("Ignoring sym link to %s.", proper->filename); break;
      case T_CHARSPECIAL:    log_d("Ignoring special char."); break;
      case T_BLOCKSPECIAL:   log_d("Ignoring special block."); break;
      case T_DIRECTORY:      log_d("Entering %s directory.", proper->filename);
        //tarMessageCallback( "Entering %s directory\n", proper->filename );
        totalFolders++;
      break;
      case T_FIFO:           log_d("Ignoring FIFO request."); break;
      case T_CONTIGUOUS:     log_d("Ignoring contiguous data to %s.", proper->filename); break;
      case T_GLOBALEXTENDED: log_d("Ignoring global extended data."); break;
      case T_EXTENDED:       log_d("Ignoring extended data."); break;
      case T_OTHER: default: log_d("Ignoring unrelevant data.");       break;
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

      // health check 1: file existence
      if( !tarFS->exists(tmp_path ) ) {
        log_e("[TAR ERROR] File %s was not created although it was properly decoded, path is too long ?", tmp_path );
        delete tmp_path;
        return ESP32_TARGZ_FS_WRITE_ERROR;
      }
      // health check 2: compare stream buffer position with speculated file size
      if( pos != proper->filesize ) {
        log_e("[TAR ERROR] File size and data size do not match (%d vs %d)!", pos, proper->filesize);
        delete tmp_path;
        return ESP32_TARGZ_FS_WRITE_ERROR;
      }
      // health check 3: reopen file to check size on filesystem
      untarredFile = tarFS->open(tmp_path, FILE_READ);
      size_t tmpsize = untarredFile.size();
      if( !untarredFile ) {
        log_e("[TAR ERROR] Failed to re-open %s for size reading", tmp_path);
        delete tmp_path;
        return ESP32_TARGZ_FS_READSIZE_ERROR;
      }
      // health check 4: see if everyone (buffer, stream, filesystem) agree
      if( tmpsize == 0 || proper->filesize != tmpsize || pos != tmpsize ) {
        log_e("[TAR ERROR] Byte sizes differ between written file %s (%d), tar headers (%d) and/or stream buffer (%d) !!", tmp_path, tmpsize, proper->filesize, pos );
        untarredFile.close();
        delete tmp_path;
        return ESP32_TARGZ_FS_ERROR;
      }

      log_d("Expanded %s (%d bytes)", tmp_path, tmpsize );

      // health check5: prind md5sum
      log_d("[TAR INFO] unTarEndCallBack: Untarred %d bytes md5sum(%s)=%s", tmpsize, proper->filename, MD5Sum::fromFile( untarredFile ) );

      delete tmp_path;

    }

    untarredFile.close();

    static size_t totalsize = 0;
    if( proper->type != T_DIRECTORY ) {
      totalsize += proper->filesize;
    }

    if( tarGzStream.tar_size > 0 ) {
      int32_t tarprogress = (totalsize*100) / tarGzStream.tar_size;
      tarProgressCallback( tarprogress );
    } else {
      log_d("Total expanded bytes: %d", totalsize );
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
    log_e("Error: file %s does not exist or is not reachable", fileName);
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
    log_e("[ERROR] operation aborted while expanding tar file %s (return code #%d", fileName, res-30);
    if( targz_halt_on_error ) targz_system_halt();
    _error = (tarGzErrorCode)(res-30);
    return false;
  }

  tarProgressCallback( 100 );

  return true;
}






// gz decompression main routine, handles all logical cases
// isupdate      => zerofill to fit SPI_FLASH_SEC_SIZE
// stream_to_tar => sent bytes to tar instead of filesystem
// use_dict      => change memory usage stragegy
// show_progress => enable/disable bytes count (not always applicable)
int gzUncompress( bool isupdate = false, bool stream_to_tar = false, bool use_dict = true, bool show_progress = true )
{

  log_d("gzUncompress( isupdate = %s, stream_to_tar = %s, use_dict = %s, show_progress = %s)",
    isupdate      ? "true" : "false",
    stream_to_tar ? "true" : "false",
    use_dict      ? "true" : "false",
    show_progress ? "true" : "false"
  );

  if( !tarGzStream.gz->available() ) {
    log_e("[ERROR] in gzUncompress: gz resource doesn't exist!");
    if( targz_halt_on_error ) targz_system_halt();
    return ESP32_TARGZ_STREAM_ERROR;
  }

  size_t output_buffer_size = SPI_FLASH_SEC_SIZE; // 4Kb
  int uzlib_dict_size = 0;
  int res = 0;

  uzlib_init();

  if ( use_dict == true ) {
    uzlib_gzip_dict = new unsigned char[GZIP_DICT_SIZE];
    uzlib_dict_size = GZIP_DICT_SIZE;
    uzLibDecompressor.readDestByte   = NULL;
    log_d("[INFO] gzUncompress tradeoff: faster, used %d bytes of ram (heap after alloc: %d)", GZIP_DICT_SIZE+output_buffer_size, ESP.getFreeHeap());
    //log_w("[%d] alloc() done", ESP.getFreeHeap() );
  } else {
    if( stream_to_tar ) {
      log_e("[ERROR] gz->tar->filesystem streaming requires a gzip dictionnnary");
      if( targz_halt_on_error ) targz_system_halt();
      return ESP32_TARGZ_NEEDS_DICT;
    } else {
      uzLibDecompressor.readDestByte   = gzReadDestByte;
      log_d("[INFO] gz output is file\n");
    }
    //output_buffer_size = SPI_FLASH_SEC_SIZE;
    log_d("[INFO] gzUncompress tradeoff: slower will use %d bytes of ram (heap before alloc: %d)", output_buffer_size, ESP.getFreeHeap());
    uzlib_gzip_dict = NULL;
    uzlib_dict_size = 0;
  }

  uzLibDecompressor.source         = nullptr;
  uzLibDecompressor.readSourceByte = gzReadDestByte;
  uzLibDecompressor.destSize       = 1;
  uzLibDecompressor.log            = targzPrintLoggerCallback;

  res = uzlib_gzip_parse_header(&uzLibDecompressor);
  if (res != TINF_OK) {
    log_e("[ERROR] in gzUncompress: uzlib_gzip_parse_header failed (response code %d!", res);
    if( targz_halt_on_error ) targz_system_halt();
    gzExpanderCleanup();
    return res;
  }

  uzlib_uncompress_init(&uzLibDecompressor, uzlib_gzip_dict, uzlib_dict_size);

  output_buffer = (unsigned char *)calloc( output_buffer_size+1, sizeof(unsigned char) );
  if( !output_buffer ) {
    log_e("[ERROR] can't alloc %d bytes for output buffer", output_buffer_size );
    if( targz_halt_on_error ) targz_system_halt();
    return -1; // TODO : Number this error
  }

  blockmod = output_buffer_size / TAR_BLOCK_SIZE;
  log_d("[INFO] output_buffer_size=%d blockmod=%d", output_buffer_size, blockmod );

  /* decompress a single byte at a time */
  output_position = 0;
  unsigned int outlen = 0;

  if( show_progress ) {
    gzProgressCallback( 0 );
  }

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
        log_v("[INFO] Buffer full, now writing %d bytes (total=%d)", output_buffer_size, outlen);
        gzWriteCallback( output_buffer, output_buffer_size );
        outlen += output_buffer_size;
        output_position = 0;
        //log_w("uzLibDecompressor write done");
      }

      if( show_progress ) {
        uzlib_bytesleft = tarGzStream.output_size - outlen;
        int32_t progress = 100*(tarGzStream.output_size-uzlib_bytesleft) / tarGzStream.output_size;
        gzProgressCallback( progress );
      }

    } while ( res == TINF_OK/*uzlib_bytesleft > 0 */);

    if (res != TINF_DONE) {
      log_w("[GZ WARNING] uzlib_uncompress_chksum return code=%d, premature end at position %d while %d bytes left", res, output_position, uzlib_bytesleft);
    }

    if( output_position > 0 && outlen > 0 ) {
      gzWriteCallback( output_buffer, output_position );
      outlen += output_position;
      output_position = 0;
    }

    if( isupdate && outlen > 0 ) {
      size_t updatable_size = ( outlen/*tarGzStream.output_size*/ + SPI_FLASH_SEC_SIZE-1 ) & ~( SPI_FLASH_SEC_SIZE-1 );
      size_t zerofill_size  = updatable_size - outlen/*tarGzStream.output_size*/;
      if( zerofill_size <= SPI_FLASH_SEC_SIZE ) {
        memset( output_buffer, 0, zerofill_size );
        // zero-fill to fit update.h required binary size
        gzWriteCallback( output_buffer, zerofill_size );
        outlen += zerofill_size;
        output_position = 0;
      }
    }

  }

  if( show_progress ) {
    gzProgressCallback( 100 );
  }

  log_d("decompressed %d bytes", outlen + output_position);

  free( output_buffer );
  gzExpanderCleanup();

  return outlen > 0 ? ESP32_TARGZ_OK : ESP32_TARGZ_STREAM_ERROR;

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
    log_e("Insufficient heap to decompress (available:%d, needed:%d), aborting", ESP.getFreeHeap(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
    //_error = ESP32_TARGZ_HEAP_TOO_LOW;
    //return false;
  } else {
    log_d("Current heap budget (available:%d, needed:%d)", ESP.getFreeHeap(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
  }

  log_d("uzLib expander start!\n");
  fs::File gz = sourceFS.open( sourceFile, FILE_READ );
  if( !gzProgressCallback ) {
    setProgressCallback( defaultProgressCallback );
  }
  if( !gzReadHeader( gz ) ) {
    log_e("[GZ ERROR] in gzExpander: invalid gzip file or not enough space left on device ?");
    gz.close();
    _error = ESP32_TARGZ_UZLIB_INVALID_FILE;
    return false;
  }

  if( destFS.exists( destFile ) ) {
    log_d("[GZ INFO] Deleting %s as it is in the way", destFile);
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
    log_e("gzUncompress returned error code %d", ret);
    _error = (tarGzErrorCode)ret;
    return false;
  }
  log_d("uzLib expander finished!\n");

  outfile = destFS.open( destFile, FILE_READ );
  log_d("Expanded %s to %s (%d bytes)", sourceFile, destFile, outfile.size() );
  outfile.close();

  if( fstotalBytes &&  fsfreeBytes ) {
    log_d("[GZ Info] FreeBytes after expansion=%d\n", fsfreeBytes() );
  }

  return true;
}




// uncompress gz sourceFile directly to untar, no intermediate file
bool gzStreamTarExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFolder )
{
  tarGzClearError();
  setupFSCallbacks();
  if (!tgzLogger ) {
    setLoggerCallback( targzPrintLoggerCallback );
  }

  if( ESP.getFreeHeap() < GZIP_DICT_SIZE+GZIP_BUFF_SIZE*2 ) {
    log_e("Insufficient heap to decompress (available:%d, needed:%d), aborting", ESP.getFreeHeap(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
    _error = ESP32_TARGZ_HEAP_TOO_LOW;
    return false;
  } else {
    log_d("Current heap budget (available:%d, needed:%d)", ESP.getFreeHeap(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
  }

  log_d("uzLib expander start!");
  fs::File gz = sourceFS.open( sourceFile, FILE_READ );
  if( !gzProgressCallback ) {
    setProgressCallback( defaultProgressCallback );
  }
  if( !gzReadHeader( gz ) ) {
    log_e("[GZ ERROR] in gzStreamTarExpander: invalid gzip file or not enough space left on device ?");
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
  lastblock  = false;

  int ret = gzUncompress( false, true );

  gz.close();

  if( ret!=0 ) {
    log_e("gzUncompress returned error code %d", ret);
    _error = (tarGzErrorCode)ret;
    return false;
  }
  log_d("uzLib expander finished!");

  if( fstotalBytes &&  fsfreeBytes ) {
    log_d("[GZ Info] FreeBytes after expansion=%d", fsfreeBytes() );
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
    log_e("Insufficient heap to decompress (available:%d, needed:%d), aborting", ESP.getFreeHeap(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE*2 );
    _error = ESP32_TARGZ_HEAP_TOO_LOW;
    return false;
  }

  log_d("uzLib SPIFFS Updater start!");
  fs::File gz = fs.open( gz_filename, FILE_READ );
  if( !gzReadHeader( gz ) ) {
    log_e("[ERROR] in gzUpdater: Not a valid gzip file");
    if( targz_halt_on_error ) targz_system_halt();
    gz.close();
    _error = ESP32_TARGZ_UZLIB_INVALID_FILE;
    return false;
  }
  if( !gzProgressCallback ) {
    setProgressCallback( defaultProgressCallback );
  }
  tarGzStream.gz = &gz;
  gzWriteCallback = &gzUpdateWriteCallback; // for unzipping direct to flash
  Update.begin( /*UPDATE_SIZE_UNKNOWN*/( ( tarGzStream.output_size + SPI_FLASH_SEC_SIZE-1 ) & ~( SPI_FLASH_SEC_SIZE-1 ) ) );
  // TODO: restore this
  int ret = gzUncompress( true, false, true );
  gz.close();

  if( ret!=0 ) {
    log_e("gzUncompress returned error code %d", ret);
    _error = (tarGzErrorCode)ret;
    return false;
  }

  if ( Update.end( /*true */ ) ) {
    log_d( "OTA done!" );
    if ( Update.isFinished() ) {
      // yay
      log_d("Update finished !");
      ESP.restart();
    } else {
      log_e( "Update not finished? Something went wrong!" );
      _error = ESP32_TARGZ_UPDATE_INCOMPLETE;
      return false;
    }
  } else {
    log_e( "Update Error Occurred. Error #: %u", Update.getError() );
    _error = (tarGzErrorCode)(Update.getError()-20); // "-20" offset is Update error id to esp32-targz error id
    return false;
  }
  log_d("uzLib filesystem Updater finished!");
  _error = (tarGzErrorCode)ret;
  return true;
}


#ifdef ESP32

  // uncompress gz stream (file or HTTP) to flash (expected to be a valid Arduino compiled binary sketch)
  bool gzStreamUpdater( Stream *stream, size_t uncompressed_size )
  {

    if( !gzProgressCallback ) {
      setProgressCallback( defaultProgressCallback );
    }

    size_t size = stream->available();
    if( ! size ) {
      log_e("Bad stream, aborting");
      _error = ESP32_TARGZ_STREAM_ERROR;
      return false;
    }

    tarGzStream.gz = stream;

    gzWriteCallback = &gzUpdateWriteCallback; // for unzipping direct to flash

    if( uncompressed_size == 0 || uncompressed_size == UPDATE_SIZE_UNKNOWN ) {
      Update.begin( UPDATE_SIZE_UNKNOWN );
    } else {
      Update.begin( ( ( uncompressed_size + SPI_FLASH_SEC_SIZE-1 ) & ~( SPI_FLASH_SEC_SIZE-1 ) ) );
    }

    int ret = gzUncompress( true, false, true, false );

    if( ret!=0 ) {
      log_e("gzHTTPUpdater returned error code %d", ret);
      _error = (tarGzErrorCode)ret;
      return false;
    }

    if ( Update.end( true ) ) {
      log_d( "OTA done!" );
      if ( Update.isFinished() ) {
        // yay
        log_d("Update finished !");
        ESP.restart();
      } else {
        log_e( "Update not finished? Something went wrong!" );
        _error = ESP32_TARGZ_UPDATE_INCOMPLETE;
        return false;
      }
    } else {
      log_e( "Update Error Occurred. Error #: %u", Update.getError() );
      _error = (tarGzErrorCode)(Update.getError()-20); // "-20" offset is Update error id to esp32-targz error id
      return false;
    }
    log_d("uzLib filesystem Updater finished!\n");
    _error = (tarGzErrorCode)ret;

    return true;
  }

#endif















// unzip sourceFS://sourceFile.tar.gz contents into destFS://destFolder
bool tarGzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFolder, const char* tempFile )
{
  tarGzClearError();
  setupFSCallbacks();

  if( tempFile != nullptr ) {

    log_v("[INFO] tarGzExpander will use a separate file: %s)", tempFile );

    mkdirp( &destFS, tempFile );

    if( gzExpander(sourceFS, sourceFile, destFS, tempFile) ) {
      log_d("[INFO] heap before tar-expanding: %d)", ESP.getFreeHeap());
      if( tarExpander(destFS, tempFile, destFS, destFolder) ) {
        // yay
      }
    }
    if( destFS.exists( tempFile ) ) destFS.remove( tempFile );

    return !tarGzHasError();
  } else {

    log_v("[INFO] tarGzExpander will use no intermediate file\n" );

    return gzStreamTarExpander( sourceFS, sourceFile, destFS, destFolder );
  }
}




#if defined ESP32

  // uncompress tar+gz stream (file or HTTP) to filesystem without intermediate tar file
  bool tarGzStreamExpander( Stream *stream, fs::FS &destFS, const char* destFolder )
  {

    if( !stream->available() ) {
      log_e("Bad stream, aborting");
      Serial.printf("0x%02x", stream->read() );
      Serial.printf("0x%02x", stream->read() );
      Serial.println();
      _error = ESP32_TARGZ_STREAM_ERROR;
      return false;
    }

    if( !gzProgressCallback ) {
      setProgressCallback( defaultProgressCallback );
    }

    tarGzStream.gz = stream;

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
    tar_error_logger      = tgzLogger; // targzPrintLoggerCallback or tgzLogger
    tar_debug_logger      = tgzLogger; // comment this out if too verbose
    tinyUntarReadCallback = &gzFeedTarBuffer;
    gzWriteCallback       = &gzProcessTarBuffer;

    totalFiles = 0;
    totalFolders = 0;

    firstblock = true;
    lastblock  = false;

    int ret = gzUncompress( false, false, true, false );

    if( ret!=0 ) {
      log_e("tarGzStreamExpander returned error code %d", ret);
      _error = (tarGzErrorCode)ret;
      return false;
    }

    return true;
  }


#endif




// generate hex view in the console, one call per line
void hexDumpData( const char* buff, size_t buffsize, uint32_t output_size )
{
  static size_t totalBytes = 0;
  String bytesStr = "";
  String binaryStr = "";
  char byteToStr[32];

  for( size_t i=0; i<buffsize; i++ ) {
    sprintf( byteToStr, "%02X", buff[i] );
    bytesStr  += String( byteToStr ) + String(" ");
    if( isprint( buff[i] ) ) {
      binaryStr += String( buff[i] );
    } else {
      binaryStr += ".";
    }
  }
  sprintf( byteToStr, "[0x%04X - 0x%04X] ",  totalBytes, totalBytes+buffsize);
  totalBytes += buffsize;
  if( buffsize < output_size ) {
    for( size_t i=0; i<output_size-buffsize; i++ ) {
      bytesStr  += "-- ";
      binaryStr += ".";
    }
  }
  Serial.println( byteToStr + bytesStr + " " + binaryStr );
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
    //String bytesStr  = "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00";
    //String binaryStr = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    //String addrStr = "[0x0000 - 0x0000] ";
    //char byteToStr[32];
    //size_t totalBytes = 0;
    while( bytes_read > 0 ) {

      hexDumpData( buff, bytes_read, output_size );
      /*
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
        for( size_t i=0; i<output_size-bytes_read; i++ ) {
          bytesStr  += "-- ";
          binaryStr += ".";
        }
      }
      Serial.println( byteToStr + bytesStr + " " + binaryStr );
      */
      bytes_read = binFile.readBytes( buff, output_size );
    }
  } else {
    Serial.printf("Ignoring file %s (%d bytes)", filename, binFile.size() );
  }
  binFile.close();
}

// get a directory listing of a given filesystem
#if defined ESP32

  void tarGzListDir( fs::FS &fs, const char * dirName, uint8_t levels, bool hexDump )
  {
    File root = fs.open( dirName, FILE_READ );
    if( !root ) {
      log_e("[ERROR] in tarGzListDir: Can't open %s dir", dirName );
      if( targz_halt_on_error ) targz_system_halt();
      return;
    }
    if( !root.isDirectory() ) {
      log_e("[ERROR] in tarGzListDir: %s is not a directory", dirName );
      if( targz_halt_on_error ) targz_system_halt();
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
