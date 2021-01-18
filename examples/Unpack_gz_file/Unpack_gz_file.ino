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

  GzUnpacker *GZUnpacker = new GzUnpacker();
  GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  if( !GZUnpacker->gzExpander(tarGzFS, "/gz_example.gz", tarGzFS, "/tbz.jpg") ) {
    Serial.printf("gzExpander failed with return code #%d", GZUnpacker->tarGzGetError() );
  }

  GZUnpacker->tarGzListDir( tarGzFS, "/", 3 );

}


void loop() {

}
