# ESP32-targz

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
