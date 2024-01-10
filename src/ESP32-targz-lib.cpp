/*\

  MIT License

  Copyright (c) 2020-now tobozo

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


  Tradeoffs :
    - speed: fast decompression needs 32Kb memory
    - memory: reducing memory use by dropping the gz_dictionary is VERY slow and prevents tar->gz->filesystem direct streaming
    - space: limited filesystems (<512KB spiffs) need tar->gz->filesystem direct streaming

\*/

#ifdef ESP8266
// stop GCC from whining about unused ESP32 signatures
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "ESP32-targz-lib.hpp"

struct TarGzIO
{
  Stream *gz;
  Stream *tar;
  Stream *output;
  size_t gz_size;
  size_t tar_size;
  size_t output_size;
};

TarGzIO tarGzIO;

static fs::File untarredFile;
static fs::FS *tarFS = nullptr;

TAR::entry_callbacks_t tarCallbacks;

void* (*tgz_malloc)(size_t size) = nullptr;
void* (*tgz_calloc)(size_t n, size_t size) = nullptr;
void* (*tgz_realloc)(void *ptr, size_t size) = nullptr;
bool tgz_use_psram = false;

void          (*tgzLogger)( const char* format, ...) = nullptr;
static size_t (*fstotalBytes)() = nullptr;
static size_t (*fsfreeBytes)()  = nullptr;
static void   (*fsSetupSizeTools)( fsTotalBytesCb cbt, fsFreeBytesCb cbf ) = nullptr;

static void   (*tarProgressCallback)( uint8_t progress ) = nullptr;
static void   (*tarMessageCallback)( const char* format, ...) = nullptr;
static bool   (*tarSkipThisEntryOut)( TAR::header_translated_t *header ) = nullptr;
static bool   (*tarSkipThisEntryIn)( TAR::header_translated_t *header ) = nullptr;
static void   (*tarStatusProgressCallback)( const char* name, size_t size, size_t total_unpacked ) = nullptr;
static bool   tarSkipThisEntry = false;

static void   (*gzMessageCallback)( const char* format, ...) = nullptr;
static void   (*gzProgressCallback)( uint8_t progress ) = nullptr;
static bool   (*gzWriteCallback)( unsigned char* buff, size_t buffsize ) = nullptr;
static unsigned int (*gzReadDestByte)(int offset, unsigned char *out);


static const char* tarDestFolder = nullptr;
static unsigned char __attribute__((aligned(4))) *output_buffer = nullptr; // gz write buffer
static unsigned char *uzlib_gzip_dict = nullptr; // gz dictionnary buffer
static struct GZ::TINF_DATA uzLibDecompressor; // uzlib object

static tarGzErrorCode _error = ESP32_TARGZ_OK;

static bool     targz_halt_on_error = false;
static bool     firstblock = true; // for gzProcessTarBuffer
static bool     lastblock = false; // for gzProcessTarBuffer
static uint32_t targz_read_timeout = 10000; // ms, should be larger than stream timeout
static size_t   tarCurrentFileSize = 0;
static size_t   tarCurrentFileSizeProgress = 0;
static size_t   tarTotalSize = 0;
static size_t   min_output_buffer_size = TAR_BLOCK_SIZE;
static int32_t  untarredBytesCount = 0;
static size_t   totalFiles = 0;
static size_t   totalFolders = 0;
static int64_t  uzlib_bytesleft = 0;
static int64_t  stream_bytesleft = 0;
static uint32_t output_position = 0;  // position in output_buffer
static uint16_t blockmod = GZIP_BUFF_SIZE / TAR_BLOCK_SIZE; // how many tar blocks can fit in the gzip buffer
static uint16_t gzTarBlockPos = 0; // tar block number being decompressed
static size_t   tarReadGzStreamBytes = 0;
static char*    tar_file_path = nullptr; // temporary storage for filenames
#if defined HAS_OTA_SUPPORT
  static bool     tarBlockIsUpdateData = false;
#endif


#if defined ESP32
  static bool unTarDoHealthChecks = true; // set to false for faster writes
#elif defined ESP8266 || defined ARDUINO_ARCH_RP2040
  static bool unTarDoHealthChecks = false; // ESP8266 is unstable with health checks
  void vTaskDelay(int ms) { delay(ms); }   // ESP8266 has no OS
#else
  static bool unTarDoHealthChecks = false;
  void vTaskDelay(int ms) { delay(ms); }
#endif



static bool halt_on_error()
{
  return targz_halt_on_error;
}

static void targz_system_halt()
{
  log_e("System halted after error code #%d", _error); while(1) { yield(); }
}

static void setError( tarGzErrorCode code )
{
  _error = code;
  if( _error != ESP32_TARGZ_OK && halt_on_error() ) targz_system_halt();
}



BaseUnpacker::BaseUnpacker()
{

  fstotalBytes = nullptr;
  fsfreeBytes  = nullptr;
  fsSetupSizeTools = nullptr;
  tarFS = nullptr;
  tarDestFolder = nullptr;
  output_buffer = nullptr;
  _error = ESP32_TARGZ_OK;

  tarCurrentFileSize = 0;
  tarCurrentFileSizeProgress = 0;
  untarredBytesCount = 0;
  totalFiles = 0;
  totalFolders = 0;

  uzlib_gzip_dict = nullptr;
  uzlib_bytesleft = 0;
  stream_bytesleft = 0;
  output_position = 0;  //position in output_buffer
  blockmod = GZIP_BUFF_SIZE / TAR_BLOCK_SIZE;
  gzTarBlockPos = 0;
  tarReadGzStreamBytes = 0;

  tgz_malloc  = malloc;
  tgz_calloc  = calloc;
  tgz_realloc = realloc;
  tgz_use_psram = false;

}


void BaseUnpacker::setReadTimeout( uint32_t read_timeout )
{
  targz_read_timeout = read_timeout;
}

#ifdef ESP32
bool BaseUnpacker::setPsram( bool enable )
{
  if( enable ) {
    if( psramInit() ) {
      tgz_malloc  = ps_malloc;
      tgz_calloc  = ps_calloc;
      tgz_realloc = ps_realloc;
      tgz_use_psram = true;
      log_v("Enabled Psram for uzlib dictionary");
      unTarDoHealthChecks = false;
      return true;
    }
  } else {
    tgz_malloc  = malloc;
    tgz_calloc  = calloc;
    tgz_realloc = realloc;
    log_v("Disabled Psram for uzlib dictionary");
    return true;
  }
  return false;
}
#endif


void BaseUnpacker::setGeneralError( tarGzErrorCode code )
{
  setError( code );
}

void BaseUnpacker::haltOnError( bool halt )
{
  targz_halt_on_error = halt;
}


int8_t BaseUnpacker::tarGzGetError()
{
  return (int8_t)_error;
}


void BaseUnpacker::tarGzClearError()
{
  _error = ESP32_TARGZ_OK;
}


bool BaseUnpacker::tarGzHasError()
{
  return _error != ESP32_TARGZ_OK;
}


void BaseUnpacker::defaultTarStatusProgressCallback( const char* name, size_t size, size_t total_unpacked )
{
  Serial.printf("[TAR] %-64s %8d bytes - %8d Total bytes\n", name, size, total_unpacked );
}


// unpack sourceFS://fileName.tar contents to destFS::/destFolder/
void BaseUnpacker::defaultProgressCallback( uint8_t progress )
{
  static int8_t uzLibLastProgress = -1;
  if( uzLibLastProgress != progress ) {
    if( uzLibLastProgress == -1 ) {
      Serial.print("Progress: 0% ▓");
    }
    uzLibLastProgress = progress;
    switch( progress ) {
      //case   0: Serial.print("0% ▓");  break;
      case  25: Serial.print(" 25% ");break;
      case  50: Serial.print(" 50% ");break;
      case  75: Serial.print(" 75% ");break;
      case 100: Serial.print("▓ 100%\n"); uzLibLastProgress = -1; break;
      default: if( progress > 0 && progress < 100) Serial.print( "▓" ); break;
    }
  }
}


// progress callback for TAR, leave empty for less console output
void BaseUnpacker::tarNullProgressCallback( CC_UNUSED uint8_t progress )
{
  // print( message );
  yield();
}


// progress callback for GZ, leave empty for less console output
void BaseUnpacker::targzNullProgressCallback( CC_UNUSED uint8_t progress )
{
  // printf("Progress: %d", progress );
  yield();
}


// error/warning/info NULL logger, for less console output
void BaseUnpacker::targzNullLoggerCallback( CC_UNUSED const char* format, ...)
{
  //va_list args;
  //va_start(args, format);
  //vprintf(format, args);
  //va_end(args);
  yield();
}


// error/warning/info FULL logger, for more console output
void BaseUnpacker::targzPrintLoggerCallback(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}


// set totalBytes() function callback
void BaseUnpacker::setFsTotalBytesCb( fsTotalBytesCb cb )
{
  log_v("Assigning setFsTotalBytesCb callback : 0x%8x", (uint)cb );
  fstotalBytes = cb;
}


// set freelBytes() function callback
void BaseUnpacker::setFsFreeBytesCb( fsFreeBytesCb cb )
{
  log_v("Assigning setFsFreeBytesCb callback : 0x%8x", (uint)cb );
  fsfreeBytes = cb;
}


// set logger callback
void BaseUnpacker::setLoggerCallback( genericLoggerCallback cb )
{
  log_v("Assigning debug logger callback : 0x%8x", (uint)cb );
  tgzLogger = cb;
}


// private (enables)
void BaseUnpacker::initFSCallbacks()
{
  if( fsSetupSizeTools && fstotalBytes && fsfreeBytes ) {
    log_v("Setting up fs size tools");
    fsSetupSizeTools( fstotalBytes, fsfreeBytes );
  } else {
    log_v("Skipping fs size tools setup");
  }
}


