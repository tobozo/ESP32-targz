#include <SPIFFS.h>
#include <ESP32-targz.h>

void setup() {

  Serial.begin( 115200 );

  SPIFFS.begin(true);

  // direct .tar.gz to file expanding is broken so do this separately
  // tarGzExpander(SPIFFS, "/tbz.tar.gz", SPIFFS, "/tmp");

  gzExpander(SPIFFS, "/tbz.tar.gz", SPIFFS, "/tmp/data.tar");
  tarExpander(SPIFFS, "/tmp/data.tar", SPIFFS, "/tmp");
  SPIFFS.remove("/tmp/data.tar");

  tarGzListDir( SPIFFS, "/tmp");

}

void loop() {

}
