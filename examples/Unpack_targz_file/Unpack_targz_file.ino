/*\
 *
 * Unpack_targz_file.ino
 * Example code for ESP32-targz
 * https://github.com/tobozo/ESP32-targz
 *
\*/

// Set **destination** filesystem by uncommenting one of these:
#define DEST_FS_USES_SPIFFS
//#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only
#include <ESP32-targz.h>
//#include <ESP8266WiFi.h>
//#include <ESP8266HTTPClient.h>

// small partitions crash-test !!
// contains 1 big file that will make the extraction fail on partitions <= 1.5MB
// const char *fileTooBigForSPIFFS = "/zombocrash.tar.gz";

// regular test, should work even after a crash-test
// same archive without the big file
const char *fileJustBigEnoughForSPIFFS = "/zombocom.tar.gz";

void setup() {

  Serial.begin( 115200 );


  delay(1000);
  Serial.printf("Initializing Filesystem with free heap: %d\n", ESP.getFreeHeap() );

  if (!tarGzFS.begin()) {
    Serial.println("Filesystem Mount Failed :(");
  } else {
    Serial.println("Source filesystem Mount Successful :)");

    // attach FS callbacks to prevent the partition from exploding during decompression
    setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn );
    // attach empty callbacks to silent the output (zombie mode)
    // setProgressCallback( targzNullProgressCallback );
    // setLoggerCallback( targzNullLoggerCallback );

    // targz files can be extracted with or without an intermediate file

    Serial.println("Decompressing using an intermediate file: this is slower and requires more filesystem space, but uses less memory");
    if( tarGzExpander(tarGzFS, fileJustBigEnoughForSPIFFS, tarGzFS, "/tmp") ) {
      Serial.println("Yay!");
    } else {
      Serial.printf("tarGzExpander failed with return code #%d\n", tarGzGetError() );
    }

    Serial.printf("Free heap after operation: %d\n", ESP.getFreeHeap() );

    #ifdef ESP32
    // ESP32 has enough ram to uncompress without intermediate file
    Serial.println("Decompressing using no intermediate file is faster and generates much less i/o, but consumes more memory (~32kb)");
    if( tarGzExpander(tarGzFS, fileJustBigEnoughForSPIFFS, tarGzFS, "/tmp", nullptr ) ) {
      Serial.println("Yay!");
    } else {
      Serial.printf("tarGzExpander failed with return code #%d\n", tarGzGetError() );
    }

    Serial.printf("Free heap after operation: %d\n", ESP.getFreeHeap() );
    #endif


  }

  tarGzListDir( tarGzFS, "/", 3 );

}


void loop() {

}
