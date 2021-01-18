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
// const char *tarGzFile = "/zombocrash.tar.gz";

// regular test, should work even after a crash-test
// same archive without the big file
const char *tarGzFile = "/zombocom.tar.gz";

void setup() {

  Serial.begin( 115200 );

  delay(1000);
  Serial.printf("Initializing Filesystem with free heap: %d\n", ESP.getFreeHeap() );

  if (!tarGzFS.begin()) {
    Serial.println("Filesystem Mount Failed :(");
  } else {

    Serial.println("Source filesystem Mount Successful :)");

    TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();
    TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
    TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
    TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
    TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
    TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
    TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
    TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
    TARGZUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity

    if( !TARGZUnpacker->tarGzExpander(tarGzFS, tarGzFile, tarGzFS, "/tmp") ) {
      Serial.printf("tarGzExpander+intermediate file failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
    }

    if( !TARGZUnpacker->tarGzExpander(tarGzFS, tarGzFile, tarGzFS, "/tmp", nullptr ) ) {
      Serial.printf("tarGzExpander+intermediate file failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
    }

    TARGZUnpacker->tarGzListDir( tarGzFS, "/", 3 );

  }

}


void loop() {

}
