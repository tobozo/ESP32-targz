/*\
 *
 * Update_from_gz_file.ino
 * Example code for ESP32-targz
 * https://github.com/tobozo/ESP32-targz
 *
\*/

// Set **destination** filesystem by uncommenting one of these:
#define DEST_FS_USES_SPIFFS
//#define DEST_FS_USES_FFAT
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_SD_MMC
//#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>

// 1) Find your firmware and gzip it:
//     gzip -c /tmp/arduino/firmware.bin > /tmp/firmware.gz
//
// 2) Copy the gz file in the /data folder and upload it using Arduino sketch data uploader for FFat
//
// 3) Edit the value of "firmwareFile" to match the gz file name:
//
const char* firmwareFile = "/M5Rotatey_Cube.gz";

void setup() {

  Serial.begin( 115200 );
  Serial.println("Initializing Filesystem...");

  if (!tarGzFS.begin()) {
    Serial.println("Filesystem Mount Failed");
    while(1);
  } else {
    Serial.println("Filesystem Mount Successful");
  }

}


void loop() {
  bool done = false;

  while (!done) {
    delay(1000);
    if (tarGzFS.exists( firmwareFile )) { // <<< gzipped firmware update
      Serial.printf("Found file, about to update...\n");
      delay(1000);
      // attach empty callbacks to silent the output (zombie mode)
      // setProgressCallback( targzNullProgressCallback );
      // setLoggerCallback( targzNullLoggerCallback );

      // flash the ESP with gz's contents (gzip the bin yourself and use the spiffs uploader)
      if( ! gzUpdater(tarGzFS, firmwareFile ) ) {
        Serial.printf("gzUpdater failed with return code #%d", tarGzGetError() );
      }
      done = true;
    } else {
      Serial.println("Firmware not found");
      while(1);
    }
  }
}
