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

#pragma once

#include <FS.h>
#include "ESP32-targz-log.hpp"


#if defined ESP32

  #include <Update.h>
  #define HAS_OTA_SUPPORT

  // Figure out the chosen fs::FS library to load for the **destination** filesystem
  #if defined DEST_FS_USES_SPIFFS
    #include <SPIFFS.h>
    #define tarGzFS SPIFFS
    #define FS_NAME "SPIFFS"
  #elif defined DEST_FS_USES_FFAT
    #include <FFat.h>
    #define tarGzFS FFat
    #define FS_NAME "FFAT"
  #elif defined DEST_FS_USES_SD
    #include <SD.h>
    #define tarGzFS SD
    #define FS_NAME "SD"
  #elif defined DEST_FS_USES_SD_MMC
    #include <SD_MMC.h>
    #define tarGzFS SD_MMC
    #define FS_NAME "SD_MMC"
  #elif defined DEST_FS_USES_LITTLEFS
    #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(2, 0, 0)
      // littlefs is built-in since sdk 2.0.0
      #include <LittleFS.h>
      #define tarGzFS LittleFS
      #define FS_NAME "LittleFS (builtin)"
    #else
      // get "littlefs_esp32" from library manager
      #include <LITTLEFS.h>
      #define tarGzFS LITTLEFS
      #define FS_NAME "LITTLEFS (extlib)"
    #endif
  #elif defined DEST_FS_USES_PSRAMFS
    #include <PSRamFS.h> // https://github.com/tobozo/ESP32-PsRamFS
    #define tarGzFS PSRamFS
    #define FS_NAME "PSRamFS"
  #else
    // no filesystem, no helpers available, power user ?
  #endif

#elif defined ESP8266

  #include <Updater.h>
  #define HAS_OTA_SUPPORT

  // ESP8266 has no SD_MMC or FFat.h library, so these are implicitely invalidated
  #undef DEST_FS_USES_SD_MMC // unsupported
  #undef DEST_FS_USES_FFAT   // unsupported
  // the fuck with spamming the console
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"

  // Figure out the chosen fs::FS library to load for the **destination** filesystem

  #if defined DEST_FS_USES_SD
    #include <SD.h>
    #define tarGzFS SDFS
    #define FS_NAME "SDFS"
  #else
    #if defined DEST_FS_USES_LITTLEFS
      #include <LittleFS.h>
      #define tarGzFS LittleFS
      #define FS_NAME "LITTLEFS (extlib)"
    #elif defined DEST_FS_USES_SPIFFS
      #if defined USE_LittleFS // emulate SPIFFS using LittleFS
        #include <LittleFS.h>
        #define tarGzFS SPIFFS
        #define FS_NAME "LITTLEFS (subst)"
      #else // use core SPIFFS
        #define tarGzFS SPIFFS
        #define FS_NAME "SPIFFS"
      #endif
    #else // no destination filesystem defined in sketch
      #warning "Unspecified or invalid destination filesystem, please #define one of these before including the library: DEST_FS_USES_SPIFFS, DEST_FS_USES_LITTLEFS, DEST_FS_USES_SD, DEST_FS_USES_PSRAMFS"
      // however, check for USE_LittleFS as it is commonly defined since SPIFFS deprecation
      #if defined USE_LittleFS
        #include <LittleFS.h>
        #define tarGzFS LittleFS
        #warning "Defaulting to LittleFS"
        #define DEST_FS_USES_LITTLEFS
        #define FS_NAME "LITTLEFS (defaulted)"
      #else
        #define tarGzFS SPIFFS
        #warning "Defaulting to SPIFFS (soon deprecated)"
        #define DEST_FS_USES_SPIFFS
        #define FS_NAME "SPIFFS"
      #endif
    #endif
  #endif

  static FSInfo fsinfo;

#elif defined ARDUINO_ARCH_RP2040

  #pragma message "Experimental RP2040 support"

  #undef DEST_FS_USES_SD_MMC // unsupported
  #undef DEST_FS_USES_FFAT   // unsupported
  #undef DEST_FS_USES_SPIFFS // unsupported

  // Figure out the chosen fs::FS library to load for the **destination** filesystem
  #if defined DEST_FS_USES_SD
    #include <SD.h>
    #define tarGzFS SDFS
    #define FS_NAME "SD"
  #else
    #include <LittleFS.h>
    #define tarGzFS LittleFS
    #define FS_NAME "LITTLEFS (picolib)"
  #endif

  static FSInfo fsinfo;

