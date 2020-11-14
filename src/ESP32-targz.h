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

\*/

#ifndef _ESP_TGZ_H
#define _ESP_TGZ_H

#include <FS.h>

#if defined( ESP32 )
  #include <SPIFFS.h>
  //#include <SD.h>
  //#include <SD_MMC.h>
  #include <Update.h>
#elif defined( ESP8266 )
  #ifdef USE_LittleFS
    #define SPIFFS LittleFS
    #include <LittleFS.h>
  #endif
  #include <Updater.h>
  #define log_e tgzLogger
  #define log_w tgzLogger
  #define log_d tgzNullLogger
  #define log_v tgzNullLogger
#else
  #error Unsupported architecture
#endif


// unzip sourceFS://sourceFile.tar.gz contents into destFS://destFolder
bool     tarGzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFolder="/tmp" );
// unpack sourceFS://fileName.tar contents to destFS::/destFolder/
bool     tarExpander( fs::FS &sourceFS, const char* fileName, fs::FS &destFS, const char* destFolder );
// uncompresses *gzipped* sourceFile to destFile, filesystems may differ
bool    gzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFile );
// flashes the ESP with the content of a *gzipped* file
bool    gzUpdater( fs::FS &fs, const char* gz_filename );
// naive ls
void    tarGzListDir( fs::FS &fs, const char * dirName, uint8_t levels=1, bool hexDump = false );
// fs helper
char    *dirname(char *path);
// useful to share the buffer so it's not totally wasted memory outside targz scope
uint8_t *getGzBufferUint8();
void    hexDumpFile( fs::FS &fs, const char* filename);

// callbacks for progress and misc output messages
typedef void (*genericProgressCallback)( uint8_t progress );
typedef void (*genericLoggerCallback)( const char* format, ... );
void    setProgressCallback( genericProgressCallback cb );
void    setLoggerCallback( genericLoggerCallback cb );

// error handling
int8_t  tarGzGetError();
void    tarGzClearError();
bool    tarGzHasError();

// this is only to centralize error codes and spare the
// hassle of looking up in three different library folders

typedef enum tarGzErrorCode {
  ESP32_TARGZ_OK                         =   0,  // yay
  ESP32_TARGZ_FS_ERROR                   =  -1,  // Filesystem error
  // keeping error values from uzlib.h
  ESP32_TARGZ_UZLIB_INVALID_FILE         =  -2,  // Not a valid gzip file
  ESP32_TARGZ_UZLIB_DATA_ERROR           =  -3,  // TINF_DATA_ERROR
  ESP32_TARGZ_UZLIB_CHKSUM_ERROR         =  -4,  // TINF_CHKSUM_ERROR
  ESP32_TARGZ_UZLIB_DICT_ERROR           =  -5,  // TINF_DICT_ERROR

  ESP32_TARGZ_STREAM_ERROR               =  -6,  // same a Filesystem error

  ESP32_TARGZ_UPDATE_INCOMPLETE          =  -7,  // Update not finished? Something went wrong
  // adding -20 offset to actual error values from Update.h
  ESP32_TARGZ_UPDATE_ERROR_ABORT         =  -8,  // UPDATE_ERROR_ABORT        -20   // (12-20) = -8
  ESP32_TARGZ_UPDATE_ERROR_BAD_ARGUMENT  =  -9,  // UPDATE_ERROR_BAD_ARGUMENT -20   // (11-20) = -9
  ESP32_TARGZ_UPDATE_ERROR_NO_PARTITION  =  -10, // UPDATE_ERROR_NO_PARTITION -20   // (10-20) = -10
  ESP32_TARGZ_UPDATE_ERROR_ACTIVATE      =  -11, // UPDATE_ERROR_ACTIVATE     -20   // (9-20)  = -11
  ESP32_TARGZ_UPDATE_ERROR_MAGIC_BYTE    =  -12, // UPDATE_ERROR_MAGIC_BYTE   -20   // (8-20)  = -12
  ESP32_TARGZ_UPDATE_ERROR_MD5           =  -13, // UPDATE_ERROR_MD5          -20   // (7-20)  = -13
  ESP32_TARGZ_UPDATE_ERROR_STREAM        =  -14, // UPDATE_ERROR_STREAM       -20   // (6-20)  = -14
  ESP32_TARGZ_UPDATE_ERROR_SIZE          =  -15, // UPDATE_ERROR_SIZE         -20   // (5-20)  = -15
  ESP32_TARGZ_UPDATE_ERROR_SPACE         =  -16, // UPDATE_ERROR_SPACE        -20   // (4-20)  = -16
  ESP32_TARGZ_UPDATE_ERROR_READ          =  -17, // UPDATE_ERROR_READ         -20   // (3-20)  = -17
  ESP32_TARGZ_UPDATE_ERROR_ERASE         =  -18, // UPDATE_ERROR_ERASE        -20   // (2-20)  = -18
  ESP32_TARGZ_UPDATE_ERROR_WRITE         =  -19, // UPDATE_ERROR_WRITE        -20   // (1-20)  = -19

  ESP32_TARGZ_TAR_ERR_DATACB_FAIL        =  -32, // TAR_ERR_DATACB_FAIL       -30   // (-2-30) = -32
  ESP32_TARGZ_TAR_ERR_HEADERCB_FAIL      =  -33, // TAR_ERR_HEADERCB_FAIL     -30   // (-3-30) = -33
  ESP32_TARGZ_TAR_ERR_FOOTERCB_FAIL      =  -34, // TAR_ERR_FOOTERCB_FAIL     -30   // (-4-30) = -34
  ESP32_TARGZ_TAR_ERR_READBLOCK_FAIL     =  -35, // TAR_ERR_READBLOCK_FAIL    -30   // (-5-30) = -35
  ESP32_TARGZ_TAR_ERR_HEADERTRANS_FAIL   =  -36, // TAR_ERR_HEADERTRANS_FAIL  -30   // (-6-30) = -36
  ESP32_TARGZ_TAR_ERR_HEADERPARSE_FAIL   =  -37, // TAR_ERR_HEADERPARSE_FAIL  -30   // (-7-30) = -37

  ESP32_TARGZ_TAR_ERR_GZDEFL_FAIL        =  -38,
  ESP32_TARGZ_TAR_ERR_GZREAD_FAIL        =  -39,
  ESP32_TARGZ_TAR_ERR_FILENAME_TOOLONG   =  -40,


} ErrorCodes ;



#endif // #ifdef _ESP_TGZ_H
