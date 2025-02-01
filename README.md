# 🗜️ ESP32-targz

## An ESP32/ESP8266/RP2040 Arduino library to handle .tar, .gz and .tar.gz files

[![arduino-library-badge](https://www.ardu-badge.com/badge/ESP32-targz.svg?)](https://www.ardu-badge.com/ESP32-targz)
[![PlatformIO Registry](https://badges.registry.platformio.org/packages/tobozo/library/ESP32-targz.svg)](https://registry.platformio.org/packages/libraries/tobozo/ESP32-targz)

<p align="center">
<img src="ESP32-targz.png" alt="ES32-targz logo" width="512" />
</p>

## 🆕 ESP32-targz now supports compression!

## ESP32-targz is based on those two great libraries:

  - uzlib https://github.com/pfalcon/uzlib
  - TinyUntar https://github.com/dsoprea/TinyUntar

ESP32-targz enables the channeling of gz :arrow_left::arrow_right: tar :arrow_left::arrow_right: filesystem data in both directions.

Parental advisory: this project was made under the influence of hyperfocus and its code may contain comments that are unfit for children.


Scope
-----

  - Compressing to `.tar.gz`
  - Decompressing from `tar.gz` 
  - Compressing to `gz`
  - Decompressing from `gz` 
  - Packing files/folders to `tar`
  - Unpacking `tar`
  - Supports any fs::FS filesystem (SD, SD_MMC, FFat, LittleFS) and Stream (HTTP, HTTPS, UDP, CAN, Ethernet)
  - This is experimental, expect bugs!
  - Contributions and feedback are more than welcome :-)
  

Tradeoffs
---------

When decompressing to the filesystem (e.g. NOT when streaming to TAR), gzip can work without the dictionary.
Disabling the dictionary can cause huge slowdowns but saves ~36KB of ram.

TinyUntar requires 512bytes only so its memory footprint is negligible.


Limitations
-----------

- ESP32-targz decompression can only have one **output** filesystem (see *Support Matrix*), and it must be set at compilation time (see *Usage*).
This limitation does not apply to the **input** filesystem/stream.



Support Matrix
--------------


| fs::FS  | SPIFFS  |  LittleFS |  SD    |  SD_MMC |  FFAT |
| ------- |:------- | :-------- | :----- | :------ | :---- |
| ESP32   | 1.0     |  3.1.0    |  1.0.5 |  1.0    |  1.0  |
|         |         |           |        |         |       |
| ESP8266 | builtin |  0.1.0    |  0.1.0 |  n/a    |  n/a  |
|         |         |           |        |         |       |
| RP2040  | n/a     |  0.1.0    |  2.0.0 |  n/a    |  n/a  |



Usage
-----


:warning: Optional: setting the `#define` **before** including `<ESP32-targz.h>` will alias a default flash filesystem to `tarGzFS`.


```C
    // Set **destination** filesystem by uncommenting one of these:
    //#define DEST_FS_USES_SPIFFS
    //#define DEST_FS_USES_FFAT
    //#define DEST_FS_USES_SD
    //#define DEST_FS_USES_SD_MMC
    #define DEST_FS_USES_LITTLEFS
    #include <ESP32-targz.h>
    // filesystem object will be available as "tarGzFS"
```



Extract content from `.gz` file
-------------------------------

```C

    // mount spiffs (or any other filesystem)
    tarGzFS.begin();

    GzUnpacker *GZUnpacker = new GzUnpacker();

    GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
    GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
    GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
    GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity

    // expand one file
    if( !GZUnpacker->gzExpander(tarGzFS, "/gz_example.gz", tarGzFS, "/gz_example.jpg") ) {
      Serial.printf("gzExpander failed with return code #%d", GZUnpacker->tarGzGetError() );
    }

    // expand another file
    if( ! gzExpander(tarGzFS, "/blah.gz", tarGzFS, "/blah.jpg") ) {
      Serial.printf("operation failed with return code #%d", GZUnpacker->tarGzGetError() );
    }


```


Expand contents from `.tar` file to `/tmp` folder
-------------------------------------------------

```C

    // mount spiffs (or any other filesystem)
    tarGzFS.begin();

    TarUnpacker *TARUnpacker = new TarUnpacker();

    TARUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
    TARUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
    TARUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
    TARUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
    TARUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
    TARUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity

    if( !TARUnpacker->tarExpander(tarGzFS, "/tar_example.tar", tarGzFS, "/") ) {
      Serial.printf("tarExpander failed with return code #%d\n", TARUnpacker->tarGzGetError() );
    }


```



Expand contents from `.tar.gz`  to `/tmp` folder
------------------------------------------------

```C

    // mount spiffs (or any other filesystem)
    tarGzFS.begin();

    TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

    TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
    TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
    TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
    TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
    TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
    TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
    TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
    TARGZUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity

    // using an intermediate file (default is /tmp/tmp.tar)
    if( !TARGZUnpacker->tarGzExpander(tarGzFS, "/targz_example.tar.gz", tarGzFS, "/tmp") ) {
      Serial.printf("tarGzExpander+intermediate file failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
    }

    // or without intermediate file
    if( !TARGZUnpacker->tarGzExpander(tarGzFS, "/targz_example.tar.gz", tarGzFS, "/tmp", nullptr ) ) {
      Serial.printf("tarGzExpander+intermediate file failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
    }


```


Flash the ESP with contents from `.gz` file
-------------------------------------------

```C

    // mount spiffs (or any other filesystem)
    tarGzFS.begin();

    GzUnpacker *GZUnpacker = new GzUnpacker();

    GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
    GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
    GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
    GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity

    if( ! GZUnpacker->gzUpdater(tarGzFS, firmwareFile, U_FLASH,/*don't restart after update*/false ) ) {
      Serial.printf("gzUpdater failed with return code #%d\n", GZUnpacker->tarGzGetError() );
    }


```


ESP32 Only: Flash the ESP with contents from `.gz` stream
---------------------------------------------------------

```C

    // mount spiffs (or any other filesystem)
    tarGzFS.begin();

    fs::File file = tarGzFS.open( "/example_firmware.gz", "r" );

    if (!file) {
      Serial.println("Can't open file");
      return;
    }

    GzUnpacker *GZUnpacker = new GzUnpacker();

    GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
    GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
    GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
    GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity

    if( !GZUnpacker->gzStreamUpdater( (Stream *)&file, UPDATE_SIZE_UNKNOWN ) ) {
      Serial.printf("gzStreamUpdater failed with return code #%d\n", GZUnpacker->tarGzGetError() );
    }


```


ESP32 Only: Direct expansion (no intermediate file) from `.tar.gz.` stream
--------------------------------------------------------------------------
```C

    // mount spiffs (or any other filesystem)
    tarGzFS.begin();

    fs::File file = tarGzFS.open( "/example_archive.tgz", "r" );

    if (!file) {
      Serial.println("Can't open file");
      return;
    }

    TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

    TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
    TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
    TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
    TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
    TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
    TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
    TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
    TARGZUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity

    if( !TARGZUnpacker->tarGzStreamExpander( (Stream *)&file, tarGzFS ) ) {
      Serial.printf("tarGzStreamExpander failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
    }


```


ESP32 Only: Direct Update (no intermediate file) from `.tar.gz.` stream
-----------------------------------------------------------------------
```C

    TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

    TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
    TARGZUnpacker->setTarVerify( false ); // nothing to verify as we're writing a partition
    TARGZUnpacker->setGzProgressCallback( BaseUnpacker::targzNullProgressCallback ); // don't care about gz progress
    TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual partition
    TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
    TARGZUnpacker->setTarMessageCallback( myTarMessageCallback/*BaseUnpacker::targzPrintLoggerCallback*/ ); // tar log verbosity

    // mount SD
    SD.begin();

    // this .tar.gz file has both the "app.ino.bin" and "app.spiffs.bin" partitions
    fs::File file = SD.open( "/bundle_firmware.tar.gz", "r" );

    if (!file) {
      Serial.println("Can't open file");
      return;
    }

    // this could also be a HTTP/HTTPS/UDP/Ethernet Stream
    Stream *streamptr = &file;

    if( !TARGZUnpacker->tarGzStreamUpdater( streamptr ) ) {
      Serial.printf("tarGzStreamUpdater failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
    } else {
      Serial.println( "Flashing successful, now restarting" );
      ESP.restart();
    }


```


LZPacker::compress() signatures:
-------------------------------
```cpp
  // buffer to stream (best compression)
  size_t compress( uint8_t* srcBuf, size_t srcBufLen, Stream* dstStream );
  // buffer to buffer (best compression)
  size_t compress( uint8_t* srcBuf, size_t srcBufLen, uint8_t** dstBufPtr );
  // stream to buffer
  size_t compress( Stream* srcStream, size_t srcLen, uint8_t** dstBufPtr );
  // stream to stream
  size_t compress( Stream* srcStream, size_t srcLen, Stream* dstStream );
  // stream to file
  size_t compress( Stream* srcStream, size_t srcLen, fs::FS*dstFS, const char* dstFilename );
  // file to file
  size_t compress( fs::FS *srcFS, const char* srcFilename, fs::FS*dstFS, const char* dstFilename );
  // file to stream
  size_t compress( fs::FS *srcFS, const char* srcFilename, Stream* dstStream );
```

Compress to `.gz` (buffer to stream)
-------------------------------

```C
    const char* json = "{\"hello\":\"world\"}"; // input buffer
    File out = LittleFS.open("/out.gz", "w");   // output stream
    size_t compressedSize = LZPacker::compress( (uint8_t*)json, strlen(json), &out );
    out.close();
```

Compress to `.gz` (buffer to buffer)
-------------------------------

```C
    const char* json = "{\"hello\":\"world\"}"; // input buffer
    uint8_t* compressedBytes;                   // output buffer
    size_t compressedSize = LZPacker::compress( (uint8_t*)json, strlen(json), &compressedBytes);
    // do something with compressedBytes
    free(compressedBytes);
```

Compress to `.gz` (stream to buffer)
-------------------------------

```C
    File in = LittleFS.open("/my.uncompressed.file.txt"); // input stream
    uint8_t* compressedBytes;                             // output buffer
    size_t compressedSize = LZPacker::compress( &in, in.size(), &compressedBytes );
    // do something with compressedBytes
    free(compressedBytes);
    in.close();
```

Compress to `.gz` (stream to stream)
-------------------------------

```C
    File in = LittleFS.open("/my.uncompressed.file.txt"); // input stream
    File out = LittleFS.open("/out.gz", "w");             // output stream
    size_t compressedSize = LZPacker::compress( &in, in.size(), &out );
    out.close();
    in.close();
```
    

TarPacker::pack_files() signatures:
-------------------------------
```cpp
  int pack_files(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, Stream* dstStream, const char* tar_prefix=nullptr);
  int pack_files(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, fs::FS *dstFS, const char*tar_output_file_path, const char* tar_prefix=nullptr);
```
    
    
Pack to `.tar` (entities to File)
-------------------------------
```C
  std::vector<TAR::dir_entity_t> dirEntities;
  TarPacker::collectDirEntities(&dirEntities, &LittleFS, "/folder/to/pack");
  auto packedSize = TarPacker::pack_files(&LittleFS, dirEntities, &LittleFS, "/my.archive.tar");
```
  
Pack to `.tar` (entities to Stream)
-------------------------------
```C
  std::vector<TAR::dir_entity_t> dirEntities;
  TarPacker::collectDirEntities(&dirEntities, &LittleFS, "/folder/to/pack");
  File tarOutfile = LittleFS.open("/my.archive.tar", "w");  
  size_t packedSize = TarPacker::pack_files(&LittleFS, dirEntities, &tarOutfile);
  tarOutfile.close();
```
  
  
TarGzPacker::compress() signatures:
-------------------------------  

```cpp
  int compress(fs::FS *srcFS, const char* srcDir, Stream* dstStream, const char* tar_prefix=nullptr);
  int compress(fs::FS *srcFS, const char* srcDir, fs::FS *dstFS, const char* tgz_name, const char* tar_prefix=nullptr);
  
  int compress(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, Stream* dstStream, const char* tar_prefix=nullptr);
  int compress(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, fs::FS *dstFS, const char* tgz_name, const char* tar_prefix=nullptr);
```
  
  
Pack & compress to `.tar.gz` file/stream (no filtering on source files/folders list, recursion applies)
-------------------------------  
```C
  File TarGzOutFile = LittleFS.open("/my.archive.tar.gz", "w");
  size_t compressedSize = TarGzPacker::compress(&LittleFS/*source*/, "/folder/to/compress", &TarGzOutFile);
  TarGzOutFile.close();
```

Pack & compress to `.tar.gz` file/stream
-------------------------------  

```C
  std::vector<TAR::dir_entity_t> dirEntities;
  TarPacker::collectDirEntities(&dirEntities, &LittleFS/*source*/, "/folder/to/compress");
  // eventually filter content from dirEntities
  File TarGzOutFile = LittleFS.open("/my.archive.tar.gz", "w");
  size_t compressedSize = TarGzPacker::compress(&LittleFS/*source*/, dirEntities, &TarGzOutFile);
  TarGzOutFile.close();
```

Pack & compress to `.tar.gz` file (no filtering on source files/folders list, recursion applies)
-------------------------------  
```C
  File TarGzOutFile = LittleFS.open("/my.archive.tar.gz", "w");
  size_t compressedSize = TarGzPacker::compress(&LittleFS/*source*/, "/folder/to/compress", &LittleFS/*destination*/, "/my.archive.tar.gz");
  TarGzOutFile.close();
```


Pack & compress to `.tar.gz` file
-------------------------------  

```C
  std::vector<TAR::dir_entity_t> dirEntities;
  TarPacker::collectDirEntities(&dirEntities, &LittleFS/*source*/, "/folder/to/compress");
  // eventually filter content from dirEntities
  File TarGzOutFile = LittleFS.open("/my.archive.tar.gz", "w");
  size_t compressedSize = TarGzPacker::compress(&LittleFS/*source*/, dirEntities, &LittleFS/*destination*/, "/my.archive.tar.gz");
  TarGzOutFile.close();
```
  




  


TarGzUnpacker/GzUnpacker/TarUnpacker Callbacks
---------

```C


    // basic progress callback (valid for tar or gzip)
    void myBasicProgressCallback( uint8_t progress )
    {
      Serial.printf("Progress: %d\n", progress );
    }


    // complex progress callback (valid for tar or gzip)
    void myProgressCallback( uint8_t progress )
    {
      static int8_t myLastProgress = -1;
      if( myLastProgress != progress ) {
        if( myLastProgress == -1 ) {
          Serial.print("Progress: ");
        }
        myLastProgress = progress;
        switch( progress ) {
          case   0: Serial.print("0% ▓");  break;
          case  25: Serial.print(" 25% ");break;
          case  50: Serial.print(" 50% ");break;
          case  75: Serial.print(" 75% ");break;
          case 100: Serial.print("▓ 100%\n"); myLastProgress = -1; break;
          default: if( progress < 100) Serial.print( "▓" ); break;
        }
      }
    }


    // General Error/Warning/Info logger
    void myLogger(const char* format, ...)
    {
      va_list args;
      va_start(args, format);
      vprintf(format, args);
      va_end(args);
    }


    // status callback for TAR (fired at file creation)
    void myTarStatusProgressCallback( const char* name, size_t size, size_t total_unpacked )
    {
      Serial.printf("[TAR] %-64s %8d bytes - %8d Total bytes\n", name, size, total_unpacked );
    }



```

TarGzUnpacker/GzUnpacker/TarUnpacker Return Codes 
------------

`*Unpacker->tarGzGetError()` returns a value when a problem occured:

  - General library error codes

    - `0`    : Yay no error!
    - `-1`   : Filesystem error
    - `-6`   : Same a Filesystem error
    - `-7`   : Update not finished? Something went wrong
    - `-38`  : Logic error during deflating
    - `-39`  : Logic error during gzip read
    - `-40`  : Logic error during file creation
    - `-100` : No space left on device
    - `-101` : No space left on device
    - `-102` : No space left on device
    - `-103` : Not enough heap
    - `-104` : Gzip dictionnary needs to be enabled
    - `-105` : Gz Error when parsing header
    - `-106` : Gz Error when allocating memory
    - `-107` : General error, file integrity check fail

  - UZLIB: forwarding error values from uzlib.h as is (no offset)

    - `-2`   : Not a valid gzip file
    - `-3`   : Gz Error TINF_DATA_ERROR
    - `-4`   : Gz Error TINF_CHKSUM_ERROR
    - `-5`   : Gz Error TINF_DICT_ERROR
    - `-41`  : Gz error, can't guess filename

  - UPDATE: applying -20 offset to forwarded error values from Update.h

    - `-8`   : Updater Error UPDATE_ERROR_ABORT
    - `-9`   : Updater Error UPDATE_ERROR_BAD_ARGUMENT
    - `-10`  : Updater Error UPDATE_ERROR_NO_PARTITION
    - `-11`  : Updater Error UPDATE_ERROR_ACTIVATE
    - `-12`  : Updater Error UPDATE_ERROR_MAGIC_BYTE
    - `-13`  : Updater Error UPDATE_ERROR_MD5
    - `-14`  : Updater Error UPDATE_ERROR_STREAM
    - `-15`  : Updater Error UPDATE_ERROR_SIZE
    - `-16`  : Updater Error UPDATE_ERROR_SPACE
    - `-17`  : Updater Error UPDATE_ERROR_READ
    - `-18`  : Updater Error UPDATE_ERROR_ERASE
    - `-19`  : Updater Error UPDATE_ERROR_WRITE

  - TAR: applying -30 offset to forwarded error values from untar.h

    - `32`  : Tar Error TAR_ERR_DATACB_FAIL
    - `33`  : Tar Error TAR_ERR_HEADERCB_FAIL
    - `34`  : Tar Error TAR_ERR_FOOTERCB_FAIL
    - `35`  : Tar Error TAR_ERR_READBLOCK_FAIL
    - `36`  : Tar Error TAR_ERR_HEADERTRANS_FAIL
    - `37`  : Tar Error TAR_ERR_HEADERPARSE_FAIL
    - `38`  : Tar Error TAR_ERROR_HEAP



Test Suite
----------

  - 📷 [ESP32 capture](extras/esp32-test-suite.gif)
  - 📷 [ESP8266 capture](extras/esp8266-test-suite.gif)


Known bugs
----------

  - SPIFFS is deprecated, migrate to LittleFS!
  - tarGzExpander/tarExpander: symlinks or long filename/path not supported, path limit is 100 chars
  - tarGzExpander without intermediate file uses a lot of heap
  - tarGzExpander/gzExpander on ESP8266 : while the provided examples will work, the 32Kb dynamic allocation for gzip dictionary is unlikely to work in real world scenarios (e.g. with a webserver) and would probably require static allocation



Debugging:
----------

  - ESP32: use all of the "Debug level" values from the boards menu
  - ESP8266: Warning/Error when "Debug Port:Serial" is used, and Debug/Verbose when "Debug Level:Core" is selected from the boards menu
  - RP2040: only "Debug port: Serial" and "Debug Level: Core" enable logging


Resources
-----------
  - [ESP8266 Sketch Data Upload tool for LittleFS](https://github.com/earlephilhower/arduino-esp8266littlefs-plugin)
  - [ESP32 Sketch Data Upload tool for FFat/LittleFS/SPIFFS](https://github.com/lorol/arduino-esp32fs-plugin/releases)
  - [Pico LittlsFS Data Upload tool](https://github.com/earlephilhower/arduino-pico-littlefs-plugin)

  ![image](https://user-images.githubusercontent.com/1893754/99714053-635de380-2aa5-11eb-98e3-631a94836742.png)


Alternate links
---------------
  - [https://github.com/vortigont/esp32-flashz](https://github.com/vortigont/esp32-flashz) OTA-Update your ESP32 from zlib compressed binaries (not gzip)
  - [https://github.com/chrisjoyce911/esp32FOTA](https://github.com/chrisjoyce911/esp32FOTA) OTA-Update your ESP32 from zlib or gzip compressed binaries
  - [https://github.com/laukik-hase/esp_compression](https://github.com/laukik-hase/esp_compression) inflate/deflate miniz/uzlib based esp-idf implementation

Credits:
--------
  - [pfalcon](https://github.com/pfalcon/uzlib) (uzlib maintainer)
  - [dsoprea](https://github.com/dsoprea/TinyUntar) (TinyUntar maintainer)
  - [lorol](https://github.com/lorol) (LittleFS-ESP32 + fs plugin)
  - [me-no-dev](https://github.com/me-no-dev) (inspiration and support)
  - [atanisoft](https://github.com/atanisoft) (motivation and support)
  - [lbernstone](https://github.com/lbernstone) (motivation and support)
  - [scubachristopher](https://github.com/scubachristopher) (contribution and support)
  - [infrafast](https://github.com/infrafast) (feedback fueler)
  - [vortigont](https://github.com/vortigont/) (inspiration and support)
  - [hitecSmartHome](https://github.com/hitecSmartHome) (feedback fueler)


