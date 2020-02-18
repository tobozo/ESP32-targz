# ESP32-targz

## An ESP32/ESP8266 Arduino library to provide decompression support for .tar, .gz and .tar.gz files

[![arduino-library-badge](https://www.ardu-badge.com/badge/ESP32-targz.svg?)](https://www.ardu-badge.com/ESP32-targz)

<p align="center">
<img src="ESP32-targz.png" alt="ES32-targz logo" width="512" />
</p>


## This library is a wrapper for the following two great libraries:

  - uzlib https://github.com/pfalcon/uzlib
  - TinyUntar https://github.com/dsoprea/TinyUntar

This library enables the channeling of gz :arrow_right: tar :arrow_right: filesystem data without using an intermediate file.

In order to reach this goal, TinyUntar was heavily modified to allow data streaming, however uzlib is used *as is*.

:warning: uzlib will eat ~36KB of sram when used, and try to free them afterwards.
TinyUntar requires 512bytes only so its memory footprint is negligible.


Scope
-----

  - This library is only for unpacking / decompressing, no compression support is provided whatsoever
  - Although the examples use SPIFFS, it should work with any fs::FS filesystem (SD, SD_MMC, SPIFFS)
  - This is experimental, expect bugs!
  - Contributions and feedback are more than welcome :-)


Usage
-----

```C
    #include <ESP32-targz.h>
```



Extract content from `.gz` file
-------------------------------

```C

  // mount spiffs (or any other filesystem)
  SPIFFS.begin(true);

  // expand one file
  gzExpander(SPIFFS, "/index_html.gz", SPIFFS, "/index.html");

  // expand another file
  gzExpander(SPIFFS, "/tbz.gz", SPIFFS, "/tbz.jpg");


```


Expand contents from `.tar` file to `/tmp` folder
-------------------------------------------------

```C

  // mount spiffs (or any other filesystem)
  SPIFFS.begin(true);

  tarExpander(SPIFFS, "/tobozo.tar", SPIFFS, "/tmp");


```



Expand contents from `.tar.gz`  to `/tmp` folder
------------------------------------------------

```C

  // mount spiffs (or any other filesystem)
  SPIFFS.begin(true);

  tarGzExpander(SPIFFS, "/tbz.tar.gz", SPIFFS, "/tmp");

```


Flash the ESP with contents from `.gz` file
-------------------------------------------

```C
  // mount spiffs (or any other filesystem)
    SPIFFS.begin(true);

  gzUpdater(SPIFFS, "/menu_bin.gz");


```

Known bugs
----------

  - tarGzExpander: files smaller than 4K aren't processed
  - tarGzExpander/tarExpander: some formats aren't supported (e.g contains symlinks or long filename/path)
  - tarGzExpander/gzExpander on ESP8266 : while the provided examples will work, the 32Kb dynamic allocation for gzip dictionary is unlikely to work in real world scenarios (e.g. with a webserver) and would probably require static allocation
  - ~~error detection isn't deferred efficiently, debugging may be painful~~
  - ~~.tar files containing files smaller than 512 bytes aren't fully processed~~
  - ~~reading/writing simultaneously on SPIFFS may induce errors~~


Credits:
--------

  - [pfalcon](https://github.com/pfalcon/uzlib) (uzlib maintainer)
  - [dsoprea](https://github.com/dsoprea/TinyUntar) (TinyUntar maintainer)
  - [me-no-dev](https://github.com/me-no-dev) (inspiration and support)
  - [atanisoft](https://github.com/atanisoft) (motivation and support)
  

