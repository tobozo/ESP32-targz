#include <SPIFFS.h>
#include <ESP32-targz.h>

// progress callback, leave empty for less console output
void myNullProgressCallback( uint8_t progress ) {
  // printf("Progress: %d", progress );
}
// error/warning/info logger, leave empty for less console output
void myNullLogger(const char* format, ...) {
  //va_list args;
  //va_start(args, format);
  //vprintf(format, args);
  //va_end(args);
}

// 1) Find your firmware and gzip it:
//     gzip -c /tmp/arduino/firmware.bin > /tmp/firmware.gz
//
// 2) Copy the gz file in the /data folder and upload it using Arduino sketch data uploader for SPIFFS
//
// 3) Edit the value of "firmwareFile" to match the gz file name:
//
const char* firmwareFile = "/M5Rotatey_Cube.gz";

void setup() {
  Serial.begin( 115200 );
  Serial.printf("Initializing SPIFFS...\n");

  if (!SPIFFS.begin(false)) {
     Serial.printf("SPIFFS Mount Failed\n");
  }
  else {
    Serial.printf("SPIFFS Mount Successful\n");
  }
}


void loop() {
  bool done = false;

  while (!done) {
    delay(1000);
    if (SPIFFS.exists( firmwareFile )) { // <<< gzipped firmware update
      Serial.printf("Found file, about to update...\n");
      delay(1000);
      //setProgressCallback( myNullProgressCallback );
      //setLoggerCallback( myNullLogger );
      // flash the ESP with gz's contents (gzip the bin yourself and use the spiffs uploader)
      if( ! gzUpdater(SPIFFS, firmwareFile ) ) {
        Serial.printf("gzUpdater failed with return code #%d", tarGzGetError() );
      }
      done = true;
    }
    else {
      Serial.printf("firmware not found\n");
      break;
    }
  }
}
