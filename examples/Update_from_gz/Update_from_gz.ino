#include <SPIFFS.h>
#include <ESP32-targz.h>

#define BUTTON_PIN 32

void setup() {

  Serial.begin( 115200 );

  pinMode( BUTTON_PIN, INPUT_PULLUP );

}

void loop() {

  if( digitalRead( BUTTON_PIN ) == LOW ) {

    SPIFFS.begin(true);

    // flash the ESP with gz's contents (gzip the bin yourself and use the spiffs uploader)
    gzUpdater(SPIFFS, "/menu_bin.gz");

  }

}
