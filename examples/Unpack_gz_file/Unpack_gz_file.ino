#include <SPIFFS.h>
#include <ESP32-targz.h>

void setup() {

  Serial.begin( 115200 );

  SPIFFS.begin( true );

  // extract content from gz file
  gzExpander(SPIFFS, "/index_html.gz", SPIFFS, "/index.html");

  gzExpander(SPIFFS, "/tbz.gz", SPIFFS, "/tbz.jpg");

  tarGzListDir( SPIFFS, "/");

}

void loop() {

}
