#include <ESP32-targz.h>

void setup() {

  Serial.begin( 115200 );

  SPIFFS.begin(true);

  // direct .tar.gz to file expanding
  tarGzExpander(SPIFFS, "/tbz.tar.gz", SPIFFS, "/tmp");

  tarGzListDir( SPIFFS, "/tmp");

}

void loop() {

}
