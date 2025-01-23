// Set **destination** filesystem by uncommenting one of these:
//#define DEST_FS_USES_SPIFFS
#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only

// #define ESP32_TARGZ_DISABLE_COMPRESSION
// #define ESP32_TARGZ_DISABLE_DECOMPRESSION
#include <ESP32-targz.h>

// Uncomment this macro to download the generated files when using SPIFFS/LittleFS
// Comment out this macro if you get a "sketch too big" compilation error
#define USE_WEBSERVER

#if defined USE_WEBSERVER
  #include <WiFi.h>
  #include <WebServer.h>
  #include <ESPmDNS.h>
  WebServer server(80);
#endif



const char* src_path   = "/";            // source folder (no ending slash except if root)
const char* tar_path   = "/test.tar";    // output tar archive to create
const char* targz_path = "/test.tar.gz"; // output tar archive to create
const char* dst_path   = "data";         // optional virtual folder in the tar archive, no ending slash, set to NULL to put everything in the root

std::vector<TAR::dir_entity_t> dirEntities; // storage for scanned dir entities

void removeTempFiles()
{
  // cleanup test from other examples to prevent SPIFFS from exploding :)
  if(tarGzFS.exists(tar_path))
    tarGzFS.remove(tar_path);
  if(tarGzFS.exists(targz_path))
    tarGzFS.remove(targz_path);
}


bool testTarGzPackerFS()
{
  // Read source directory "/", append content under "data/" in tar output, and compress to "test.tar.gz"
  // src_path: source, path to dir (will recursively fetch files)
  // targz_path: destination, .tar.gz compressed file
  // dst_path: optional, rootdir name in tar archive
  // NOTE: Source and destination are on the same filesystem.
  removeTempFiles();
  Serial.println();
  Serial.println("TarGzPacker::compress(&tarGzFS, src_path, targz_path, dst_path )");
  size_t dstLen = TarGzPacker::compress(&tarGzFS, src_path, targz_path, dst_path );
  Serial.printf("Source folder '%s' compressed to %d bytes in %s\n", src_path, dstLen, targz_path);
  Serial.printf("Free heap: %lu bytes\n", ESP.getFreeHeap() );
  return dstLen > 0;
}



bool testTarGzPackerStream()
{
  // Read source directory "/", put content under "data/" in tar archive, and compress to stream
  // src_path: source, path to dir (will recursively fetch files)
  // targz_path: optional, name of the .tar.gz compressed file to prevent self-inclusion
  // dst_path: optional, rootdir name in tar archive
  removeTempFiles();
  Serial.println();
  Serial.printf("TarGzPacker::compress( &tarStream, &tarGzOutput ); Free heap: %lu bytes\n", ESP.getFreeHeap() );
  TarGzStream tarStream(&tarGzFS, &dirEntities, src_path, targz_path, dst_path);
  Serial.printf("Free heap: %lu bytes\n", ESP.getFreeHeap() );
  size_t srcLen = tarStream.size();
  File tarGzOutput = tarGzFS.open(targz_path, "w"); // NOTE: tarGzOutput can also be a network client, or even Serial
  size_t dstLen = TarGzPacker::compress( &tarStream, &tarGzOutput );
  tarGzOutput.close();
  Serial.printf("Compressed %d bytes to %s (%d bytes)\n", srcLen, targz_path, dstLen);
  Serial.printf("Free heap: %lu bytes\n", ESP.getFreeHeap() );
  return dstLen > 0;
}


bool testTarPacker()
{
  // Read source directory "/", put contents under "data/" in tar archive, and pack to "test.tar"
  // src_path: source, path to dir (will recursively fetch files)
  // tar_path: destination, .tar packed file
  // NOTE: Source and destination are on the same filesystem.
  removeTempFiles();
  Serial.println();
  Serial.printf("TarPacker::pack_files(&tarGzFS, &dirEntities, tar_path, dst_path); Free heap: %lu bytes\n", ESP.getFreeHeap() );
  ssize_t dstLen = TarPacker::pack_files(&tarGzFS, &dirEntities, tar_path, dst_path);
  if( dstLen <= 0 ) {// test failed
    Serial.printf("Packing failed (ret=%d)\n", dstLen);
    return false;
  }
  File tar = tarGzFS.open(tar_path);
  size_t tar_size = tar.size();
  tar.close();
  Serial.printf("Wrote %d bytes to %s, file size=%d\n", dstLen, tar_path, tar_size );
  return ( tar_size == dstLen );
}


void setup()
{
  Serial.begin(115200);
  Serial.println("TarPacker test");

  if(! tarGzFS.begin() ) {
    Serial.println("Failed to start filesystem, halting");
    while(1) yield();
  }

  removeTempFiles(); // cleanup previous examples
  TarPacker::collectDirEntities(&dirEntities, &tarGzFS, src_path, 3); // collect dir and files at %{src_path}
  LZPacker::setProgressCallBack( LZPacker::defaultProgressCallback );

  Serial.printf("Free heap: %lu bytes\n", ESP.getFreeHeap() );

  // testTarGzPackerFS();
  testTarGzPackerStream();
  // testTarPacker();

  Serial.printf("Free heap: %lu bytes\n", ESP.getFreeHeap() );

  #if defined USE_WEBSERVER
    // start WiFI
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    printf("Connect to WiFi...\n");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      printf(".");
    }
    printf("connected.\n");
    // start webserver, serve filesystem at root
    server.serveStatic("/", tarGzFS, "/", nullptr);
    server.begin();
    printf("open <http://%s> or <http://%s>\n", WiFi.getHostname(), WiFi.localIP().toString().c_str());
  #endif
}

void loop()
{
  #if defined USE_WEBSERVER
    server.handleClient();
  #endif
}
