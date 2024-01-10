#pragma once

#ifndef _TGZ_FSFOOLS_
#define _TGZ_FSFOOLS_
#endif

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
        #include <FS.h>
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
        #include <FS.h>
        #define tarGzFS SPIFFS
        #warning "Defaulting to SPIFFS (soon deprecated)"
        #define DEST_FS_USES_SPIFFS
        #define FS_NAME "SPIFFS"
      #endif
    #endif
  #endif

  FSInfo fsinfo;


#elif defined ARDUINO_ARCH_RP2040

  #pragma message "Experimental RP2040 support"
  #include "ESP32-targz-log.hpp"

  #undef DEST_FS_USES_SD_MMC // unsupported
  #undef DEST_FS_USES_FFAT   // unsupported
  #undef DEST_FS_USES_SPIFFS // unsupported

  // Figure out the chosen fs::FS library to load for the **destination** filesystem
  #if defined DEST_FS_USES_SD
    #include <SD.h>
    #define tarGzFS SDFS
    #define FS_NAME "SD"
  #elif defined DEST_FS_USES_LITTLEFS
    //#include <FS.h>
    #include <LittleFS.h>
    #define tarGzFS LittleFS
    #define FS_NAME "LITTLEFS (picolib)"
  #else
    #error "Unspecified or invalid destination filesystem, please #define one of these before including the library: DEST_FS_USES_LITTLEFS, DEST_FS_USES_SD"
  #endif

  FSInfo fsinfo;

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

#include "ESP32-targz-lib.hpp"