// public (assigns)
void BaseUnpacker::setupFSCallbacks( fsTotalBytesCb cbt, fsFreeBytesCb cbf )
{
  setFsTotalBytesCb( cbt );
  setFsFreeBytesCb( cbf );
  if( fsSetupSizeTools != NULL ) {
    log_v("deleting fsSetupSizeTools");
    fsSetupSizeTools = NULL;
  }
  log_v("Assigning lambda to fsSetupSizeTools");
  fsSetupSizeTools = []( fsTotalBytesCb cbt, fsFreeBytesCb cbf ) {
    log_v("Calling fsSetupSizeTools lambda");
    setFsTotalBytesCb( cbt );
    setFsFreeBytesCb( cbf );
  };
}


// generate hex view in the console, one call per line
void BaseUnpacker::hexDumpData( const char* buff, size_t buffsize, uint32_t output_size )
{
  static size_t totalBytes = 0;
  String bytesStr = "";
  String binaryStr = "";
  char* byteToStr = new char[output_size];

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
  delete[] byteToStr;
}


// show the contents of a given file as a hex dump
void BaseUnpacker::hexDumpFile( fs::FS &fs, const char* filename, uint32_t output_size )
{
  File binFile = fs.open( filename, FILE_READ );
  //log_w("File size : %d", binFile.size() );
  // only dump small files
  if( binFile.size() > 0 ) {
    Serial.printf("Showing file %s (%d bytes) md5: %s\n", filename, binFile.size(), MD5Sum::fromFile( binFile ) );
    binFile.seek(0);
    char* buff = new char[output_size];
    uint8_t bytes_read = binFile.readBytes( buff, output_size );
    while( bytes_read > 0 ) {
      hexDumpData( buff, bytes_read, output_size );
      bytes_read = binFile.readBytes( buff, output_size );
    }
    delete[] buff;
  } else {
    Serial.printf("Ignoring file %s (%d bytes)", filename, binFile.size() );
  }
  binFile.close();
}


// get a directory listing of a given filesystem
#if defined ESP32

  void BaseUnpacker::tarGzListDir( fs::FS &fs, const char * dirName, uint8_t levels, bool hexDump )
  {
    File root = fs.open( dirName, FILE_READ );
    if( !root ) {
      log_e("[ERROR] in tarGzListDir: Can't open %s dir", dirName );
      if( halt_on_error() ) targz_system_halt();
      return;
    }
    if( !root.isDirectory() ) {
      log_e("[ERROR] in tarGzListDir: %s is not a directory", dirName );
      if( halt_on_error() ) targz_system_halt();
      return;
    }
    File file = root.openNextFile();
    while( file ) {
      if( file.isDirectory() ) {
        Serial.printf( "[DIR]  %s\n", targzFSFilePath(&file) );
        if( levels && levels > 0  ) {
          tarGzListDir( fs, targzFSFilePath(&file), levels -1, hexDump );
        }
      } else {
        Serial.printf( "[FILE] %-32s %8d bytes - md5:%s\n", targzFSFilePath(&file), file.size(), MD5Sum::fromFile( file ) );
        if( hexDump ) {
          hexDumpFile( fs, targzFSFilePath(&file) );
        }
      }
      file = root.openNextFile();
    }
  }

#elif defined ESP8266


  void BaseUnpacker::printDirectory(fs::FS &fs, File dir, int numTabs, uint8_t levels, bool hexDump)
  {
    while (true) {

      File entry =  dir.openNextFile();
      if (! entry) {
        // no more files
        break;
      }
      for (uint8_t i = 0; i < numTabs; i++) {
        Serial.print("  ");
      }

      if (entry.isDirectory()) {
        if( levels > 0 ) {
          Serial.printf( "[DIR] %s\n", targzFSFilePath(&entry) );
          printDirectory(fs, entry, numTabs + 1, levels-1, hexDump );
        }
      } else {
        if( entry.size() > 0 ) {
          Serial.printf( "[FILE] %-32s %8d bytes - md5:%s\n", targzFSFilePath(&entry), entry.size(), MD5Sum::fromFile( entry ) );
        } else {
          Serial.printf( "[????] %-32s \n", targzFSFilePath(&entry) );
        }

        if( hexDump ) {
          hexDumpFile( fs, targzFSFilePath(&entry) );
        }

      }
      entry.close();
    }
  }

  void BaseUnpacker::tarGzListDir(fs::FS &fs, const char * dirname, uint8_t levels, bool hexDump)
  {
    //void( hexDump ); // not used (yet?) with ESP82
    Serial.printf("Listing directory %s with level %d\n", dirname, levels);

    File root = fs.open(dirname, "r");
    if( !root.isDirectory() ){
      log_e( "%s is not a directory", dirname );
      return;
    }
    printDirectory(fs, root, 0, levels, hexDump );
    return;
  }



#endif // defined( ESP8266 )








TarUnpacker::TarUnpacker()
{
  #if __has_include(<PSRamFS.h>)
    log_w("Implicitely disabling health checks on PSRamFS");
    unTarDoHealthChecks = false; // disable that with psramFS
  #endif
  tar_file_path = (char*)malloc(256);
  if( tar_file_path == NULL ) {
    log_e("Failed to allocate 256 bytes, halting");
    targz_halt_on_error = true;
    setError( ESP32_TARGZ_HEAP_TOO_LOW );
  }
}

TarUnpacker::~TarUnpacker()
{
  free( tar_file_path );
}


void TarUnpacker::setTarStatusProgressCallback( tarStatusProgressCb cb )
{
  tarStatusProgressCallback = cb;
}


// set progress callback for TAR
void TarUnpacker::setTarProgressCallback( genericProgressCallback cb )
{
  log_v("Assigning tar progress callback : 0x%8x", (uint)cb );
  tarProgressCallback = cb;
}


// set tar unpacker message callback
void TarUnpacker::setTarMessageCallback( genericLoggerCallback cb )
{
  log_v("Assigning tar message callback : 0x%8x", (uint)cb );
  tarMessageCallback = cb;
}


// exclude / include based on tar header properties (filename, size, type)
void TarUnpacker::setTarExcludeFilter( tarExcludeFilter cb )
{
  log_v("Assigning tar filename exclude filter callback : 0x%8x", (uint)cb );
  tarSkipThisEntryOut = cb;
}
void TarUnpacker::setTarIncludeFilter( tarIncludeFilter cb )
{
  log_v("Assigning tar filename include filter callback : 0x%8x", (uint)cb );
  tarSkipThisEntryIn  = cb;
}


// safer but slower
void TarUnpacker::setTarVerify( bool verify )
{
  log_v("Setting tar verify : %s", verify ? "true" : "false" );
  #if __has_include(<PSRamFS.h>)
    log_d("Implicitely ignoring health checks on PSRamFS");
    unTarDoHealthChecks = false; // disable that with psramFS
  #else
    unTarDoHealthChecks = verify;
  #endif

}




