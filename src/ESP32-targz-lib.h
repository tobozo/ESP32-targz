/*\

  MIT License

  Copyright (c) 2020 tobozo

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

#ifndef _ESP_TGZ_H
#define _ESP_TGZ_H

#include <FS.h>


#if defined( ESP32 )
  #include <Update.h>
#elif defined( ESP8266 )
  //#ifdef USE_LittleFS
  //  #define SPIFFS LittleFS
  //  #include <LittleFS.h>
  //#endif
  #include <Updater.h>
#else
  #error Unsupported architecture
#endif

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

// unzip sourceFS://sourceFile.tar.gz contents into destFS://destFolder
bool    tarGzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFolder="/tmp", const char* tempFile = "/tmp/data.tar" );
#if defined ESP32
  // unpack stream://fileName.tar.gz contents to destFS::/destFolder/
  bool    tarGzStreamExpander( Stream *stream, fs::FS &destFs, const char* destFolder = "/" );
#endif
// unpack sourceFS://fileName.tar contents to destFS::/destFolder/
bool    tarExpander( fs::FS &sourceFS, const char* fileName, fs::FS &destFS, const char* destFolder );
//TODO: tarStreamExpander( Stream* sourceStream, fs::FS &destFS, const char* destFolder );
// uncompresses *gzipped* sourceFile to destFile, filesystems may differ
bool    gzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFile );
//TODO: gzStreamExpander( Stream* sourceStream, fs::FS destFS, const char* destFile );
// flashes the ESP with the content of a *gzipped* file
bool    gzUpdater( fs::FS &sourceFS, const char* gz_filename );
#if defined ESP32
  // flashes the ESP with the contents of a gzip stream (file or http), no progress callbacks
  bool    gzStreamUpdater( Stream *stream, size_t uncompressed_size = 0 );
#endif
// error/warning/info null logger, use with setLoggerCallback() to silent output
void    targzNullLoggerCallback( const char* format, ... );
// null progress callback, use with setProgressCallback or setTarProgressCallback to silent output
void    targzNullProgressCallback( uint8_t progress );

// error/warning/info logger, use with setLoggerCallback() to enable output
void    targzPrintLoggerCallback(const char* format, ...);
// print progress callback, use with setProgressCallback or setTarProgressCallback to enable progress output
void    defaultProgressCallback( uint8_t progress );

// print tar status since a progress can't be provided
void    defaultTarStatusProgressCallback( const char* name, size_t size, size_t total_unpacked );

// naive ls
void    tarGzListDir( fs::FS &fs, const char * dirName, uint8_t levels=1, bool hexDump = false );

// paranoid mode
void tarGzHaltOnError( bool halt );

// useful to share the buffer so it's not totally wasted memory outside targz scope
uint8_t *gzGetBufferUint8();
// file-based hexViewer for debug
void    hexDumpFile( fs::FS &fs, const char* filename, uint32_t output_size = 32 );
void    hexDumpData( const char* buff, size_t buffsize, uint32_t output_size = 32 );

// Callbacks for getting free/total space left on *destination* device.
// Optional but recommended to prevent SPIFFS/LittleFS/FFat partitions
// to explode during stream writes.
typedef size_t (*fsTotalBytesCb)();
typedef size_t (*fsFreeBytesCb)();
void    setFsTotalBytesCb( fsTotalBytesCb cb );
void    setFsFreeBytesCb( fsFreeBytesCb cb );

// setup filesystem helpers (totalBytes, freeBytes)
// must be done from outside the library since FS is an abstraction of an abstraction :(
typedef void (*fsSetupCallbacks)( fsTotalBytesCb cbt, fsFreeBytesCb cbf );
void    setupFSCallbacks(  fsTotalBytesCb cbt, fsFreeBytesCb cbf );

// tar doesn't have a real progress, so provide a status instead
typedef void (*tarStatusProgressCb)( const char* name, size_t size, size_t total_unpacked );
void    setTarStatusProgressCallback( tarStatusProgressCb cb );

// Callbacks for progress and misc output messages, default is verbose
typedef void (*genericProgressCallback)( uint8_t progress );
typedef void (*genericLoggerCallback)( const char* format, ... );
void    setProgressCallback( genericProgressCallback cb ); // for gzip
void    setTarProgressCallback( genericProgressCallback cb ); // for tar
void    setTarMessageCallback( genericLoggerCallback cb ); // for tar
void    setLoggerCallback( genericLoggerCallback cb );
void    setTarVerify( bool verify ); // enables health checks but does slower writes


// Error handling
__attribute__((unused))
static void (*tgzLogger)( const char* format, ...);
int8_t  tarGzGetError();
void    tarGzClearError();
bool    tarGzHasError();

// This is only to centralize error codes and spare the
// hassle of looking up in three different library folders

typedef enum tarGzErrorCode /* int8_t */
{

  ESP32_TARGZ_OK                         =   0,   // yay
  // general library errors
  ESP32_TARGZ_FS_ERROR                   =  -1,   // Filesystem error
  ESP32_TARGZ_STREAM_ERROR               =  -6,   // same a Filesystem error
  ESP32_TARGZ_UPDATE_INCOMPLETE          =  -7,   // Update not finished? Something went wrong
  ESP32_TARGZ_TAR_ERR_GZDEFL_FAIL        =  -38,  // Library error during deflating
  ESP32_TARGZ_TAR_ERR_GZREAD_FAIL        =  -39,  // Library error during gzip read
  ESP32_TARGZ_TAR_ERR_FILENAME_TOOLONG   =  -40,  // Library error during file creation
  ESP32_TARGZ_FS_FULL_ERROR              =  -100, // no space left on device
  ESP32_TARGZ_FS_WRITE_ERROR             =  -101, // no space left on device
  ESP32_TARGZ_FS_READSIZE_ERROR          =  -102, // no space left on device
  ESP32_TARGZ_HEAP_TOO_LOW               =  -103, // not enough heap
  ESP32_TARGZ_NEEDS_DICT                 =  -104, // gzip dictionnary needs to be enabled

  // UZLIB: keeping error values from uzlib.h as is (no offset)
  ESP32_TARGZ_UZLIB_INVALID_FILE         =  -2,   // Not a valid gzip file
  ESP32_TARGZ_UZLIB_DATA_ERROR           =  -3,   // Gz Error TINF_DATA_ERROR
  ESP32_TARGZ_UZLIB_CHKSUM_ERROR         =  -4,   // Gz Error TINF_CHKSUM_ERROR
  ESP32_TARGZ_UZLIB_DICT_ERROR           =  -5,   // Gz Error TINF_DICT_ERROR

  // UPDATE: adding -20 offset to actual error values from Update.h
  ESP32_TARGZ_UPDATE_ERROR_ABORT         =  -8,   // Updater Error UPDATE_ERROR_ABORT        -20   // (12-20) = -8
  ESP32_TARGZ_UPDATE_ERROR_BAD_ARGUMENT  =  -9,   // Updater Error UPDATE_ERROR_BAD_ARGUMENT -20   // (11-20) = -9
  ESP32_TARGZ_UPDATE_ERROR_NO_PARTITION  =  -10,  // Updater Error UPDATE_ERROR_NO_PARTITION -20   // (10-20) = -10
  ESP32_TARGZ_UPDATE_ERROR_ACTIVATE      =  -11,  // Updater Error UPDATE_ERROR_ACTIVATE     -20   // (9-20)  = -11
  ESP32_TARGZ_UPDATE_ERROR_MAGIC_BYTE    =  -12,  // Updater Error UPDATE_ERROR_MAGIC_BYTE   -20   // (8-20)  = -12
  ESP32_TARGZ_UPDATE_ERROR_MD5           =  -13,  // Updater Error UPDATE_ERROR_MD5          -20   // (7-20)  = -13
  ESP32_TARGZ_UPDATE_ERROR_STREAM        =  -14,  // Updater Error UPDATE_ERROR_STREAM       -20   // (6-20)  = -14
  ESP32_TARGZ_UPDATE_ERROR_SIZE          =  -15,  // Updater Error UPDATE_ERROR_SIZE         -20   // (5-20)  = -15
  ESP32_TARGZ_UPDATE_ERROR_SPACE         =  -16,  // Updater Error UPDATE_ERROR_SPACE        -20   // (4-20)  = -16
  ESP32_TARGZ_UPDATE_ERROR_READ          =  -17,  // Updater Error UPDATE_ERROR_READ         -20   // (3-20)  = -17
  ESP32_TARGZ_UPDATE_ERROR_ERASE         =  -18,  // Updater Error UPDATE_ERROR_ERASE        -20   // (2-20)  = -18
  ESP32_TARGZ_UPDATE_ERROR_WRITE         =  -19,  // Updater Error UPDATE_ERROR_WRITE        -20   // (1-20)  = -19

  // TAR: adding -30 offset to actual error values from untar.h
  ESP32_TARGZ_TAR_ERR_DATACB_FAIL        =  -32,  // Tar Error TAR_ERR_DATACB_FAIL       -30   // (-2-30) = -32
  ESP32_TARGZ_TAR_ERR_HEADERCB_FAIL      =  -33,  // Tar Error TAR_ERR_HEADERCB_FAIL     -30   // (-3-30) = -33
  ESP32_TARGZ_TAR_ERR_FOOTERCB_FAIL      =  -34,  // Tar Error TAR_ERR_FOOTERCB_FAIL     -30   // (-4-30) = -34
  ESP32_TARGZ_TAR_ERR_READBLOCK_FAIL     =  -35,  // Tar Error TAR_ERR_READBLOCK_FAIL    -30   // (-5-30) = -35
  ESP32_TARGZ_TAR_ERR_HEADERTRANS_FAIL   =  -36,  // Tar Error TAR_ERR_HEADERTRANS_FAIL  -30   // (-6-30) = -36
  ESP32_TARGZ_TAR_ERR_HEADERPARSE_FAIL   =  -37,  // Tar Error TAR_ERR_HEADERPARSE_FAIL  -30   // (-7-30) = -37

} ErrorCodes ;



struct TarGzStream
{
  Stream *gz;
  Stream *tar;
  Stream *output;
  size_t gz_size;
  size_t tar_size;
  size_t output_size;
};



// md5sum (essentially for debug)
#include "helpers/md5_sum.h"
// helpers: mkdir, mkpath, dirname
#include "helpers/path_tools.h"


#endif // #ifdef _ESP_TGZ_H
