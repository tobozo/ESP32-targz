#include <ESP32-targz.h>

void setup()
{
  Serial.begin(115200);

  TarUnpacker *TARUnpacker     = new TarUnpacker();
  GzUnpacker *GZUnpacker       = new GzUnpacker();
  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

}


void loop()
{

}