int TarUnpacker::tarHeaderCallBack( TAR::header_translated_t *header,  CC_UNUSED int entry_index,  CC_UNUSED void *context_data )
{
  dump_header(header);

  tarSkipThisEntry = false;

  if( tarSkipThisEntryOut ) {
    if( tarSkipThisEntryOut( header ) ) {
      tgzLogger("[TAR] Skipping: %s (filter 'Out' matches)\n", header->filename );
      tarSkipThisEntry = true;
    }
  }

  if( tarSkipThisEntryIn ) {
    if( !tarSkipThisEntryIn( header ) ) {
      tgzLogger("[TAR] Skipping: %s (filter 'In' does not match)\n", header->filename );
      tarSkipThisEntry = true;
    }
  }

  // https://github.com/tobozo/ESP32-targz/issues/33
  if( header->type == TAR::T_NORMAL  || header->type == TAR::T_EXTENDED ) {

    if( fstotalBytes &&  fsfreeBytes ) {
      size_t freeBytes  = fsfreeBytes();
      if( freeBytes < header->filesize ) {
        // Abort before the partition is smashed!
        log_e("[TAR ERROR] Not enough space left on device (%llu bytes required / %d bytes available)!", header->filesize, freeBytes );
        return ESP32_TARGZ_FS_FULL_ERROR;
      }
    } else {
      #if defined WARN_LIMITED_FS
        log_w("[TAR WARNING] Can't check target medium for free space (required:%llu, free:\?\?), will try to expand anyway", header->filesize );
      #endif
    }

    if( !tarSkipThisEntry ) {
      memset( tar_file_path, 0, 256 );
      // check that TAR path does not start with "./" and truncate if necessary
      if( header->filename[0] == '.' && header->filename[1] == '/' ) {
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf( tar_file_path, 101, "%s", header->filename ); // TAR paths are limited to 100 chars
        snprintf( header->filename, 101, "%s", &tar_file_path[2] );
        #pragma GCC diagnostic pop
      }
      memset( tar_file_path, 0, 256 );
      if( strcmp( tarDestFolder, FOLDER_SEPARATOR ) != 0 ) {
        // destination folder isn't root folder, prefix !
        strcat(tar_file_path, tarDestFolder);
        // append slash if missing
        if( tarDestFolder[strlen(tarDestFolder)-1] != '/' ) {
          strcat(tar_file_path, FOLDER_SEPARATOR);
        }
      }
      // only append slash if destination folder does not end with a slash
      if( tar_file_path[strlen(tar_file_path)-1] != FOLDER_SEPARATOR[0] ) {
        strcat(tar_file_path, FOLDER_SEPARATOR);
      }

      strcat(tar_file_path, header->filename );

      if( tarFS->exists( tar_file_path ) ) {
        // file will be truncated
        /*
        untarredFile = tarFS->open( file_path, FILE_READ );
        bool isdir = untarredFile.isDirectory();
        untarredFile.close();
        if( isdir ) {
          log_d("[TAR DEBUG] Keeping %s folder", file_path);
        } else {
          log_d("[TAR DEBUG] Deleting %s as it is in the way", file_path);
          tarFS->remove( file_path );
        }
        */
      } else {
        // create directory (recursively if necessary)
        mkdirp( tarFS, tar_file_path );
      }
      //TODO: limit this check to SPIFFS/LittleFS only
      if( strlen( tar_file_path ) > 32 ) {
        // WARNING: SPIFFS LIMIT
        #if defined WARN_LIMITED_FS
          log_w("[TAR WARNING] file path is longer than 32 chars (SPIFFS limit) and may fail: %s", tar_file_path);
          setError( ESP32_TARGZ_TAR_ERR_FILENAME_TOOLONG ); // don't break untar for that
        #endif
      } else {
        log_v("[TAR] Creating %s", tar_file_path);
      }

      untarredFile = tarFS->open(tar_file_path, FILE_WRITE);
      if(!untarredFile) {
        log_e("[ERROR] in tarHeaderCallBack: Could not open [%s] for write, filesystem full?", tar_file_path);
        setError( ESP32_TARGZ_FS_ERROR );
        return ESP32_TARGZ_FS_ERROR;
      }
      tarGzIO.output = &untarredFile;
    } else {
      log_v("[TAR FILTER] Skipped file/folder creation for: %s.", header->filename);
    }

    tarCurrentFileSize = header->filesize; // for progress
    tarCurrentFileSizeProgress = 0; // for progress

    tarTotalSize += header->filesize;

    if( tarStatusProgressCallback && !tarSkipThisEntry ) {
      tarStatusProgressCallback( header->filename, header->filesize, tarTotalSize );
    }
    if( tarTotalSize == header->filesize && !tarSkipThisEntry )
      tarProgressCallback( 0 );

  } else {

    switch( header->type ) {
      case TAR::T_HARDLINK:       log_v("Ignoring hard link to %s.", header->filename); break;
      case TAR::T_SYMBOLIC:       log_v("Ignoring sym link to %s.", header->filename); break;
      case TAR::T_CHARSPECIAL:    log_v("Ignoring special char."); break;
      case TAR::T_BLOCKSPECIAL:   log_v("Ignoring special block."); break;
      case TAR::T_DIRECTORY:      log_d("Entering %s directory.", header->filename);
        //tarMessageCallback( "Entering %s directory\n", header->filename );
        if( tarStatusProgressCallback && !tarSkipThisEntry ) {
          tarStatusProgressCallback( header->filename, 0, tarTotalSize );
        }
        totalFolders++;
      break;
      case TAR::T_FIFO:           log_v("Ignoring FIFO request."); break;
      case TAR::T_CONTIGUOUS:     log_v("Ignoring contiguous data to %s.", header->filename); break;
      case TAR::T_GLOBALEXTENDED: log_v("Ignoring global extended data."); break;
      //case TAR::T_EXTENDED:       log_d("Ignoring extended data."); break;
      case TAR::T_OTHER: default: log_v("Ignoring unrelevant data.");       break;
    }

  }

  return ESP32_TARGZ_OK;
}





int TarUnpacker::tarEndCallBack( TAR::header_translated_t *header, CC_UNUSED int entry_index, CC_UNUSED void *context_data)
{
  int ret = ESP32_TARGZ_OK;

  if( untarredFile ) {
    if( unTarDoHealthChecks ) {
      memset( tar_file_path, 0, 256 );
      snprintf( tar_file_path, 256, "%s", targzFSFilePath(&untarredFile) );
      size_t pos = untarredFile.position();
      untarredFile.close();
      // health check 1: file existence
      if( !tarFS->exists( tar_file_path ) ) {
        log_e("[TAR ERROR] File %s was not created although it was properly decoded, path is too long ?", tar_file_path );
        return ESP32_TARGZ_FS_WRITE_ERROR;
      }
      // health check 2: compare stream buffer position with speculated file size
      if( pos != header->filesize ) {
        log_e("[TAR ERROR] File size and data size do not match (%d vs %d)!", (int)pos, (int)header->filesize);
        return ESP32_TARGZ_FS_WRITE_ERROR;
      }
      // health check 3: reopen file to check size on filesystem
      untarredFile = tarFS->open(tar_file_path, FILE_READ);
      size_t tmpsize = untarredFile.size();
      if( !untarredFile ) {
        log_e("[TAR ERROR] Failed to re-open %s for size reading", tar_file_path);
        return ESP32_TARGZ_FS_READSIZE_ERROR;
      }
      // health check 4: see if everyone (buffer, stream, filesystem) agree
      if( (header->filesize>0 && tmpsize == 0) || header->filesize != tmpsize || pos != tmpsize ) {
        log_e("[TAR ERROR] Byte sizes differ between written file %s (%d), tar headers (%d) and/or stream buffer (%d) !!", tar_file_path, (int)tmpsize, (int)header->filesize, (int)pos );
        untarredFile.close();
        return ESP32_TARGZ_FS_ERROR;
      }
      log_d("Expanded %s (%d bytes)", tar_file_path, (int)tmpsize );
    }

    untarredFile.close();

    tarTotalSize = 0;
    if( header->type != TAR::T_DIRECTORY ) {
      tarTotalSize += header->filesize;
    }

    tarProgressCallback( 100 );
    log_d("Total expanded bytes: %d, heap free: %d", (int)tarTotalSize, HEAP_AVAILABLE() );

    tarMessageCallback( "%s", header->filename );

  } else {
    if( tarSkipThisEntry ) {
      log_v("[TAR FILTER] Skipped file close for: %s.", header->filename);
    } else {
      log_v("[TAR INFO] tarEndCallBack: nofile for `%s`", header->filename );
    }
  }
  totalFiles++;

  return ret;
}



#if defined HAS_OTA_SUPPORT

  int TarUnpacker::tarHeaderUpdateCallBack(TAR::header_translated_t *header,  int entry_index,  void *context_data)
  {
    dump_header(header);

    // https://github.com/tobozo/ESP32-targz/issues/33
    if( header->type == TAR::T_NORMAL  || header->type == TAR::T_EXTENDED ) {

      int partition = -1;

      if( String( header->filename ).endsWith("ino.bin") ) {
        // partition = app
        partition = U_FLASH;
      } else if( String( header->filename ).endsWith("spiffs.bin")
              || String( header->filename ).endsWith("littlefs.bin")
              || String( header->filename ).endsWith("ffat.bin")  ) {
        partition = U_PART;
      } else {
        // not relevant to Update
        // TODO: provide action callbacks (e.g. file has meta info)
        return ESP32_TARGZ_OK;
      }

      // check that header->filesize is smaller than partition available size
      if( !Update.begin( header->filesize/*UPDATE_SIZE_UNKNOWN*/, partition ) ) {
        setError( (tarGzErrorCode)(Update.getError()-20) ); // "-20" offset is Update error id to esp32-targz error id
        return (Update.getError()-20);
      }

      tarCurrentFileSize = header->filesize; // for progress
      tarCurrentFileSizeProgress = 0; // for progress
      tarBlockIsUpdateData = true;

      tarTotalSize += header->filesize;
      if( tarStatusProgressCallback ) {
        tarStatusProgressCallback( header->filename, header->filesize, tarTotalSize );
      }
      if( tarTotalSize == header->filesize )
        tarProgressCallback( 0 );

    }/* else {

      switch( header->type ) {
        case TAR::T_HARDLINK:       log_d("Ignoring hard link to %s.", header->filename); break;
        case TAR::T_SYMBOLIC:       log_d("Ignoring sym link to %s.", header->filename); break;
        case TAR::T_CHARSPECIAL:    log_d("Ignoring special char."); break;
        case TAR::T_BLOCKSPECIAL:   log_d("Ignoring special block."); break;
        case TAR::T_DIRECTORY:      log_d("Entering %s directory.", header->filename);
          //tarMessageCallback( "Entering %s directory\n", header->filename );
          if( tarStatusProgressCallback ) {
            tarStatusProgressCallback( header->filename, 0, tarTotalSize );
          }
          totalFolders++;
        break;
        case TAR::T_FIFO:           log_d("Ignoring FIFO request."); break;
        case TAR::T_CONTIGUOUS:     log_d("Ignoring contiguous data to %s.", header->filename); break;
        case TAR::T_GLOBALEXTENDED: log_d("Ignoring global extended data."); break;
        case TAR::T_EXTENDED:       log_d("Ignoring extended data."); break;
        case TAR::T_OTHER: default: log_d("Ignoring unrelevant data.");       break;
      }

    }*/

    return ESP32_TARGZ_OK;
  }


  int TarUnpacker::tarEndUpdateCallBack( TAR::header_translated_t *header, int entry_index, void *context_data)
  {
    int ret = ESP32_TARGZ_OK;

    if ( !Update.end( true ) ) {
      Update.printError(Serial);
      log_e( "Update Error Occurred. Error #: %u", Update.getError() );
      setError( (tarGzErrorCode)(Update.getError()-20) ); // "-20" offset is Update error id to esp32-targz error id
      return (Update.getError()-20);
    }

    if ( !Update.isFinished() ) {
      log_e("Update incomplete");
      setError( ESP32_TARGZ_UPDATE_INCOMPLETE );
      return ESP32_TARGZ_UPDATE_INCOMPLETE;
    }

    log_d("Update finished !");
    Update.end();

    tarBlockIsUpdateData = false;
    tarProgressCallback( 100 );
    //log_d("Total expanded bytes: %d, heap free: %d", (int)tarTotalSize, HEAP_AVAILABLE() );
    tarMessageCallback( "%s", header->filename );
    totalFiles++;

    return ret;
  }



  int TarUnpacker::tarStreamWriteUpdateCallback(TAR::header_translated_t *header, int entry_index, void *context_data, unsigned char *block, int length)
  {
    if( tarBlockIsUpdateData ) {
      int wlen = Update.write( block, length );
      if( wlen != length ) {
        //tgzLogger("\n");
        log_e("[TAR ERROR] Written length differs from buffer length (unpacked bytes:%d, expected: %d, returned: %d)!\n", untarredBytesCount, length, wlen );
        return ESP32_TARGZ_FS_ERROR;
      }
      untarredBytesCount+=wlen;
      // file unpack progress
      log_v("[TAR INFO] tarStreamWriteCallback wrote %d bytes to %s", length, header->filename );
      tarCurrentFileSizeProgress += wlen;
      if( tarCurrentFileSize > 0 ) {
        // this is a per-file progress, not an overall progress !
        int32_t progress = (100*tarCurrentFileSizeProgress) / tarCurrentFileSize;
        if( progress != 100 && progress != 0 ) {
          tarProgressCallback( progress );
        }
      }
    }
    return ESP32_TARGZ_OK;
  }

