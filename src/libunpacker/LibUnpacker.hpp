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

#include "../ESP32-targz-lib.hpp"

namespace TAR
{
  #include "../TinyUntar/untar.h"
}

namespace GZ
{
  #include "../uzlib/uzlib.h"
}


#include "../types/esp32_targz_types.h"


struct BaseUnpacker
{
  BaseUnpacker();
  bool   tarGzHasError();
  int8_t tarGzGetError();
  void   tarGzClearError();
  void   haltOnError( bool halt );
  void   initFSCallbacks();

  // TODO: move these debug tools outside the library
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
    #if defined ESP32
      #if defined ESP_IDF_VERSION_MAJOR && ESP_IDF_VERSION_MAJOR >= 4
        return file->path();
      #else
        return file->name();
      #endif
    #elif defined ESP8266 || defined ARDUINO_ARCH_RP2040
      return file->fullName();
    #else
      return nullptr;
      #error "Unsupported architecture"
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

#endif // #if defined ESP32 && defined HAS_OTA_SUPPORT



