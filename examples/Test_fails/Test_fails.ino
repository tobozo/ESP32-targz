/*\
 *
 * Test_tar_gz_tgz.ino
 * Test suite for ESP32-targz
 * https://github.com/tobozo/ESP32-targz
 *
\*/
// Set **destination** filesystem by uncommenting one of these:
//#define DEST_FS_USES_SPIFFS // WARN: SPIFFS is full of bugs
#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only
//#define DEST_FS_USES_PSRAMFS // ESP32 only
#include <ESP32-targz.h>

// #define sourceFS tarGzFS // assume source = destination unless stated otherwise in the test function

#include <PSRamFS.h>
#define sourceFS PSRamFS

#include "data_16_files_of_1100_bytes_in_40kb_of_tar.h"

void setup()
{
  Serial.begin( 115200 );
  delay(1000);
  Serial.println("Hello fail test");

  Serial.println("Formatting LittleFS...");
  if (LittleFS.format()) {
    Serial.println("LittleFS formatted successfully.");
  } else {
    Serial.println("LittleFS formatting failed.");
  }

  if (!LittleFS.begin()) {
    Serial.println("LittleFS Filesystem mount failed, halting");
    while(1);
  }

  if( targzTotalBytesFn() != 65536 ) {
    Serial.printf("[ERROR] LittleFS partition size should be 65536, got %d total bytes (%d free), halting\n", targzTotalBytesFn(), targzFreeBytesFn());
    //while(1);
  }

  Serial.printf("LittleFS mounted, %d total bytes, %d free bytes\n", targzTotalBytesFn(), targzFreeBytesFn());

  if( !PSRamFS.begin() ) {
    Serial.println("PSRamFS Filesystem mount failed, halting");
    while(1);
  }

  Serial.printf("PSRamFS mounted: %d total bytes\n", PSRamFS.totalBytes());

  // Copy TGZ sample data from PROGMEM to RAMDisk
  const char* tar_source_file = "/16_files_of_1100_bytes.tar";
  // File samplecopy = PSRamFS.open(tar_source_file, "w");
  // samplecopy.write(tgz_data, tgz_data_len);
  // samplecopy.close();

  if(LittleFS.exists(tar_source_file))
    LittleFS.remove(tar_source_file);

  if( PSRamFS.createFile(tar_source_file, tgz_data, tgz_data_len) ) {
    Serial.println("YAY");
  } else {
    Serial.println("NAY");
  }

  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();
  TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn );
  TARGZUnpacker->setTarVerify( true );

  if( !TARGZUnpacker->tarGzExpander(PSRamFS, tar_source_file, LittleFS, "/out", nullptr ) ) {
    Serial.println();
    Serial.printf("[SUCCESS] test failed successfully with return code #%d\n", TARGZUnpacker->tarGzGetError() );
    Serial.println();
  } else {
    Serial.println("[ERROR] test unexpectedly succeeded");
  }

  BaseUnpacker *Unpacker = new BaseUnpacker();
  Unpacker->tarGzListDir( LittleFS, "/", 3 );

}

void loop()
{
}
