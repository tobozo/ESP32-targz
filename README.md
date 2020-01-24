# ESP32-targz

This library is a wrapper for the following two great libraries:

  - uzlib https://github.com/pfalcon/uzlib
  - TinyUntar https://github.com/dsoprea/TinyUntar

Decompression support for .tar and .gz files

This is a work in progress


```C
#include <ESP32-targz.h>

void setup() {
  SPIFFS.begin(true);
  uzFileExpander(SPIFFS, "/menu.tar.gz", SPIFFS, "/menu.tar");
  untarFile(SPIFFS, "/menu.tar");
}

void loop() {

}

```
