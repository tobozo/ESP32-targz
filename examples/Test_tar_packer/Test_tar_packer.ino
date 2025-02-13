// Set **destination** filesystem by uncommenting one of these:
//#define DEST_FS_USES_SPIFFS
#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only

#include <ESP32-targz.h>

// Uncomment `USE_WEBSERVER` to download the generated files when using SPIFFS/LittleFS
// Comment out `USE_WEBSERVER` if you get a "sketch too big" compilation error
#if defined ESP32 || defined ESP8266
  #define USE_WEBSERVER
  #include "./network.h"
#elif defined ARDUINO_ARCH_RP2040
  #include <SingleFileDrive.h>
#endif

// because some espressif cores will error instead of just emitting a warning
#pragma GCC diagnostic ignored "-Wformat"

const char* src_path   = "/";            // source folder (no ending slash except if root)
const char* tar_path   = "/test.tar";    // output tar archive to create
const char* targz_path = "/test.tar.gz"; // output tar archive to create
const char* dst_path   = nullptr;        // optional virtual folder in the tar archive, no ending slash, set to NULL to put everything in the root

std::vector<TAR::dir_entity_t> dirEntities; // storage for scanned dir entities


void removeTempFiles()
{
  Serial.println("Cleaning up temporary files");
  // cleanup test from other examples to prevent SPIFFS from exploding :)
  if(tarGzFS.exists(tar_path))
    tarGzFS.remove(tar_path);
  if(tarGzFS.exists(targz_path))
    tarGzFS.remove(targz_path);
}


void testTarPacker()
{
  // tar_path: destination, .tar packed file
  // dst_path: optional path prefix in tar archive
  removeTempFiles(); // cleanup previous examples
  Serial.println();
  Serial.printf("TarPacker::pack_files(&tarGzFS, dirEntities, &tarGzFS, tar_path, dst_path); Free heap: %lu bytes\n", HEAP_AVAILABLE() );
  TarPacker::setProgressCallBack( LZPacker::defaultProgressCallback );
  auto ret = TarPacker::pack_files(&tarGzFS, dirEntities, &tarGzFS, tar_path, dst_path);
  Serial.printf("Wrote %d bytes to %s\n", ret, tar_path);
}

void testTarPackerStream()
{
  // tar_path: destination, .tar packed file
  // dst_path: optional path prefix in tar archive
  removeTempFiles(); // cleanup previous examples
  Serial.println();
  Serial.printf("TarPacker::pack_files(&tarGzFS, dirEntities, &tar, dst_path); Free heap: %lu bytes\n", HEAP_AVAILABLE() );
  TarPacker::setProgressCallBack( LZPacker::defaultProgressCallback );
  auto tar = tarGzFS.open(tar_path, "w");
  if(!tar)
    return;
  auto ret = TarPacker::pack_files(&tarGzFS, dirEntities, &tar, dst_path);
  tar.close();
  Serial.printf("Wrote %d bytes to %s\n", ret, tar_path);
}


void testTarGzPacker()
{
  removeTempFiles(); // cleanup previous examples
  // targz_path: name of the .tar.gz compressed file
  // dst_path: optional path prefix in tar archive
  Serial.println();
  Serial.printf("TarGzPacker::compress(&tarGzFS, dirEntities, &tarGzFS, targz_path, dst_path); Free heap: %lu bytes\n", HEAP_AVAILABLE() );
  auto ret = TarGzPacker::compress(&tarGzFS, dirEntities, &tarGzFS, targz_path, dst_path);
  Serial.printf("Wrote %d bytes to %s\n", ret, targz_path);
}

bool testTarGzPackerStream()
{
  // targz_path: name of the .tar.gz compressed file
  // dst_path: optional path prefix in tar archive
  removeTempFiles(); // cleanup previous examples
  Serial.println();
  Serial.printf("TarGzPacker::compress(&tarGzFS, dirEntities, &gz, dst_path); Free heap: %lu bytes\n", HEAP_AVAILABLE() );
  auto gz = tarGzFS.open(targz_path, "w");
  if(!gz)
    return false;
  TarPacker::setProgressCallBack( LZPacker::defaultProgressCallback );
  auto ret = TarGzPacker::compress(&tarGzFS, dirEntities, &gz, dst_path);
  gz.close();
  Serial.printf("Wrote %d bytes to %s\n", ret, targz_path);
  return ret>0;
}


void setup()
{
  Serial.begin(115200);

  delay(5000); // NOTE: USB-CDC is a drag

  Serial.println("TarPacker/TarGzPacker test");

  if(! tarGzFS.begin() ) {
    Serial.println("Failed to start filesystem, halting");
    while(1) yield();
  }

  removeTempFiles(); // cleanup previous examples
  Serial.println("Gathering directory entitites");
  TarPacker::collectDirEntities(&dirEntities, &tarGzFS, src_path); // collect dir and files at %{src_path}
  // LZPacker::setProgressCallBack( LZPacker::defaultProgressCallback );

  Serial.printf("Free heap: %lu bytes\n", HEAP_AVAILABLE() );

  // testTarPacker();
  // testTarPackerStream();
  // testTarGzPacker();
  testTarGzPackerStream();

  Serial.printf("Free heap: %lu bytes\n", HEAP_AVAILABLE() );


  Serial.println();

  #if defined USE_WEBSERVER
    setupNetwork();
  #elif defined ARDUINO_ARCH_RP2040
    // Set up the USB disk share
    singleFileDrive.begin("test.tar.gz", "test.tar.gz");
  #endif
}

void loop()
{
  #if defined USE_WEBSERVER
    handleNetwork();
  #endif
}
