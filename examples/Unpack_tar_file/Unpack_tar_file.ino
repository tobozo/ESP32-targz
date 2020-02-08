#include <ESP32-targz.h>

void setup() {

  Serial.begin( 115200 );

  SPIFFS.begin(true);

  // expand tar contents to /tmp folder
  tarExpander(SPIFFS, "/tobozo.tar", SPIFFS, "/");

  tarGzListDir( SPIFFS, "/");

}

void loop() {

}