#else

  #error "Only ESP32, ESP8266 and RP2040/Pico architectures are supported"

#endif

#if defined DEST_FS_USES_SPIFFS || defined DEST_FS_USES_LITTLEFS || defined DEST_FS_USES_FFAT
  #define WARN_LIMITED_FS
#endif

#include <stddef.h> // platformio whines about missing definition for 'size_t' 🤦

// required filesystem helpers are declared outside the main library
// because ESP32/ESP8266 <FS.h> use different abstraction flavours :)
__attribute__((unused)) static size_t targzFreeBytesFn() {
  #if defined DEST_FS_USES_SPIFFS || defined DEST_FS_USES_SD || defined DEST_FS_USES_SD_MMC || defined DEST_FS_USES_LITTLEFS || defined DEST_FS_USES_PSRAMFS
    #if defined ESP32
      return tarGzFS.totalBytes() - tarGzFS.usedBytes();
    #elif defined ESP8266 || defined ARDUINO_ARCH_RP2040
      if( tarGzFS.info( fsinfo ) ) {
        return fsinfo.totalBytes - fsinfo.usedBytes;
      } else {
        // fail
        return 0;
      }
    #else
      #error "Only ESP32, ESP8266 and RP2040/Pico are supported"
    #endif
  #elif defined DEST_FS_USES_FFAT
    return tarGzFS.freeBytes();
  #else
    // no filesystem, no helpers available, power user ?
    return 0;
  #endif
}

__attribute__((unused)) static size_t targzTotalBytesFn() {
  #if defined DEST_FS_USES_SPIFFS || defined DEST_FS_USES_SD || defined DEST_FS_USES_SD_MMC || defined DEST_FS_USES_LITTLEFS || defined DEST_FS_USES_FFAT || defined DEST_FS_USES_PSRAMFS
    #if defined ESP32
      return tarGzFS.totalBytes();
    #elif defined ESP8266 || defined ARDUINO_ARCH_RP2040
      if( tarGzFS.info( fsinfo ) ) {
        return fsinfo.totalBytes;
      } else {
        // fail
        return 0;
      }
    #else
      #error "Only ESP32, ESP8266 and RP2040/Pico are supported"
    #endif
  #else
    // no filesystem, no helpers available, power user ?
    return 0;
  #endif
}

#define GZIP_DICT_SIZE 32768

#if defined ESP8266
  #define GZIP_BUFF_SIZE 1024
#else
  #define GZIP_BUFF_SIZE 4096
#endif

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

#define CC_UNUSED __attribute__((unused))


namespace GZ
{
  #include "uzlib/uzlib.h"     // https://github.com/pfalcon/uzlib
}

namespace TAR
{
  extern "C"
  {
    #include "TinyUntar/untar.h" // https://github.com/dsoprea/TinyUntar
  }
}

#include <stdio.h>

// Callbacks for getting free/total space left on *destination* device.
// Optional but recommended to prevent SPIFFS/LittleFS/FFat partitions
// to explode during stream writes.
typedef size_t (*fsTotalBytesCb)();
typedef size_t (*fsFreeBytesCb)();

// setup filesystem helpers (totalBytes, freeBytes)
// must be done from outside the library since FS is an abstraction of an abstraction :(
typedef void (*fsSetupCallbacks)( fsTotalBytesCb cbt, fsFreeBytesCb cbf );

// overridable gz stream writer
typedef bool (*gzStreamWriter)( unsigned char* buff, size_t buffsize );
// overridable gz byte reader (used when no dictionary set)
typedef unsigned int (*gzDestByteReader)(int offset, unsigned char *out);

// tar doesn't have a real progress, so provide a status instead
typedef void (*tarStatusProgressCb)( const char* name, size_t size, size_t total_unpacked );

// tar has --exclude support, also provide --include
typedef bool (*tarExcludeFilter)( TAR::header_translated_t *header );
typedef bool (*tarIncludeFilter)( TAR::header_translated_t *header );

// Callbacks for progress and misc output messages, default is verbose
typedef void (*genericProgressCallback)( uint8_t progress );
typedef void (*genericLoggerCallback)( const char* format, ... );

