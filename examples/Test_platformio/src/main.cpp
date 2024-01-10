#define DEST_FS_USES_SD

#include <ESP32-targz.h>

void setup()
{
  if( !tarGzFS.begin() ) {
    Serial.println("Could not start filesystem");
    while(1) yield();
  }

  TarUnpacker *TARUnpacker     = new TarUnpacker();
  GzUnpacker *GZUnpacker       = new GzUnpacker();
  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

}


void loop()
{

}
