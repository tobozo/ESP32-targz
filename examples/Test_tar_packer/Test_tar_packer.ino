// Set **destination** filesystem by uncommenting one of these:
//#define DEST_FS_USES_SPIFFS
#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only

#include <ESP32-targz.h>

// Uncomment `USE_WEBSERVER` to download the generated files when using SPIFFS/LittleFS
// Comment out `USE_WEBSERVER` if you get a "sketch too big" compilation error
#define USE_WEBSERVER
// Comment out `USE_ETHERNET` to use WiFi instead of Ethernet
// #define USE_ETHERNET

#if defined USE_WEBSERVER
  #if !defined USE_ETHERNET
    #include <WiFi.h>
  #else
    #include <ETH.h>

    static bool eth_connected = false;

    // WARNING: onEvent is called from a separate FreeRTOS task (thread)!
    void onEvent(arduino_event_id_t event) {
      switch (event) {
        case ARDUINO_EVENT_ETH_START:        Serial.println("ETH Started"); ETH.setHostname("esp32-ethernet");        break;
        case ARDUINO_EVENT_ETH_CONNECTED:    Serial.println("ETH Connected");                                         break;
        case ARDUINO_EVENT_ETH_GOT_IP:       Serial.println("ETH Got IP"); Serial.println(ETH); eth_connected = true; break;
        case ARDUINO_EVENT_ETH_LOST_IP:      Serial.println("ETH Lost IP"); eth_connected = false;                    break;
        case ARDUINO_EVENT_ETH_DISCONNECTED: Serial.println("ETH Disconnected"); eth_connected = false;               break;
        case ARDUINO_EVENT_ETH_STOP:         Serial.println("ETH Stopped"); eth_connected = false;                    break;
        default: break;
      }
    }

  #endif

  #include <WebServer.h>
  #include <ESPmDNS.h>
  WebServer server(80);
#endif


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

void testTarGzPackerStream()
{
  // targz_path: name of the .tar.gz compressed file
  // dst_path: optional path prefix in tar archive
  removeTempFiles(); // cleanup previous examples
  Serial.println();
  Serial.printf("TarGzPacker::compress(&tarGzFS, dirEntities, &gz, dst_path); Free heap: %lu bytes\n", HEAP_AVAILABLE() );
  auto gz = tarGzFS.open(targz_path, "w");
  if(!gz)
    return;
  auto ret = TarGzPacker::compress(&tarGzFS, dirEntities, &gz, dst_path);
  gz.close();
  Serial.printf("Wrote %d bytes to %s\n", ret, targz_path);
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
  TarPacker::collectDirEntities(&dirEntities, &tarGzFS, src_path, 3); // collect dir and files at %{src_path}
  LZPacker::setProgressCallBack( LZPacker::defaultProgressCallback );

  Serial.printf("Free heap: %lu bytes\n", HEAP_AVAILABLE() );

  testTarPacker();
  testTarPackerStream();
  testTarGzPacker();
  testTarGzPackerStream();

  Serial.printf("Free heap: %lu bytes\n", HEAP_AVAILABLE() );


  #if defined USE_WEBSERVER
    #if !defined USE_ETHERNET // start WiFI
      WiFi.mode(WIFI_STA);
      WiFi.begin();
      Serial.printf("Connect to WiFi...\n");
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.printf(".");
      }
      Serial.printf("connected.\n");
      Serial.printf("open <http://%s> or <http://%s>\n", WiFi.getHostname(), WiFi.localIP().toString().c_str());
    #else // start ethernet
      Network.onEvent(onEvent);  // Will call onEvent() from another thread.
      ETH.begin();
      while (!eth_connected) {
        delay(500);
        printf(".");
      }
    #endif
    // start webserver, serve filesystem at root
    server.serveStatic("/", tarGzFS, "/", nullptr);
    server.begin();
  #endif
}

void loop()
{
  #if defined USE_WEBSERVER
    server.handleClient();
  #endif
}
