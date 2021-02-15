# 🗜️ ESP32-targz

## An ESP32/ESP8266 Arduino library to provide decompression support for .tar, .gz and .tar.gz files

[![arduino-library-badge](https://www.ardu-badge.com/badge/ESP32-targz.svg?)](https://www.ardu-badge.com/ESP32-targz)

<p align="center">
<img src="ESP32-targz.png" alt="ES32-targz logo" width="512" />
</p>


## This library is a wrapper for the following two great libraries:

  - uzlib https://github.com/pfalcon/uzlib
  - TinyUntar https://github.com/dsoprea/TinyUntar

This library enables the channeling of gz :arrow_right: tar :arrow_right: filesystem data ~~without using an intermediate file~~ (bug: see [#4](https://github.com/tobozo/ESP32-targz/issues/4)).

In order to reach this goal, TinyUntar was heavily modified to allow data streaming, uzlib is also customized.

Tradeoffs
---------

When the output is the filesystem (e.g. NOT when streaming to TAR), gzip can work without the dictionary.
Disabling the dictionary can cause huge slowdowns but saves ~36KB of ram.

TinyUntar requires 512bytes only so its memory footprint is negligible.



Scope
-----

  - This library is only for unpacking / decompressing, no compression support is provided whatsoever
  - Although the examples use SPIFFS as default, it should work with any fs::FS filesystem (SD, SD_MMC, FFat, LittleFS) and streams (HTTP, HTTPS, UDP, CAN, Ethernet)
  - This is experimental, expect bugs!
  - Contributions and feedback are more than welcome :-)


Usage
-----

```C
    // Set **destination** filesystem by uncommenting one of these:
    //#define DEST_FS_USES_SPIFFS
    //#define DEST_FS_USES_FFAT
    //#define DEST_FS_USES_SD
    //#define DEST_FS_USES_SD_MMC
    #define DEST_FS_USES_LITTLEFS
    #include <ESP32-targz.h>
    // filesystem object will be available as "tarGzFs"
```



Extract content from `.gz` file
-------------------------------

```C

    // mount spiffs (or any other filesystem)
    tarGzFs.begin();

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
    if( ! gzExpander(tarGzFs, "/blah.gz", tarGzFs, "/blah.jpg") ) {
      Serial.printf("operation failed with return code #%d", GZUnpacker->tarGzGetError() );
    }


```


Expand contents from `.tar` file to `/tmp` folder
-------------------------------------------------

```C

    // mount spiffs (or any other filesystem)
    tarGzFs.begin();

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
    tarGzFs.begin();

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
    tarGzFs.begin();

    GzUnpacker *GZUnpacker = new GzUnpacker();

    GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
    GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
    GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
    GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity

    if( ! GZUnpacker->gzUpdater(tarGzFS, firmwareFile, /*don't restart after update*/false ) ) {
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






Callbacks
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

Return Codes
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

  - tarGzStreamExpander hates SPIFFS
  - tarGzExpander/tarExpander: some formats aren't supported with SPIFFS (e.g contains symlinks or long filename/path)
  - tarGzExpander without intermediate file hates situations with low heap
  - tarGzExpander/gzExpander on ESP8266 : while the provided examples will work, the 32Kb dynamic allocation for gzip dictionary is unlikely to work in real world scenarios (e.g. with a webserver) and would probably require static allocation

  ~~- tarGzExpander: files smaller than 4K aren't processed~~
  - ~~error detection isn't deferred efficiently, debugging may be painful~~
  - ~~.tar files containing files smaller than 512 bytes aren't fully processed~~
  - ~~reading/writing simultaneously on SPIFFS may induce errors~~


Debugging:
----------

  - ESP32: use all of the "Debug level" values from the boards menu
  - ESP8266: Warning/Error when "Debug Port:Serial" is used, and Debug/Verbose when "Debug Level:Core" is selected from the boards menu


Resources
-----------
  - [LittleFS for ESP32](https://github.com/lorol/LITTLEFS)
  - [ESP8266 Sketch Data Upload tool for LittleFS](https://github.com/earlephilhower/arduino-esp8266littlefs-plugin)
  - [ESP32 Sketch Data Upload tool for FFat/LittleFS/SPIFFS](https://github.com/lorol/arduino-esp32fs-plugin/releases)

  ![image](https://user-images.githubusercontent.com/1893754/99714053-635de380-2aa5-11eb-98e3-631a94836742.png)


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