#endif



// tinyUntarReadCallback
int TarUnpacker::tarStreamReadCallback( unsigned char* buff, size_t buffsize )
{
  return tarGzIO.tar->readBytes( buff, buffsize );
}


int TarUnpacker::tarStreamWriteCallback(TAR::header_translated_t *header, int entry_index, void *context_data, unsigned char *block, int length)
{

  if( tarSkipThisEntry ) {
    log_v("[TAR FILTER] Skipping data bits from: %s.", header->filename);
    untarredBytesCount += length;
    tarCurrentFileSizeProgress += length;
    return ESP32_TARGZ_OK;
  }

  if( tarGzIO.output ) {
    int wlen = tarGzIO.output->write( block, length );
    if( wlen != length ) {
      //tgzLogger("\n");
      log_e("[TAR ERROR] Written length differs from buffer length (unpacked bytes:%d, expected: %d, returned: %d)!\n", untarredBytesCount, length, wlen );
      return ESP32_TARGZ_FS_ERROR;
    }
    untarredBytesCount+=wlen;
    // file unpack progress
    log_v("[TAR INFO] tarStreamWriteCallback wrote %d bytes to %s", length, header->filename );
    tarCurrentFileSizeProgress += wlen;
    if( tarCurrentFileSize > 0 ) {
      // this is a per-file progress, not an overall progress !
      int32_t progress = (100*tarCurrentFileSizeProgress) / tarCurrentFileSize;
      if( progress != 100 && progress != 0 ) {
        tarProgressCallback( progress );
      }
    }
  }
  return ESP32_TARGZ_OK;
}



// unpack sourceFS://fileName.tar contents to destFS::/destFolder/
bool TarUnpacker::tarStreamExpander( Stream *stream, size_t streamSize, fs::FS &destFS, const char* destFolder )
{

  tarGzClearError();
  initFSCallbacks();
  tarFS = &destFS;
  tarDestFolder = destFolder;
  tarTotalSize = 0;

  if (!tgzLogger ) {
    setLoggerCallback( targzPrintLoggerCallback );
  }
  if( !tarProgressCallback ) {
    setTarProgressCallback( tarNullProgressCallback );
  }
  if( !tarMessageCallback ) {
    setTarMessageCallback( targzNullLoggerCallback );
  }
  if( !stream ) {
    log_e("Error: stream is not reachable");
    setError( ESP32_TARGZ_FS_ERROR );
    return false;
  }
  if( !destFS.exists( tarDestFolder ) ) {
    destFS.mkdir( tarDestFolder );
  }

  tgzLogger("[TAR] Expanding stream to folder %s\n", destFolder );

  untarredBytesCount = 0;

  tarCallbacks = {
    tarHeaderCallBack,
    tarStreamReadCallback,
    tarStreamWriteCallback,
    tarEndCallBack
  };

  tarGzIO.tar_size = streamSize;
  tarGzIO.tar = stream;

  TAR::tar_error_logger      = tgzLogger;
  TAR::tar_debug_logger      = tgzLogger; // comment this out if too verbose

  totalFiles = 0;
  totalFolders = 0;

  int res = TAR::read_tar( &tarCallbacks, NULL );
  if( res != TAR_OK ) {
    log_e("[ERROR] operation aborted while expanding stream (return code #%d)", res-30);
    setError( (tarGzErrorCode)(res-30) );
    return false;
  }

  return true;
}


// unpack sourceFS://fileName.tar contents to destFS::/destFolder/
bool TarUnpacker::tarExpander( fs::FS &sourceFS, const char* fileName, fs::FS &destFS, const char* destFolder )
{
  if( !sourceFS.exists( fileName ) ) {
    log_e("Error: file %s does not exist or is not reachable", fileName);
    setError( ESP32_TARGZ_FS_ERROR );
    return false;
  }
  fs::File tarFile = sourceFS.open( fileName, FILE_READ );
  return tarStreamExpander( (Stream*)&tarFile, tarFile.size(), destFS, destFolder );
}





GzUnpacker::GzUnpacker()
{

}


// set progress callback for GZ
void GzUnpacker::setGzProgressCallback( genericProgressCallback cb )
{
  log_v("Assigning GZ progress callback : 0x%8x", (uint)cb );
  gzProgressCallback = cb;
}



// set logger callback
void GzUnpacker::setGzMessageCallback( genericLoggerCallback cb )
{
  log_v("Assigning debug logger callback : 0x%8x", (uint)cb );
  gzMessageCallback = cb;
}



void GzUnpacker::setStreamWriter( gzStreamWriter cb )
{
  gzWriteCallback = cb;
}

void GzUnpacker::setDestByteReader( gzDestByteReader cb )
{
  gzReadDestByte = cb;
}

void GzUnpacker::gzExpanderCleanup()
{
  if( uzlib_gzip_dict != nullptr ) {
    if( tgz_use_psram ) {
      free( uzlib_gzip_dict );
    } else {
      delete( uzlib_gzip_dict );
    }
    uzlib_gzip_dict = NULL;
  }
}


#if defined HAS_OTA_SUPPORT

  // gzWriteCallback
  bool GzUnpacker::gzUpdateWriteCallback( unsigned char* buff, size_t buffsize )
  {
    if( Update.write( buff, buffsize ) ) {
      log_v("Wrote %d bytes", buffsize );
      return true;
    } else {
      log_e("Failed to write %d bytes", buffsize );
    }
    return false;
  }

#endif


// gzWriteCallback
bool GzUnpacker::gzStreamWriteCallback( unsigned char* buff, size_t buffsize )
{
  if( ! tarGzIO.output->write( buff, buffsize ) ) {
    log_w("[GZ WARNING] failed to write %d bytes, will try a second time", buffsize );
    if( ! tarGzIO.output->write( buff, buffsize ) ) {
      log_e("[GZ ERROR] failed to write %d bytes (pos=%d)", buffsize, ((fs::File*)(tarGzIO.output))->position() );
      setError( ESP32_TARGZ_STREAM_ERROR );
      return false;
    }
  } else {
    log_v("Wrote %d bytes", buffsize );
  }
  return true;
}


// gz filesystem helper
uint8_t GzUnpacker::gzReadByte( fs::File &gzFile, const int32_t addr, fs::SeekMode mode )
{
  gzFile.seek( addr, mode );
  return gzFile.read();
}