// This is only to centralize error codes and spare the
// hassle of looking up in three different library folders

typedef enum tarGzErrorCode /* int8_t */
{

  ESP32_TARGZ_OK                         =   0,   // yay
  // general library errors
  ESP32_TARGZ_FS_ERROR                   =  -1,   // Filesystem error
  ESP32_TARGZ_STREAM_ERROR               =  -6,   // same as Filesystem error
  ESP32_TARGZ_UPDATE_INCOMPLETE          =  -7,   // Update not finished? Something went wrong
  ESP32_TARGZ_TAR_ERR_GZDEFL_FAIL        =  -38,  // Library error during deflating
  ESP32_TARGZ_TAR_ERR_GZREAD_FAIL        =  -39,  // Library error during gzip read
  ESP32_TARGZ_TAR_ERR_FILENAME_TOOLONG   =  -40,  // Library error during file creation
  ESP32_TARGZ_FS_FULL_ERROR              =  -100, // no space left on device
  ESP32_TARGZ_FS_WRITE_ERROR             =  -101, // no space left on device
  ESP32_TARGZ_FS_READSIZE_ERROR          =  -102, // no space left on device
  ESP32_TARGZ_HEAP_TOO_LOW               =  -103, // not enough heap
  ESP32_TARGZ_NEEDS_DICT                 =  -104, // gzip dictionnary needs to be enabled
  ESP32_TARGZ_UZLIB_PARSE_HEADER_FAILED  =  -105, // Gz Error when parsing header
  ESP32_TARGZ_UZLIB_MALLOC_FAIL          =  -106, // Gz Error when allocating memory
  ESP32_TARGZ_INTEGRITY_FAIL             =  -107, // General error, file integrity check fail

  // UZLIB: keeping error values from uzlib.h as is (no offset)
  ESP32_TARGZ_UZLIB_INVALID_FILE         =  -2,   // Not a valid gzip file
  ESP32_TARGZ_UZLIB_DATA_ERROR           =  -3,   // Gz Error TINF_DATA_ERROR
  ESP32_TARGZ_UZLIB_CHKSUM_ERROR         =  -4,   // Gz Error TINF_CHKSUM_ERROR
  ESP32_TARGZ_UZLIB_DICT_ERROR           =  -5,   // Gz Error TINF_DICT_ERROR
  ESP32_TARGZ_TAR_ERR_FILENAME_NOT_GZ    =  -41,  // Gz error, can't guess filename

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
  ESP32_TARGZ_TAR_ERR_HEAP_TOO_LOW       =  -38,  // Tar Error TAR_ERROR_HEAP            -30   // (-8-30) = -38

} ErrorCodes ;




struct BaseUnpacker
{
  BaseUnpacker();
  bool   tarGzHasError();
  int8_t tarGzGetError();
  void   tarGzClearError();
  void   haltOnError( bool halt );
  void   initFSCallbacks();
  void   tarGzListDir( fs::FS &fs, const char * dirName, uint8_t levels, bool hexDump = false);
  void   hexDumpData( const char* buff, size_t buffsize, uint32_t output_size = 32 );
  void   hexDumpFile( fs::FS &fs, const char* filename, uint32_t output_size = 32 );
  void   setLoggerCallback( genericLoggerCallback cb );
  void   setupFSCallbacks( fsTotalBytesCb cbt, fsFreeBytesCb cbf ); // setup abstract filesystem helpers (totalBytes, freeBytes)
  #ifdef ESP8266
    void   printDirectory(fs::FS &fs, File dir, int numTabs, uint8_t levels, bool hexDump);
  #endif
  #ifdef ESP32
    bool   setPsram( bool enable );
  #endif
  static const char* targzFSFilePath( fs::File *file ) {
    #if defined ESP_IDF_VERSION_MAJOR && ESP_IDF_VERSION_MAJOR >= 4
      return file->path();
    #else
      return file->name();
    #endif
  }
  static void tarNullProgressCallback( uint8_t progress ); // null progress callback
  static void targzNullLoggerCallback( const char* format, ... ); // null logger callback
  static void targzNullProgressCallback( uint8_t progress );  // null progress callback, use with setProgressCallback or setTarProgressCallback to silent output
  static void targzPrintLoggerCallback(const char* format, ...);  // error/warning/info logger, use with setLoggerCallback() to enable output
  static void defaultProgressCallback( uint8_t progress );  // print progress callback, use with setProgressCallback or setTarProgressCallback to enable progress output
  static void defaultTarStatusProgressCallback( const char* name, size_t size, size_t total_unpacked ); // print tar status since a progress can't be provided
  static void setFsTotalBytesCb( fsTotalBytesCb cb ); // filesystem helpers totalBytes
  static void setFsFreeBytesCb( fsFreeBytesCb cb ); // filesystem helpers freeBytes
  static void setGeneralError( tarGzErrorCode code ); // alias to static setError
  static void setReadTimeout( uint32_t read_timeout ); // read timeout: set high value (e.g. 10000ms) network and low value for filesystems
};


