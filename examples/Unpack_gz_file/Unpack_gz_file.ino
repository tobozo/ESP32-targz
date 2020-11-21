/*\
 *
 * Unpack_gz_file.ino
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


void setup() {

  Serial.begin( 115200 );
  Serial.printf("Initializing Filesystem with free heap: %d\n", ESP.getFreeHeap() );

  if (!tarGzFS.begin()) {
    Serial.println("Filesystem Mount Failed");
    while(1);
  } else {
    Serial.println("Filesystem Mount Successful");
  }

  // attach FS callbacks to prevent the partition from exploding during decompression
  setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn );
  // attach empty callbacks to silent the output (zombie mode)
  // setProgressCallback( targzNullProgressCallback );
  // setLoggerCallback( targzNullLoggerCallback );

  // extract content from gz file
  if( !gzExpander(tarGzFS, "/index_html.gz", tarGzFS, "/index.html") ) {
    Serial.printf("gzExpander failed with return code #%d\n", tarGzGetError() );
  }

  if( ! gzExpander(tarGzFS, "/tbz.gz", tarGzFS, "/tbz.jpg") ) {
    Serial.printf("gzExpander failed with return code #%d\n", tarGzGetError() );
  }

  tarGzListDir( tarGzFS, "/", 3 );

}


void loop() {

}
