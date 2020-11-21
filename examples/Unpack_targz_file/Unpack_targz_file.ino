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
const char *fileTooBigForSPIFFS = "/zombocrash.tar.gz";

// regular test, should work even after a crash-test
// same archive without the big file
const char *fileJustBigEnoughForSPIFFS = "/zombocom.tar.gz";

void setup() {

  Serial.begin( 115200 );


  delay(1000);
  Serial.printf("Initializing Filesystem with free heap: %d\n", ESP.getFreeHeap() );

/*
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0) {
    Serial.println("no networks found");
  } else {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("");

  Serial.printf("Free heap after wifi scan: %d\n", ESP.getFreeHeap() );
*/

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

    Serial.printf("Free heap after operation: %d\n", ESP.getFreeHeap() );

  }

}


void loop() {

}
