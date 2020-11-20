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

// small partitions crash-test !!
// contains 1 big file that will make the extraction fail on partitions <= 1.5MB
const char *fileTooBigForSPIFFS = "/zombocrash.tar.gz";

// regular test, should work even after a crash-test
// same archive without the big file
const char *fileJustBigEnoughForSPIFFS = "/zombocom.tar.gz";

void setup() {

  Serial.begin( 115200 );
  Serial.println("Initializing Filesystem...");

  if (!tarGzFS.begin()) {
    Serial.println("Filesystem Mount Failed :(");
  } else {
    Serial.println("Filesystem Mount Successful :)");

    // attach FS callbacks to prevent the partition from exploding during decompression
    setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn );
    // attach empty callbacks to silent the output (zombie mode)
    // setProgressCallback( targzNullProgressCallback );
    // setLoggerCallback( targzNullLoggerCallback );

    if( tarGzExpander(tarGzFS, fileJustBigEnoughForSPIFFS, tarGzFS, "/tmp") ) {
      Serial.println("Yay!");
      Serial.println("Filesystem contents:");
      tarGzListDir( tarGzFS, "/", 3 );
    } else {
      Serial.printf("tarGzExpander failed with return code #%d\n", tarGzGetError() );
    }
  }
}


void loop() {

}
