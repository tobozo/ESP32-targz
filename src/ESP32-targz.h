#ifndef _TGZ_FSFOOLS_
#define _TGZ_FSFOOLS_

#if defined DEST_FS_USES_SPIFFS
  #include <SPIFFS.h>
  #define tarGzFS SPIFFS
  #define beginBool false
#elif defined DEST_FS_USES_FFAT
  #include <FFat.h>
  #define tarGzFS FFat
  #define beginBool false
#elif defined DEST_FS_USES_SD
  #include <SD.h>
  #define tarGzFS SD
  #define beginBool
#elif defined DEST_FS_USES_SD_MMC
  #include <SD_MMC.h>
  #define tarGzFS SD_MMC
  #define beginBool
#elif defined DEST_FS_USES_LITTLEFS
  #include <LITTLEFS.h>
  #define tarGzFS LITTLEFS
  #define beginBool false
#else
  #warning "Undeclared filesystem, please #define one of these before including the library DEST_FS_USES_SPIFFS, DEST_FS_USES_FFAT, DEST_FS_USES_SD, DEST_FS_USES_SD_MMC, DEST_FS_USES_LITTLEFS"
  #warning "Defaulting to SPIFFS"
  #define DEST_FS_USES_SPIFFS
  #include <SPIFFS.h>
  #define tarGzFS SPIFFS
  #define beginBool false
#endif

// required filesystem helpers, declared outside the main class
// because <FS.h> does not handle that consistently (yet)
size_t targzFreeBytesFn() {
  #if defined DEST_FS_USES_SPIFFS || defined DEST_FS_USES_SD || defined DEST_FS_USES_SD_MMC || defined DEST_FS_USES_LITTLEFS
    return tarGzFS.totalBytes() - tarGzFS.usedBytes();
  #elif defined DEST_FS_USES_FFAT
    return tarGzFS.freeBytes();
  #else
    #error "No filesystem is declared"
  #endif
}
size_t targzTotalBytesFn() {
  #if defined DEST_FS_USES_SPIFFS || defined DEST_FS_USES_SD || defined DEST_FS_USES_SD_MMC || defined DEST_FS_USES_LITTLEFS || defined DEST_FS_USES_FFAT
    return tarGzFS.totalBytes();
  #else
    #error "No filesystem is declared"
  #endif
}

#include <ESP32-targz-lib.h>

#endif