// 1) check if a file has valid gzip headers
// 2) calculate space needed for decompression
// 2) check if enough space is available on device
bool GzUnpacker::gzReadHeader( fs::File &gzFile )
{
  tarGzIO.output_size = 0;
  tarGzIO.gz_size = gzFile.size();
  bool ret = false;
  if ((gzReadByte(gzFile, 0) == 0x1f) && (gzReadByte(gzFile, 1) == 0x8b)) {
    // GZIP signature matched.  Find real size as encoded at the end
    tarGzIO.output_size =  gzReadByte(gzFile, tarGzIO.gz_size - 4);
    tarGzIO.output_size += gzReadByte(gzFile, tarGzIO.gz_size - 3)<<8;
    tarGzIO.output_size += gzReadByte(gzFile, tarGzIO.gz_size - 2)<<16;
    tarGzIO.output_size += gzReadByte(gzFile, tarGzIO.gz_size - 1)<<24;
    log_v("[GZ INFO] valid gzip file detected! gz size: %d bytes, expanded size:%d bytes", tarGzIO.gz_size, tarGzIO.output_size);
    // Check for free space left on device before writing
    if( fstotalBytes &&  fsfreeBytes ) {
      size_t freeBytes  = fsfreeBytes();
      if( freeBytes < tarGzIO.output_size ) {
        // not enough space on device
        log_e("[GZ ERROR] Target medium will be out of space (required:%d, free:%d), aborting!", tarGzIO.output_size, freeBytes);
        return false;
      } else {
        log_v("[GZ INFO] Available space:%d bytes", freeBytes);
      }
    } else {
      #if defined WARN_LIMITED_FS
        log_w("[GZ WARNING] Can't check target medium for free space (required:%d, free:\?\?), will try to expand anyway\n", tarGzIO.output_size );
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
unsigned int GzUnpacker::gzReadDestByteFS(int offset, unsigned char *out)
{
  unsigned char data;
  //delta between our position in output_buffer, and the desired offset in the output stream
  int delta = output_position + offset;
  if (delta >= 0) {
    //we haven't written output_buffer to persistent storage yet; we need to read from output_buffer
    data = output_buffer[delta];
  } else {
    fs::File *f = (fs::File*)tarGzIO.output;
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
unsigned int GzUnpacker::gzReadSourceByte(CC_UNUSED struct GZ::TINF_DATA *data, unsigned char *out)
{
  _start: // using goto to avoid repeated code blocks
  if (tarGzIO.gz->readBytes( out, 1 ) != 1) {
    uint32_t now = millis();
    uint32_t timeout = now + targz_read_timeout;
    while( !tarGzIO.gz->available() ) {
      if( millis()>timeout ) {
        log_e("gz stream still unresponsive after %dms timeout, giving up", targz_read_timeout);
        return -1;
      }
      vTaskDelay(1); // let the app breathe
    }
    log_d("gz stream was unresponsive during %dms (timeout=%dms)", millis()-now, targz_read_timeout);
    goto _start;
  } else {
    //log_v("read 1 byte: 0x%02x", out[0] );
  }
  if( tarGzIO.gz_size > 0 ) {
    stream_bytesleft -= 1;
  }
  return 0;
}


// gz decompression main routine, handles all logical cases
// isupdate      => zerofill to fit SPI_FLASH_SEC_SIZE
// stream_to_tar => sent bytes to tar instead of filesystem
// use_dict      => change memory usage stragegy
// show_progress => enable/disable bytes count (not always applicable)
int GzUnpacker::gzUncompress( bool isupdate, bool stream_to_tar, bool use_dict, bool show_progress )
{

  log_d("gzUncompress( isupdate = %s, stream_to_tar = %s, use_dict = %s, show_progress = %s)",
    isupdate      ? "true" : "false",
    stream_to_tar ? "true" : "false",
    use_dict      ? "true" : "false",
    show_progress ? "true" : "false"
  );

  if( !tarGzIO.gz->available() ) {
    log_e("[ERROR] in gzUncompress: gz resource doesn't exist!");
    return ESP32_TARGZ_STREAM_ERROR;
  }

  #if defined ESP32
    size_t output_buffer_size = SPI_FLASH_SEC_SIZE; // SPI_FLASH_SEC_SIZE = 4Kb
  #elif defined ESP8266 || defined ARDUINO_ARCH_RP2040
    size_t output_buffer_size = min_output_buffer_size; // must be a multiple of 512 (tar block size)
  #endif

  int uzlib_dict_size = 0;
  int res = 0;

  GZ::uzlib_init();

  if ( use_dict == true && nodict == false ) {

    if( tgz_use_psram ) {
      uzlib_gzip_dict = (unsigned char*)tgz_calloc(1, GZIP_DICT_SIZE);
    } else {
      uzlib_gzip_dict = new unsigned char[GZIP_DICT_SIZE];
    }

    if( uzlib_gzip_dict == NULL ) {
      log_e("[ERROR] can't alloc %d bytes for gzip dict (%d bytes free)", GZIP_DICT_SIZE, HEAP_AVAILABLE() );
      gzExpanderCleanup();
      return ESP32_TARGZ_UZLIB_MALLOC_FAIL; // TODO : Number this error
    }
    uzlib_dict_size = GZIP_DICT_SIZE;
    uzLibDecompressor.readDestByte   = NULL;
    log_i("[INFO] gzUncompress tradeoff: faster, used %d bytes of ram (heap after alloc: %d)", GZIP_DICT_SIZE+output_buffer_size, HEAP_AVAILABLE());
    //log_w("[%d] alloc() done", HEAP_AVAILABLE() );
  } else {
    if( stream_to_tar ) {
      log_e("[ERROR] gz->tar->filesystem streaming requires a gzip dictionnnary");
      return ESP32_TARGZ_NEEDS_DICT;
    } else {
      uzLibDecompressor.readDestByte   = gzReadDestByte ? gzReadDestByte : gzReadDestByteFS;
      log_v("[INFO] gz output is file");
    }
    //output_buffer_size = SPI_FLASH_SEC_SIZE;
    log_i("[INFO] gzUncompress tradeoff: slower will use %d bytes of ram (heap before alloc: %d)", output_buffer_size, HEAP_AVAILABLE());
    uzlib_gzip_dict = NULL;
    uzlib_dict_size = 0;
  }

  uzLibDecompressor.source           = nullptr;
  uzLibDecompressor.readSourceByte   = gzReadSourceByte;
  uzLibDecompressor.destSize         = 1;
  uzLibDecompressor.log              = targzPrintLoggerCallback;
  uzLibDecompressor.readSourceErrors = 0;

  res = GZ::uzlib_gzip_parse_header(&uzLibDecompressor);
  if (res != TINF_OK) {
    log_e("[ERROR] in gzUncompress: uzlib_gzip_parse_header failed (response code %d!", res);
    //if( halt_on_error() ) targz_system_halt();
    gzExpanderCleanup();
    return ESP32_TARGZ_UZLIB_PARSE_HEADER_FAILED;
  }

  GZ::uzlib_uncompress_init(&uzLibDecompressor, uzlib_gzip_dict, uzlib_dict_size);

  output_buffer = (unsigned char *)tgz_calloc( output_buffer_size+1, sizeof(unsigned char) );
  if( output_buffer == NULL ) {
    log_e("[ERROR] can't alloc %d bytes for output buffer", output_buffer_size );
    gzExpanderCleanup();
    return ESP32_TARGZ_UZLIB_MALLOC_FAIL; // TODO : Number this error
  }

  /* decompress a single byte at a time */
  output_position = 0;
  unsigned int outlen = 0;

  if( show_progress ) {
    gzProgressCallback( 0 );
  }

  if( stream_to_tar ) {
    // tar will pull bytes from gz for when needed
    //tinyUntarReadCallback = &tarReadGzStream;
    blockmod = output_buffer_size / TAR_BLOCK_SIZE;
    log_v("[INFO] output_buffer_size=%d blockmod=%d", output_buffer_size, blockmod );
    untarredBytesCount = 0;
    int ret = TAR::tar_setup(&tarCallbacks, NULL);
    firstblock = false;
    if( ret != TAR_OK ) {
      setError( (tarGzErrorCode)(ret-30) );
      return (tarGzErrorCode)(ret-30);
    }
    while( TAR::read_tar_step() == TAR_OK ) yield();
    outlen = untarredBytesCount;

  } else {
    // gz will fill a buffer and trigger a write callback
    do {
      // link to gz internals
      uzLibDecompressor.dest = &output_buffer[output_position];
      res = GZ::uzlib_uncompress_chksum(&uzLibDecompressor);
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
      }

      if( show_progress ) {
        if( tarGzIO.output_size > 0 ) {
          uzlib_bytesleft = tarGzIO.output_size - outlen;
          int32_t progress = 100*(tarGzIO.output_size-uzlib_bytesleft) / tarGzIO.output_size;
          gzProgressCallback( progress );
        } else if( tarGzIO.gz_size > 0 ) {
          //uzlib_bytesleft = tarGzIO.output_size - outlen;
          //stream_bytesleft = tarGzIO.gz_size -
          int32_t progress = 100*(tarGzIO.gz_size-stream_bytesleft) / tarGzIO.gz_size;
          gzProgressCallback( progress );
        }

      }

    } while ( res == TINF_OK );

    if (res != TINF_DONE) {
      if( uzLibDecompressor.readSourceErrors > 0 ) {
        free( output_buffer );
        gzExpanderCleanup();
        return ESP32_TARGZ_STREAM_ERROR;
      }
      log_w("[GZ WARNING] uzlib_uncompress_chksum return code=%d, premature end at position %d while %d bytes left", res, output_position, (int)uzlib_bytesleft);
    }

    if( output_position > 0 ) {
      gzWriteCallback( output_buffer, output_position );
      outlen += output_position;
      output_position = 0;
    }

    if( isupdate && outlen > 0 ) { // Update requirement: written output size must be a multiple of SPI_FLASH_SEC_SIZE
      size_t updatable_size = ( outlen + SPI_FLASH_SEC_SIZE-1 ) & ~( SPI_FLASH_SEC_SIZE-1 );
      size_t zerofill_size  = updatable_size - outlen;
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
bool GzUnpacker::gzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFile )
{
  tarGzClearError();
  initFSCallbacks();
  if (!tgzLogger ) {
    setLoggerCallback( targzPrintLoggerCallback );
  }
  bool isupdate      = false;
  bool stream_to_tar = false;
  bool gz_use_dict   = true;
  bool needs_free    = false;

  if( nodict == true ) {
    gz_use_dict = false;
  } else if( HEAP_AVAILABLE() < GZIP_DICT_SIZE+GZIP_BUFF_SIZE ) {
    size_t free_min_heap_blocks = HEAP_AVAILABLE() / 512; // leave 1k heap, eat all the rest !
    if( free_min_heap_blocks <1 ) {
      setError( ESP32_TARGZ_HEAP_TOO_LOW );
      return false;
    }
    min_output_buffer_size = free_min_heap_blocks * 512;
    if( min_output_buffer_size > GZIP_BUFF_SIZE ) min_output_buffer_size = GZIP_BUFF_SIZE;
    log_w("Disabling GZIP Dictionary (heap wanted:%d, available: %d, buffer: %d bytes), writes will be slow", HEAP_AVAILABLE(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE, min_output_buffer_size );
    gz_use_dict = false;
    //
  } else {
    log_d("Current heap budget (available:%d, needed:%d)", HEAP_AVAILABLE(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
  }

  if( destFile == nullptr ) {
    // when no destination name is provided, it will be speculated
    // assumption: the source filename ends with ".gz"
    String sourceFileCopy = String(sourceFile);
    size_t slen = sourceFileCopy.length();
    if( sourceFileCopy.endsWith(".gz") ) {
      slen -= 3;
      sourceFileCopy = sourceFileCopy.substring( 0, slen );
      destFile = (const char*)tgz_calloc( slen+1, sizeof(char) );
      if( destFile == NULL ) {
        setError( ESP32_TARGZ_HEAP_TOO_LOW );
        return false;
      }
      needs_free = true;
      snprintf( (char*)destFile, slen+1, "%s", sourceFileCopy.c_str() );
      log_v("Speculated filename: %s", destFile );
    } else {
      setError( ESP32_TARGZ_TAR_ERR_FILENAME_NOT_GZ );
      return false;
    }
  }

  tgzLogger("[GZ] Expanding %s to %s\n", sourceFile, destFile );

  fs::File gz = sourceFS.open( sourceFile, FILE_READ );
  if( !gzProgressCallback ) {
    setGzProgressCallback( defaultProgressCallback );
  }
  if( !gzReadHeader( gz ) ) {
    log_e("[GZ ERROR] in gzExpander: invalid gzip file or not enough space left on device ?");
    gz.close();
    if( needs_free ) free( (char*)destFile );
    setError( ESP32_TARGZ_UZLIB_INVALID_FILE );
    return false;
  }

  if( destFS.exists( destFile ) ) {
    log_v("[GZ INFO] Deleting %s as it is in the way", destFile);
    destFS.remove( destFile );
  }
  fs::File outfile = destFS.open( destFile, "w+" );
  if(!outfile) {
    log_e("[GZ ERROR] in gzExpander: cannot create destination file, no space left on device ?");
    gz.close();
    if( needs_free ) free( (char*)destFile );
    setError( ESP32_TARGZ_UZLIB_INVALID_FILE );
    return false;
  }

  tarGzIO.gz = &gz;
  tarGzIO.output = &outfile;
  if( gzWriteCallback == nullptr ) {
    setStreamWriter( gzStreamWriteCallback );
  }
  //gzWriteCallback = &gzStreamWriteCallback; // for regular unzipping

  int ret = gzUncompress( isupdate, stream_to_tar, gz_use_dict );

  outfile.close();
  gz.close();

  if( ret!=0 ) {
    log_e("gzUncompress returned error code %d", ret);
    if( needs_free ) free( (char*)destFile );
    setError( (tarGzErrorCode)ret );
    return false;
  }
  log_v("uzLib expander finished!");

  /*
  outfile = destFS.open( destFile, FILE_READ );
  log_d("Expanded %s to %s (%d bytes)", sourceFile, destFile, outfile.size() );
  outfile.close();
  */
  if( gzMessageCallback ) {
    gzMessageCallback("%s", destFile );
  }

  if( needs_free ) free( (char*)destFile );

  if( fstotalBytes &&  fsfreeBytes ) {
    log_d("[GZ Info] FreeBytes after expansion=%d", fsfreeBytes() );
  }
  return true;
}



// uncompress gz stream (file or HTTP) to any destination (see setStreamWriter)
bool GzUnpacker::gzStreamExpander( Stream *stream, size_t gz_size )
{
  if( !gzProgressCallback ) {
    setGzProgressCallback( defaultProgressCallback );
  }
  if( !tgzLogger ) {
    setLoggerCallback( targzPrintLoggerCallback );
  }

  size_t size = stream->available();
  if( ! size ) {
    log_e("Bad stream, aborting");
    setError( ESP32_TARGZ_STREAM_ERROR );
    return false;
  }
  // TODO: ESP8266 support
  #if defined ESP8266 || defined ARDUINO_ARCH_RP2040
    log_e("gz stream expanding not implemented on ESP8266/RP2040");
    return false;
  #elif defined ESP32

    bool show_progress = false;
    bool use_dict      = true;
    bool isupdate      = false;
    bool stream_to_tar = false;

    if( HEAP_AVAILABLE() < GZIP_DICT_SIZE+GZIP_BUFF_SIZE ) {
      // log_w("Disabling gzip dictionnary (havailable:%d, needed:%d)", HEAP_AVAILABLE(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
      log_w("Insufficient heap to decompress (available:%d, needed:%d), aborting", HEAP_AVAILABLE(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
      setError( ESP32_TARGZ_HEAP_TOO_LOW );
      return false;
    }

    tarGzIO.gz = stream;
    if( gzWriteCallback == nullptr ) {
      setStreamWriter( gzStreamWriteCallback );
    }

    if( int( gz_size ) < 1 || gz_size == 0 ) {
      tgzLogger("[GZStreamExpander] unknown binary size\n");
      stream_bytesleft = 0;
    } else {
      tgzLogger("[GZStreamExpander] Unzipping\n");
      stream_bytesleft = gz_size;
    }
    // process with unzipping
    int ret = gzUncompress( isupdate, stream_to_tar, use_dict, show_progress );
    // unzipping ended
    if( ret!=0 ) {
      log_e("gzUncompress returned error code %d", ret);
      setError( (tarGzErrorCode)ret );
      return false;
    }
    setError( (tarGzErrorCode)ret );
  #endif // ifdef ESP32

  return true;
}




#if defined ESP32 || defined ESP8266

  // uncompress gz file to flash (expected to be a valid gzipped firmware)
  bool GzUnpacker::gzUpdater( fs::FS &fs, const char* gz_filename, int partition, bool restart_on_update )
  {
    tarGzClearError();
    initFSCallbacks();
    if (!tgzLogger ) {
      setLoggerCallback( targzPrintLoggerCallback );
    }
    // ESP8266 does not need such check as the unpacker is in the bootloader

    if( !fs.exists( gz_filename )  ) {
      log_e("[ERROR] in gzUpdater: %s does not exist", gz_filename);
      setError( ESP32_TARGZ_UZLIB_INVALID_FILE );
      return false;
    }
    log_v("uzLib SPIFFS Updater start!");
    fs::File gz = fs.open( gz_filename, FILE_READ );
    #if defined ESP8266
      int update_size = gz.size();
    #endif
    #if defined ESP32
      int update_size = UPDATE_SIZE_UNKNOWN;
    #endif
    return gzStreamUpdater( (Stream*)&gz, update_size, partition, restart_on_update );
  }




  // uncompress gz stream (file or HTTP) to flash (expected to be a valid Arduino compiled binary sketch)
  bool GzUnpacker::gzStreamUpdater( Stream *stream, size_t update_size, int partition, bool restart_on_update )
  {
    if( !gzProgressCallback ) {
      setGzProgressCallback( defaultProgressCallback );
    }
    if( !tgzLogger ) {
      setLoggerCallback( targzPrintLoggerCallback );
    }

    size_t size = stream->available();
    if( ! size ) {
      log_e("Bad stream, aborting");
      setError( ESP32_TARGZ_STREAM_ERROR );
      return false;
    }

    #ifdef ESP8266
      // ESP8266 has built-in uzlib so no need to uncompress, just update with gzData
      bool use_buffered_writes = false; // use buffered writes when the gz size is unknown, otherwise use stream writes
      size_t stream_size = 0;

      if( int( update_size ) < 1 ) {
        use_buffered_writes = true;
        stream_size = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        log_d("Stream size is unknown, aligning update to the partition available size: %d ", stream_size );
      } else {
        Update.runAsync( true );
        stream_size = update_size;
        log_v("Stream size is %d", stream_size );
      }

      if ( !Update.begin( stream_size, partition )) { // U_FLASH or U_PART
        log_e("Can't begin update");
        setError( (tarGzErrorCode)(Update.getError()-20) ); // "-20" offset is Update error id to esp32-targz error id
        return false;
      }

      if( !use_buffered_writes ) {
        // stream method
        static bool finished = false;
        // async progress
        Update.onProgress([]( size_t done, size_t total ) {
          size_t progress = (100*done)/total;
          if(! finished ) gzProgressCallback( progress );
          if( progress == 100 ) finished = true;
        });
        // walk stream
        while( stream->available() ) {
          if(! Update.writeStream( *stream ) ) {
            log_e("Update couldn't read stream");
            setError( (tarGzErrorCode)(Update.getError()-20) ); // "-20" offset is Update error id to esp32-targz error id
            return false;
          }
          if( finished ) break;
          yield();
        }
      } else {
        // buffered writes method, uses 4KB ram
        // TODO: make this adjustable
        size_t buffsize = 4096;
        uint8_t *buffer = new uint8_t[4096];
        if( !buffer) {
          log_e("Could not allocate %d bytes for stream read", buffsize );
          setError( ESP32_TARGZ_HEAP_TOO_LOW );
          return false;
        }
        uint8_t progress = 0;
        gzProgressCallback( progress );

        while( stream->available() ) {
          size_t len = stream->readBytes( buffer, buffsize );
          if( len < 1 ) break; // end of stream
          if (Update.write(buffer, len) != len) {
            log_e("Updater could not write %d bytes", len );
            setError( (tarGzErrorCode)(Update.getError()-20) ); // "-20" offset is Update error id to esp32-targz error id
            return false;
          } else {
            progress = (Update.progress()*100)/Update.size();
            gzProgressCallback( progress );
          }
          yield();
        }
        if( progress != 100 ) {
          gzProgressCallback( 100 );
        }
        delete buffer;
      }

      if ( !Update.end( true ) ) {
        Update.printError(Serial);
        log_e( "Update Error Occurred. Error #: %u", Update.getError() );
        setError( (tarGzErrorCode)(Update.getError()-20) ); // "-20" offset is Update error id to esp32-targz error id
        return false;
      }

      if ( !Update.isFinished() ) {
        log_e("Update incomplete");
        setError( ESP32_TARGZ_UPDATE_INCOMPLETE );
        return false;
      }

      log_v("Update finished !");
      if( restart_on_update ) ESP.restart();
    #endif // ifdef ESP8266


    #if defined ESP32
      // unfortunately ESP32 doesn't handle gzipped firmware from the bootloader
      bool show_progress = false;
      bool use_dict      = true;
      bool isupdate      = true;
      bool stream_to_tar = false;

      if( HEAP_AVAILABLE() < GZIP_DICT_SIZE+GZIP_BUFF_SIZE ) {
        // log_w("Disabling gzip dictionnary (havailable:%d, needed:%d)", HEAP_AVAILABLE(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
        log_w("Insufficient heap to decompress (available:%d, needed:%d), aborting", HEAP_AVAILABLE(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
        setError( ESP32_TARGZ_HEAP_TOO_LOW );
        return false;
      }

      tarGzIO.gz = stream;
      //if( gzWriteCallback == nullptr ) {
        setStreamWriter( gzUpdateWriteCallback );
      //}

      Update.onProgress([]( size_t done, size_t total ) {
        gzProgressCallback( (100*done)/total );
      });

      if( int( update_size ) < 1 || update_size == UPDATE_SIZE_UNKNOWN ) {
        tgzLogger("[GZUpdater] Starting update with unknown binary size\n");
        if( !Update.begin( UPDATE_SIZE_UNKNOWN, partition ) ) {
          setError( (tarGzErrorCode)(Update.getError()-20) ); // "-20" offset is Update error id to esp32-targz error id
          return false;
        }
      } else {
        tgzLogger("[GZUpdater] Starting update\n");
        if( !Update.begin( ( ( update_size + SPI_FLASH_SEC_SIZE-1 ) & ~( SPI_FLASH_SEC_SIZE-1 ) ), partition ) ) {
          setError( (tarGzErrorCode)(Update.getError()-20) ); // "-20" offset is Update error id to esp32-targz error id
          return false;
        }
      }
      // process with unzipping
      int ret = gzUncompress( isupdate, stream_to_tar, use_dict, show_progress );

      // unzipping ended
      if( ret!=0 ) {
        log_e("gzHTTPUpdater returned error code %d", ret);
        setError( (tarGzErrorCode)ret );
        return false;
      }

      if ( Update.end( true ) ) {
        log_v( "OTA done!" );
        if ( Update.isFinished() ) {
          // yay
          log_v("Update finished !");
          gzProgressCallback( 100 );
          if( restart_on_update ) ESP.restart();
        } else {
          log_e( "Update not finished? Something went wrong!" );
          setError( ESP32_TARGZ_UPDATE_INCOMPLETE );
          return false;
        }
      } else {
        log_e( "Update Error Occurred. Error #: %u", Update.getError() );
        setError( (tarGzErrorCode)(Update.getError()-20) ); // "-20" offset is Update error id to esp32-targz error id
        return false;
      }
      log_v("uzLib filesystem Updater finished!");
      setError( (tarGzErrorCode)ret );
    #endif // ifdef ESP32

    return true;
  }


#endif // defined HAS_OTA_SUPPORT









TarGzUnpacker::TarGzUnpacker() : TarUnpacker(), GzUnpacker()
{

}


// gzWriteCallback
bool TarGzUnpacker::gzProcessTarBuffer( CC_UNUSED unsigned char* buff, CC_UNUSED size_t buffsize )
{
  //stream_bytesleft -= buffsize;

  if( lastblock ) {
    return true;
  }

  if( firstblock ) {
    if( TAR::tar_setup(&tarCallbacks, NULL) == TAR_OK ) {
      firstblock = false;
    } else {
      return false;
    }
  }
  gzTarBlockPos = 0;
  while( gzTarBlockPos < blockmod ) {
    int response = TAR::read_tar_step(); // warn: this may fire more than 1 read_cb()
    if( response == TAR_EXPANDING_DONE ) {
      log_v("[TAR] Expanding done !");
      lastblock = true;
      return true;
    }
    if( gzTarBlockPos > blockmod ) {
      log_e("[ERROR] read_tar_step() fired more too many read_cb()");
      setError( ESP32_TARGZ_TAR_ERR_GZREAD_FAIL );
      return false;
    }
    if( response < 0 ) {
      log_e("[WARN] gzProcessTarBuffer failed reading %d bytes (buffsize=%d) in gzip block #%d/%d, got response %d", TAR_BLOCK_SIZE, buffsize, gzTarBlockPos%blockmod, blockmod, response);
      setError( ESP32_TARGZ_TAR_ERR_GZREAD_FAIL );
      return false;
    }
  }
  log_v("gz buffer processed by tar (%d steps)", gzTarBlockPos);
  return true;
}


// tinyUntarReadCallback
int TarGzUnpacker::tarReadGzStream( unsigned char* buff, size_t buffsize )
{
  if( buffsize%TAR_BLOCK_SIZE !=0 ) {
    log_e("[ERROR] tarReadGzStream Can't unmerge tar blocks (%d bytes) from gz block (%d bytes)\n", buffsize, GZIP_BUFF_SIZE);
    setError( ESP32_TARGZ_TAR_ERR_GZDEFL_FAIL );
    return 0;
  }
  size_t i;
  for( i=0; i<buffsize; i++) {
    uzLibDecompressor.dest = &buff[i];
    int res = GZ::uzlib_uncompress_chksum(&uzLibDecompressor);
    if (res != TINF_OK) {
      // uncompress done or aborted, no need to go further
      break;
    }
  }
  tarReadGzStreamBytes += i;

  uzlib_bytesleft = tarGzIO.output_size - tarReadGzStreamBytes;
  // stream_bytesleft -= buffsize;
  if( tarGzIO.output_size > 0 ) {
    int32_t progress = 100*(tarGzIO.output_size-uzlib_bytesleft) / tarGzIO.output_size;
    gzProgressCallback( progress );
  }
  // else if( tarGzIO.gz_size>0 ) {
    //int32_t progress = 100*(tarGzIO.gz_size-stream_bytesleft) / tarGzIO.gz_size;
    //gzProgressCallback( progress );
  //}
  return i;
}


// tinyUntarReadCallback
int TarGzUnpacker::gzFeedTarBuffer( unsigned char* buff, size_t buffsize )
{
  static size_t bytes_fed = 0;
  if( buffsize%TAR_BLOCK_SIZE !=0 ) {
    log_e("[ERROR] gzFeedTarBuffer Can't unmerge tar blocks (%d bytes) from gz block (%d bytes)\n", buffsize, GZIP_BUFF_SIZE);
    setError( ESP32_TARGZ_TAR_ERR_GZDEFL_FAIL );
    return 0;
  }
  //stream_bytesleft -= buffsize;
  uint32_t blockpos = gzTarBlockPos%blockmod;
  memcpy( buff, output_buffer/*uzlib_buffer*/+(TAR_BLOCK_SIZE*blockpos), TAR_BLOCK_SIZE );
  bytes_fed += TAR_BLOCK_SIZE;
  log_v("[TGZ INFO][tarbuf<-gzbuf] block #%d (%d mod %d) at output_buffer[%d] (%d bytes, total %d)", blockpos, gzTarBlockPos, blockmod, (TAR_BLOCK_SIZE*blockpos), buffsize, bytes_fed );
  gzTarBlockPos++;
  return TAR_BLOCK_SIZE;
}



/*
bool TarGzUnpacker::tarGzExpanderNoTempFile( Stream* stream, fs::FS destFS, const char* destFolder )
{
}
*/

// uncompress gz sourceFile directly to untar, no intermediate file
bool TarGzUnpacker::tarGzExpanderNoTempFile( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFolder )
{
  tarGzClearError();
  initFSCallbacks();
  if (!tgzLogger ) {
    setLoggerCallback( targzPrintLoggerCallback );
  }
  if( !gzProgressCallback ) {
    setGzProgressCallback( defaultProgressCallback );
  }
  if( !tarProgressCallback ) {
    setTarProgressCallback( tarNullProgressCallback );
  }
  if( !tarMessageCallback ) {
    setTarMessageCallback( targzNullLoggerCallback );
  }
  if( gzProgressCallback && gzProgressCallback == tarProgressCallback ) {
    log_v("Disabling colliding gzProgressCallback");
    setGzProgressCallback( targzNullProgressCallback );
  }

  if( nodict == true ) {
    log_e("Function explicitely disabled by ::noDict(), aborting");
    setError( ESP32_TARGZ_HEAP_TOO_LOW );
    return false;
  } else if( HEAP_AVAILABLE() < GZIP_DICT_SIZE+GZIP_BUFF_SIZE ) {
    log_e("Insufficient heap to decompress (available:%d, needed:%d), aborting", HEAP_AVAILABLE(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
    setError( ESP32_TARGZ_HEAP_TOO_LOW );
    return false;
  } else {
    log_d("Current heap budget (available:%d, needed:%d)", HEAP_AVAILABLE(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE );
  }
  if( !sourceFS.exists( sourceFile ) ) {
    log_e("gzip file %s does not exist", sourceFile);
    setError( ESP32_TARGZ_UZLIB_INVALID_FILE );
    return false;
  }
  fs::File gz = sourceFS.open( sourceFile, FILE_READ );

  tgzLogger("[TGZ] Will direct-expand %s to %s\n", sourceFile, destFolder );

  if( !gzReadHeader( gz ) ) {
    log_e("[GZ ERROR] in tarGzExpanderNoTempFile: invalid gzip file or not enough space left on device ?");
    gz.close();
    setError( ESP32_TARGZ_UZLIB_INVALID_FILE );
    return false;
  }

  tarGzIO.gz = &gz;
  tarFS = &destFS;
  tarDestFolder = destFolder;

  if( !destFS.exists( tarDestFolder ) ) {
    destFS.mkdir( tarDestFolder );
  }

  untarredBytesCount = 0;
  gzTarBlockPos = 0;

  tarCallbacks = {
    tarHeaderCallBack,
    tarReadGzStream,
    tarStreamWriteCallback,
    tarEndCallBack
  };

  TAR::tar_error_logger = tgzLogger;
  TAR::tar_debug_logger = tgzLogger; // comment this out if too verbose

  if( gzWriteCallback == nullptr ) {
    setStreamWriter( gzProcessTarBuffer );
  }
  //gzWriteCallback       = &gzProcessTarBuffer;
  tarReadGzStreamBytes = 0;

  totalFiles = 0;
  totalFolders = 0;

  firstblock = true; // trigger TAR setup from gzUncompress callback
  lastblock  = false;

  bool isupdate      = false;
  bool stream_to_tar = true;

  int ret = gzUncompress( isupdate, stream_to_tar );

  gz.close();

  if( ret!=0 ) {
    log_e("gzUncompress returned error code %d", ret);
    setError( (tarGzErrorCode)ret );
    return false;
  }
  log_v("uzLib expander finished!");

  if( fstotalBytes &&  fsfreeBytes ) {
    log_d("[GZ Info] FreeBytes after expansion=%d", fsfreeBytes() );
  }

  return true;
}


// unzip sourceFS://sourceFile.tar.gz contents into destFS://destFolder
bool TarGzUnpacker::tarGzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFolder, const char* tempFile )
{
  tarGzClearError();
  initFSCallbacks();

  if( tempFile != nullptr ) {

    tgzLogger("[TGZ] Will expand using an intermediate file: %s\n", tempFile );

    mkdirp( &destFS, tempFile );

    if( gzExpander(sourceFS, sourceFile, destFS, tempFile) ) {
      log_d("[INFO] heap before tar-expanding: %d)", HEAP_AVAILABLE());
      if( tarExpander(destFS, tempFile, destFS, destFolder) ) {
        // yay
      }
    }
    if( destFS.exists( tempFile ) ) destFS.remove( tempFile );

    return !tarGzHasError();
  } else {

    tgzLogger("[TGZ] Will expand without intermediate file\n" );

    if( gzProgressCallback && gzProgressCallback == tarProgressCallback ) {
      log_v("Disabling gzprogress callback for this instance");
      setGzProgressCallback( targzNullProgressCallback );
    }

    return tarGzExpanderNoTempFile( sourceFS, sourceFile, destFS, destFolder );
  }
}


#if defined HAS_OTA_SUPPORT

  bool TarGzUnpacker::tarGzStreamUpdater( Stream *stream )
  {
    if( !stream->available() ) {
      log_e("Bad stream, aborting");
      setError( ESP32_TARGZ_STREAM_ERROR );
      return false;
    }
    if( !gzProgressCallback ) {
      setGzProgressCallback( defaultProgressCallback );
    }
    tarGzIO.gz = stream;
    tarFS = nullptr;
    untarredBytesCount = 0;
    gzTarBlockPos = 0;

    tarCallbacks = {
      tarHeaderUpdateCallBack,
      gzFeedTarBuffer,
      tarStreamWriteUpdateCallback,
      tarEndUpdateCallBack
    };

    TAR::tar_error_logger      = tgzLogger; // targzPrintLoggerCallback or tgzLogger
    TAR::tar_debug_logger      = tgzLogger; // comment this out if too verbose

    if( gzWriteCallback == nullptr ) {
      setStreamWriter( gzProcessTarBuffer );
    }
    //gzWriteCallback = &gzProcessTarBuffer;

    totalFiles = 0;
    totalFolders = 0;

    firstblock = true; // trigger TAR setup from gzUncompress callback
    lastblock  = false;

    bool isupdate      = true;
    bool stream_to_tar = false;
    bool use_dict      = true;
    bool show_progress = false;

    int ret = gzUncompress( isupdate, stream_to_tar, use_dict, show_progress );

    if( ret!=0 ) {
      log_e("gzUncompress returned error code %d", ret);
      setError( (tarGzErrorCode)ret );
      return false;
    }

    return true;

  }

#endif // defined HAS_OTA_SUPPORT


// uncompress tar+gz stream (file or HTTP) to filesystem without intermediate tar file
bool TarGzUnpacker::tarGzStreamExpander( Stream *stream, fs::FS &destFS, const char* destFolder, int64_t streamSize )
{
  if( nodict == true ) { // leave 1k heap for the stack
    log_e("[GZ] Function explicely disabled by ::noDict()" );
    setError( ESP32_TARGZ_HEAP_TOO_LOW );
    return false;
  }

  bool isupdate      = false;
  bool stream_to_tar = false;
  bool use_dict      = true; // mandatory for stream to stream, no temp file = checksum+seek unavailable !!
  bool show_progress = false;

  // size was provided when passing the stream, enable progress
  if( streamSize > 0 ) {
    tarGzIO.gz_size = streamSize;
    stream_bytesleft = streamSize;
    show_progress = true;
    log_w("Enabling progress");
  }

  if( !stream->available() ) {
    log_e("Bad stream, aborting");
    setError( ESP32_TARGZ_STREAM_ERROR );
    return false;
  }

  if( !gzProgressCallback ) {
    show_progress = false;
    setGzProgressCallback( defaultProgressCallback );
  }

  tarGzIO.gz = stream;

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
    tarHeaderCallBack,
    gzFeedTarBuffer,
    tarStreamWriteCallback,
    tarEndCallBack
  };

  TAR::tar_error_logger      = tgzLogger; // targzPrintLoggerCallback or tgzLogger
  TAR::tar_debug_logger      = tgzLogger; // comment this out if too verbose

  //if( gzWriteCallback == nullptr ) {
    setStreamWriter( gzProcessTarBuffer );
  //}
  //gzWriteCallback = &gzProcessTarBuffer;

  totalFiles = 0;
  totalFolders = 0;

  firstblock = true; // trigger TAR setup from gzUncompress callback
  lastblock  = false;



  #if defined ESP8266 || defined ARDUINO_ARCH_RP2040

    min_output_buffer_size = 1024;

    int dict_available_heap = (HEAP_AVAILABLE()-(GZIP_DICT_SIZE+GZIP_BUFF_SIZE+min_output_buffer_size));

    // check minimal ram for gzip+tar
    if( dict_available_heap < 1024 ) { // leave 1k heap for the stack
      log_e("[GZ] not enough heap, available: %d, needed: %d :-(", HEAP_AVAILABLE(), GZIP_DICT_SIZE+GZIP_BUFF_SIZE+min_output_buffer_size );
      setError( ESP32_TARGZ_HEAP_TOO_LOW );
      return false;
    }

    blockmod = min_output_buffer_size / TAR_BLOCK_SIZE; // adjust gz->tar buffered block modulo
    tgzLogger("tarGzStreamExpander will unpack stream to %s folder using %d buffered bytes and %s dictionary\n", destFolder, min_output_buffer_size, use_dict ? "36Kb for the" : "NO" );

  #endif

  int ret = gzUncompress( isupdate, stream_to_tar, use_dict, show_progress );

  if( ret!=0 ) {
    log_e("tarGzStreamExpander returned error code %d", ret);
    setError( (tarGzErrorCode)ret );
    return false;
  }

  return true;
}




#if defined ESP32 && defined HAS_OTA_SUPPORT

  /**    GzUpdateClass Class implementation    **/

  bool GzUpdateClass::begingz(size_t size, int command, int ledPin, uint8_t ledOn, const char *label)
  {
    if( !gzProgressCallback ) {
      log_d("Setting progress cb");
      gzUnpacker.setGzProgressCallback( gzUnpacker.defaultProgressCallback );
    }
    if( !tgzLogger ) {
      log_d("Setting logger cb");
      gzUnpacker.setLoggerCallback( gzUnpacker.targzPrintLoggerCallback );
    }
    if( gzWriteCallback == nullptr ) {
      log_d("Setting write cb");
      gzUnpacker.setStreamWriter( this->gzUpdateWriteCallback );
    }

    mode_gz = true;

    bool ret = begin(size, command, ledPin, ledOn, label);

    return ret;
  }



  bool GzUpdateClass::gzUpdateWriteCallback( unsigned char* buff, size_t buffsize )
  {
    int written = GzUpdateClass::getInstance().write( buff, buffsize );
    if( written ) {
      log_v("Wrote %d bytes", written );
      return true;
    }
    log_e("Failed to write %d bytes", buffsize );
    return false;
  }


  void GzUpdateClass::abortgz()
  {
    abort();
    gzUnpacker.gzExpanderCleanup();
    mode_gz = false;
  }


  bool GzUpdateClass::endgz(bool evenIfRemaining)
  {
    gzUnpacker.gzExpanderCleanup();
    mode_gz = false;
    return end(evenIfRemaining);
  }


  size_t GzUpdateClass::writeGzStream(Stream &data, size_t len)
  {
    if (!mode_gz) {
      log_d("Not in gz mode");
      return writeStream(data);
    }

    uint32_t timeout = millis() + targz_read_timeout;

    while( !data.available() ) {
      if(millis()>timeout) {
        log_e("stream still not responsive after %dms timeout, giving up", targz_read_timeout);
        return 0;
      }
      vTaskDelay(1);
    }

    log_d("In gz mode");

    tarGzIO.gz = &data;

    // process with decompressing
    int ret = gzUnpacker.gzUncompress( true/*isupdate*/, false/*stream_to_tar*/, true/*use_dict*/, false/*show_progress*/ );

    if( ret!=0 ) {
      log_e("gzUncompress returned error code %d (free heap=%d bytes)", ret, HEAP_AVAILABLE() );
      //gzUnpacker.setError( (tarGzErrorCode)ret );
      return 0;
    }

    //log_d("unpack complete (%d bytes)", use_dict ? tarGzIO.gz_size : GzUpdateClass_Write_Offset );
    // TODO: return actual uncompressed length

    return len;
  }

#endif



#ifdef ESP8266
#pragma GCC diagnostic pop
#endif
