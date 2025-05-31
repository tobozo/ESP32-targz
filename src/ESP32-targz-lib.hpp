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
  #pragma GCC diagnostic ignored "-Wunused-parameter"
  #pragma GCC diagnostic ignored "-Wformat"

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
    #else
      // no destination filesystem defined in sketch
    #endif
  #endif

  [[maybe_unused]] static FSInfo fsinfo;

#elif defined ARDUINO_ARCH_RP2040

  // #pragma message "Experimental RP2040 support"

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

  [[maybe_unused]] static FSInfo fsinfo;

#else

  #error "Only ESP32, ESP8266 and RP2040/Pico architectures are supported"

#endif


#if defined DEST_FS_USES_SPIFFS || defined DEST_FS_USES_LITTLEFS || defined DEST_FS_USES_FFAT
  #define WARN_LIMITED_FS
#endif

#include <stddef.h> // platformio whines about missing definition for 'size_t' 🤦

// required filesystem helpers are declared outside the main library
// because ESP32/ESP8266/RP2040 versions of <FS.h> use different abstraction flavours :)
__attribute__((unused)) static uint64_t targzFreeBytesFn() {
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

__attribute__((unused)) static uint64_t targzTotalBytesFn() {
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


#include "types/esp32_targz_types.h"




// md5sum (essentially for debug)
#include "helpers/md5_sum.h"
// helpers: mkdir, mkpath, dirname
#include "helpers/path_tools.h"

#if !defined ESP32_TARGZ_DISABLE_DECOMPRESSION
  #include "libunpacker/LibUnpacker.hpp"
#else
  #pragma message "Decompression support is disabled"
#endif

#if !defined ESP32_TARGZ_DISABLE_COMPRESSION
  #include <vector>
  #include "libpacker/LibPacker.hpp"
  #if defined ARDUINO_ARCH_RP2040 && defined TARGZ_USE_TASKS
    #warning "FreeRTOS Tasks are enabled, this may interfer with TinyUSB"
  #endif

#else
  #pragma message "Compression support is disabled"
#endif


//#endif // #ifdef _ESP_TGZ_H
