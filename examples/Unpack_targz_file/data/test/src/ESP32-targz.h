#ifndef _TGZ_FSFOOLS_
#define _TGZ_FSFOOLS_

// Figure out the fs::FS library to load for the **destination** filesystem

#if defined ESP32

  #if defined DEST_FS_USES_SPIFFS
    #include <SPIFFS.h>
    #define tarGzFS SPIFFS
  #elif defined DEST_FS_USES_FFAT
    #include <FFat.h>
    #define tarGzFS FFat
  #elif defined DEST_FS_USES_SD
    #include <SD.h>
    #define tarGzFS SD
  #elif defined DEST_FS_USES_SD_MMC
    #include <SD_MMC.h>
    #define tarGzFS SD_MMC
  #elif defined DEST_FS_USES_LITTLEFS
    #include <LITTLEFS.h>
    #define tarGzFS LITTLEFS
  #else
    #warning "Unspecified filesystem, please #define one of these before including the library: DEST_FS_USES_SPIFFS, DEST_FS_USES_FFAT, DEST_FS_USES_SD, DEST_FS_USES_SD_MMC, DEST_FS_USES_LITTLEFS"
    #warning "Defaulting to SPIFFS"
    #define DEST_FS_USES_SPIFFS
    #include <SPIFFS.h>
    #define tarGzFS SPIFFS
  #endif

#elif defined ESP8266

  // ESP8266 has no SD_MMC or FFat.h library, so these are implicitely invalidated
  #undef DEST_FS_USES_SD_MMC // unsupported
  #undef DEST_FS_USES_FFAT   // unsupported

  #if defined DEST_FS_USES_SD
    #include <SDFS.h>
    #define tarGzFS SDFS
  #else
    #if defined DEST_FS_USES_LITTLEFS
      #include <LittleFS.h>
      #define tarGzFS LittleFS
    #elif defined DEST_FS_USES_SPIFFS
      #if defined USE_LittleFS // emulate SPIFFS using LittleFS
        #include <LittleFS.h>
        #define tarGzFS SPIFFS
      #else // use core SPIFFS
        #include <FS.h>
        #define tarGzFS SPIFFS
      #endif
    #else // no destination filesystem defined in sketch
      #warning "Unspecified filesystem, please #define one of these before including the library: DEST_FS_USES_SPIFFS, DEST_FS_USES_LITTLEFS, DEST_FS_USES_SD"
      // however, check for USE_LittleFS as it is commonly defined since SPIFFS deprecation
      #if defined USE_LittleFS
        #include <LittleFS.h>
        #define tarGzFS LittleFS
        #warning "Defaulting to LittleFS"
        #define DEST_FS_USES_LITTLEFS
      #else
        #include <FS.h>
        #define tarGzFS SPIFFS
        #warning "Defaulting to SPIFFS (soon deprecated)"
        #define DEST_FS_USES_SPIFFS
      #endif
    #endif
  #endif

  FSInfo fsinfo;

#else

  #error "Only ESP32 and ESP8266 architectures are supported"

#endif


// required filesystem helpers are declared outside the main library
// because ESP32/ESP8266 <FS.h> use different abstraction flavours :)
size_t targzFreeBytesFn() {
  #if defined DEST_FS_USES_SPIFFS || defined DEST_FS_USES_SD || defined DEST_FS_USES_SD_MMC || defined DEST_FS_USES_LITTLEFS
    #if defined ESP32
      return tarGzFS.totalBytes() - tarGzFS.usedBytes();
    #elif defined ESP8266
      if( tarGzFS.info( fsinfo ) ) {
        return fsinfo.totalBytes - fsinfo.usedBytes;
      } else {
        // fail
        return 0;
      }
    #else
      #error "Only ESP32 and ESP8266 are supported"
    #endif
  #elif defined DEST_FS_USES_FFAT
    return tarGzFS.freeBytes();
  #else
    #error "No filesystem is declared"
  #endif
}
size_t targzTotalBytesFn() {
  #if defined DEST_FS_USES_SPIFFS || defined DEST_FS_USES_SD || defined DEST_FS_USES_SD_MMC || defined DEST_FS_USES_LITTLEFS || defined DEST_FS_USES_FFAT
    #if defined ESP32
      return tarGzFS.totalBytes();
    #elif defined ESP8266
      if( tarGzFS.info( fsinfo ) ) {
        return fsinfo.totalBytes;
      } else {
        // fail
        return 0;
      }
    #else
      #error "Only ESP32 and ESP8266 are supported"
    #endif
  #else
    #error "No filesystem is declared"
  #endif
}

#include <ESP32-targz-lib.h>

#endif