struct TarUnpacker : virtual public BaseUnpacker
{
  TarUnpacker();
  ~TarUnpacker();
  bool tarExpander( fs::FS &sourceFS, const char* fileName, fs::FS &destFS, const char* destFolder );
  bool tarStreamExpander( Stream *stream, size_t streamSize, fs::FS &destFS, const char* destFolder );
  //TODO: tarStreamExpander( Stream* sourceStream, fs::FS &destFS, const char* destFolder );
  void setTarStatusProgressCallback( tarStatusProgressCb cb );
  void setTarProgressCallback( genericProgressCallback cb ); // for tar
  void setTarMessageCallback( genericLoggerCallback cb ); // for tar
  void setTarVerify( bool verify ); // enables health checks but does slower writes

  // callback setters: handles criterias for allowing or skipping files/folders creation
  // NOTE: the exclude filter runs first, set to nullptr (default) for disabling
  void setTarExcludeFilter( tarExcludeFilter cb );
  void setTarIncludeFilter( tarIncludeFilter cb );

  static int tarStreamReadCallback( unsigned char* buff, size_t buffsize );
  static int tarStreamWriteCallback( TAR::header_translated_t *header, int entry_index, void *context_data, unsigned char *block, int length);

  static int tarHeaderCallBack(TAR::header_translated_t *header,  int entry_index,  void *context_data);
  static int tarEndCallBack( TAR::header_translated_t *header, int entry_index, void *context_data);

  #if defined HAS_OTA_SUPPORT
    static int tarHeaderUpdateCallBack(TAR::header_translated_t *header,  int entry_index,  void *context_data);
    static int tarEndUpdateCallBack( TAR::header_translated_t *header, int entry_index, void *context_data);
    static int tarStreamWriteUpdateCallback(TAR::header_translated_t *header, int entry_index, void *context_data, unsigned char *block, int length);
  #endif

};


struct GzUnpacker : virtual public BaseUnpacker
{
  GzUnpacker();
  bool    gzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFile = nullptr );
  bool    gzStreamExpander( Stream* sourceStream, fs::FS destFS, const char* destFile );
  bool    gzStreamExpander( Stream *stream, size_t gz_size = 0 ); // use with setStreamWriter
  void    setGzProgressCallback( genericProgressCallback cb );
  void    setGzMessageCallback( genericLoggerCallback cb );
  //void    setStreamReader( gzStreamReader cb ); // optional, use with gzStreamExpander
  void    setStreamWriter( gzStreamWriter cb ); // optional, use with gzStreamExpander
  void    setDestByteReader( gzDestByteReader cb );
  void    gzExpanderCleanup();
  int     gzUncompress( bool isupdate = false, bool stream_to_tar = false, bool use_dict = true, bool show_progress = true );
  static bool         gzStreamWriteCallback( unsigned char* buff, size_t buffsize );
  static bool         gzReadHeader(fs::File &gzFile);
  static uint8_t      gzReadByte(fs::File &gzFile, const int32_t addr, fs::SeekMode mode=fs::SeekSet);
  static unsigned int gzReadDestByteFS(int offset, unsigned char *out);
  static unsigned int gzReadSourceByte(struct GZ::TINF_DATA *data, unsigned char *out);
  #if defined HAS_OTA_SUPPORT
    bool        gzUpdater( fs::FS &sourceFS, const char* gz_filename, int partition = U_FLASH, bool restart_on_update = true ); // flashes the ESP with the content of a *gzipped* file
    bool        gzStreamUpdater( Stream *stream, size_t update_size = 0, int partition = U_FLASH, bool restart_on_update = true ); // flashes the ESP from a gzip stream, no progress callback
    static bool gzUpdateWriteCallback( unsigned char* buff, size_t buffsize );
  #endif
  bool nodict = false;
  inline void noDict( bool force_disable_dict = true ) { nodict = force_disable_dict; };
};


