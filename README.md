# ESP32-targz

## An ESP32/ESP8266 Arduino library to provide decompression support for .tar, .gz and .tar.gz files

[![arduino-library-badge](https://www.ardu-badge.com/badge/ESP32-targz.svg?)](https://www.ardu-badge.com/ESP32-targz)

<p align="center">
<img src="ESP32-targz.png" alt="ES32-targz logo" width="512" />
</p>


## This library is a wrapper for the following two great libraries:

  - uzlib https://github.com/pfalcon/uzlib
  - TinyUntar https://github.com/dsoprea/TinyUntar

This library enables the channeling of gz :arrow_right: tar :arrow_right: filesystem data ~~without using an intermediate file~~ (bug: see [#4](https://github.com/tobozo/ESP32-targz/issues/4)).

In order to reach this goal, TinyUntar was heavily modified to allow data streaming, however uzlib is used *as is*.

:warning: uzlib will eat ~36KB of sram when used, and try to free them afterwards.
TinyUntar requires 512bytes only so its memory footprint is negligible.


Scope
-----

  - This library is only for unpacking / decompressing, no compression support is provided whatsoever
  - Although the examples use SPIFFS as default, it should work with any fs::FS filesystem (SD, SD_MMC, SPIFFS, FFat, LittleFS)
  - This is experimental, expect bugs!
  - Contributions and feedback are more than welcome :-)


Usage
-----

```C
    // Set **destination** filesystem by uncommenting one of these:
    #define DEST_FS_USES_SPIFFS
    //#define DEST_FS_USES_FFAT
    //#define DEST_FS_USES_SD
    //#define DEST_FS_USES_SD_MMC
    //#define DEST_FS_USES_LITTLEFS
    #include <ESP32-targz.h>
    // filesystem object will be available as "tarGzFs"
```



Extract content from `.gz` file
-------------------------------

```C

    // mount spiffs (or any other filesystem)
    tarGzFs.begin();

    // attach FS callbacks to prevent the partition from exploding during decompression
    setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn );

    // expand one file
    if( ! gzExpander(tarGzFs, "/index_html.gz", tarGzFs, "/index.html") ) {
      Serial.printf("operation failed with return code #%d", tarGzGetError() );
    }

    // expand another file
    if( ! gzExpander(tarGzFs, "/tbz.gz", tarGzFs, "/tbz.jpg") ) {
      Serial.printf("operation failed with return code #%d", tarGzGetError() );
    }


```


Expand contents from `.tar` file to `/tmp` folder
-------------------------------------------------

```C

    // mount spiffs (or any other filesystem)
    tarGzFs.begin();

    // attach FS callbacks to prevent the partition from exploding during decompression
    setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn );

    if( ! tarExpander(tarGzFs, "/tobozo.tar", tarGzFs, "/tmp") ) {
      Serial.printf("operation failed with return code #%d", tarGzGetError() );
    }


```



Expand contents from `.tar.gz`  to `/tmp` folder
------------------------------------------------

```C

    // mount spiffs (or any other filesystem)
    tarGzFs.begin();

    // attach FS callbacks to prevent the partition from exploding during decompression
    setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn );

    // using an intermediate file (default is /tmp/tmp.tar)
    if( ! tarGzExpander(tarGzFs, "/tbz.tar.gz", tarGzFs, "/tmp") ) {
      Serial.printf("operation failed with return code #%d", tarGzGetError() );
    }

    // or without intermediate file
    if( ! tarGzExpander(tarGzFs, "/tbz.tar.gz", tarGzFs, "/tmp", nullptr ) ) {
      Serial.printf("operation failed with return code #%d", tarGzGetError() );
    }


```


Flash the ESP with contents from `.gz` file
-------------------------------------------

```C

    // mount spiffs (or any other filesystem)
    tarGzFs.begin();

    if( ! gzUpdater(tarGzFs, "/menu_bin.gz") ) {
      Serial.printf("operation failed with return code #%d", tarGzGetError() );
    }


```


ESP32 Only: Flash the ESP with contents from `.gz` stream (HTTP or Filesystem)
------------------------------------------------------------------------------

```C

    // mount spiffs (or any other filesystem)
    tarGzFS.begin();
    fs::File file = tarGzFS.open( "/example_firmware.gz", "r" );
    size_t streamsize = file.size();
    if (!file) {
      Serial.println("Can't open file");
      return;
    }
    if( !gzStreamUpdater( (Stream *)&file, streamsize ) ) {
      Serial.printf("gzHTTPUpdater failed with return code #%d", tarGzGetError() );
    } else {
      Serial.println("Yay!"); // however the ESP has restarted so this code is unreachable
    }


```


ESP32 Only: Direct expansion (no intermediate file) from .tar.gz.` stream (HTTP or Filesystem)
----------------------------------------------------------------------------------------------
```C

    // mount spiffs (or any other filesystem)
    tarGzFS.begin();
    fs::File file = tarGzFS.open( "/example_archive.tgz", "r" );
    if (!file) {
      Serial.println("Can't open file");
      return;
    }
    if( !tarGzStreamExpander( streamptr, tarGzFS ) ) {
      Serial.printf("tarGzStreamExpander failed with return code #%d", tarGzGetError() );
    } else {
      Serial.println("Yay!");
    }

```



Callbacks
---------

```C

    #define DEST_FS_USES_SPIFFS
    //#define DEST_FS_USES_FFAT
    //#define DEST_FS_USES_SD
    //#define DEST_FS_USES_SD_MMC
    //#define DEST_FS_USES_LITTLEFS
    #include <ESP32-targz.h>

    // Progress callback for gzip, leave empty for less console output
    void myGzProgressCallback( uint8_t progress )
    {
      Serial.printf("Progress: %d\n", progress );
    }

    // General Error/Warning/Info logger, leave empty for less console output
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

    // progress callback for TAR (fired for each individual file)
    void myTarProgressCallback( uint8_t progress )
    {
      static int8_t myLastProgress = -1;
      if( myLastProgress != progress ) {
        myLastProgress = progress;
        if( progress == 0 ) {
          Serial.print("Progress: [0% ");
        } else if( progress == 100 ) {
          Serial.println(" 100%]\n");
        } else {
          switch( progress ) {
            case 25: Serial.print(" 25% ");break;
            case 50: Serial.print(" 50% ");break;
            case 75: Serial.print(" 75% ");break;
            default: Serial.print("T"); break;
          }
        }
      }
    }


    void setup()
    {
      // (...)

      tarGzFs.begin(true);

      // attach progress/log callbacks
      setProgressCallback( myGzProgressCallback );
      setLoggerCallback( myLogger );
      setTarMessageCallback( myLogger );

      setTarProgressCallback( myTarProgressCallback ); // prints the untarring progress for each individual file
      setTarStatusProgressCallback( myTarStatusProgressCallback ); // print the filenames as they're expanded

      // .... or attach empty callbacks to silent the output (zombie mode)
      // setProgressCallback( targzNullProgressCallback );
      // setLoggerCallback( targzNullLoggerCallback );
      // setTarMessageCallback( targzNullLoggerCallback );

      // optional but recommended, to check for available space before writing
      setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn );

      // (...)

      if( tarGzExpander(tarGzFs, "/blah.tar.gz", tarGzFs, "/tmp", nullptr ) ) {
        Serial.println("Yay!");
      } else {
        Serial.printf("tarGzExpander failed with return code #%d\n", tarGzGetError() );
      }

    }


```

Return Codes
------------

`tarGzGetError()` returns a value when a problem occured:

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

  - UZLIB: forwarding error values from uzlib.h as is (no offset)

    - `-2`   : Not a valid gzip file
    - `-3`   : Gz Error TINF_DATA_ERROR
    - `-4`   : Gz Error TINF_CHKSUM_ERROR
    - `-5`   : Gz Error TINF_DICT_ERROR

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


Known bugs
----------


  - tarGzExpander/tarExpander: some formats aren't supported with SPIFFS (e.g contains symlinks or long filename/path)
  - tarGzExpander/gzExpander on ESP8266 : while the provided examples will work, the 32Kb dynamic allocation for gzip dictionary is unlikely to work in real world scenarios (e.g. with a webserver) and would probably require static allocation
  ~~- tarGzExpander: files smaller than 4K aren't processed~~
  - ~~error detection isn't deferred efficiently, debugging may be painful~~
  - ~~.tar files containing files smaller than 512 bytes aren't fully processed~~
  - ~~reading/writing simultaneously on SPIFFS may induce errors~~

Resources
-----------
  - [ESP32 Sketch Data Upload tool for FFat/LittleFS/SPIFFS/](https://github.com/lorol/arduino-esp32fs-plugin/releases)

  ![image](https://user-images.githubusercontent.com/1893754/99714053-635de380-2aa5-11eb-98e3-631a94836742.png)

  - [LittleFS for ESP32](https://github.com/lorol/LITTLEFS)


Credits:
--------

  - [pfalcon](https://github.com/pfalcon/uzlib) (uzlib maintainer)
  - [dsoprea](https://github.com/dsoprea/TinyUntar) (TinyUntar maintainer)
  - [lorol](https://github.com/lorol) (LittleFS-ESP32 + fs plugin)
  - [me-no-dev](https://github.com/me-no-dev) (inspiration and support)
  - [atanisoft](https://github.com/atanisoft) (motivation and support)
  - [lbernstone](https://github.com/lbernstone) (motivation and support)
  - [scubachristopher](https://github.com/scubachristopher) (contribution and support)