struct TarGzUnpacker : public TarUnpacker, public GzUnpacker
{

  TarGzUnpacker();
  // unzip sourceFS://sourceFile.tar.gz contents into destFS://destFolder
  bool tarGzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFolder="/tmp", const char* tempFile = "/tmp/data.tar" );
  // same as tarGzExpander but without intermediate file
  bool tarGzExpanderNoTempFile( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFolder="/tmp" );
  // unpack stream://fileName.tar.gz contents to destFS::/destFolder/
  bool tarGzStreamExpander( Stream *stream, fs::FS &destFs, const char* destFolder = "/", int64_t streamSize = -1 );

  static bool gzProcessTarBuffer( unsigned char* buff, size_t buffsize );
  static int tarReadGzStream( unsigned char* buff, size_t buffsize );
  static int gzFeedTarBuffer( unsigned char* buff, size_t buffsize );

  #if defined HAS_OTA_SUPPORT
    // requirements: targz archive must contain files with names suffixed by ".ino.bin" and/or ".spiffs.bin"
    bool tarGzStreamUpdater( Stream *stream );
  #endif

};




#if defined ESP32 && defined HAS_OTA_SUPPORT

  // this class was inspired by https://github.com/vortigont/esp32-flashz

  class GzUpdateClass : public UpdateClass {

      GzUpdateClass(){};     // hidden c-tor
      ~GzUpdateClass(){};    // hidden d-tor

      bool mode_gz = false;  // needed to keep mode state for async writez() calls
      int command = U_FLASH; // needed to keep track of the destination partition
      GzUnpacker gzUnpacker;

      /**
      * @brief callback for GzUnpacker
      * writes inflated firmware chunk to flash
      *
      */
      //int flash_cb(size_t index, const uint8_t* data, size_t size, bool final);    //> inflate_cb_t

      public:
          // this is a singleton, no copy's
          GzUpdateClass(const GzUpdateClass&) = delete;
          GzUpdateClass& operator=(const GzUpdateClass &) = delete;
          GzUpdateClass(GzUpdateClass &&) = delete;
          GzUpdateClass & operator=(GzUpdateClass &&) = delete;

          static GzUpdateClass& getInstance(){
              static GzUpdateClass flashz;
              return flashz;
          }

          /**
          * @brief initilize GzUnpacker structs and UpdaterClass
          *
          * @return true on success
          * @return false on GzUnpacker mem allocation error or flash free space error
          */
          bool begingz(size_t size=UPDATE_SIZE_UNKNOWN, int command = U_FLASH, int ledPin = -1, uint8_t ledOn = LOW, const char *label = NULL);

          /**
          * @brief Writes a buffer to the flash
          * Returns true on success
          *
          * @param buff
          * @param buffsize
          * @return processed bytes
          */
          //size_t writez(const uint8_t *data, size_t len, bool final);
          static bool gzUpdateWriteCallback( unsigned char* buff, size_t buffsize );

          /**
          * @brief Read zlib compressed data from stream, decompress and write it to flash
          * size of the stream must be known in order to signal zlib inflator last chunk
          *
          * @param data Stream object, usually data from a tcp socket
          * @param len total length of compressed data to read from stream (actually ignored)
          * @return size_t number of bytes processed from a stream
          */
          size_t writeGzStream(Stream &data, size_t len);

          /**
          * @brief abort running inflator and flash update process
          * also releases inflator memory
          */
          void abortgz();

          /**
          * @brief release inflator memory and run UpdateClass.end()
          * returns status of end() call
          *
          * @return true
          * @return false
          */
          bool endgz(bool evenIfRemaining = true);

  };

#endif

// md5sum (essentially for debug)
#include "helpers/md5_sum.h"
// helpers: mkdir, mkpath, dirname
#include "helpers/path_tools.h"


//#endif // #ifdef _ESP_TGZ_H
