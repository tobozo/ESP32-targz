/*\
 *
 * Update_from_gz_stream.ino
 * Example code for ESP32-targz
 * https://github.com/tobozo/ESP32-targz
 *
\*/

#ifndef ESP32
  #error "tarGzStreamUpdater is only available on ESP32 architecture"
#endif

// Set **destination** filesystem by uncommenting one of these:
//#define DEST_FS_USES_SPIFFS
#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <rom/rtc.h> // to get reset reason
HTTPClient http;

// 1) Choose between testing tarGzStreamUpdater or gzStreamUpdater
//      /!\ tarGzStreamUpdater can update both spiffs *and* the firmware while
//          gzStreamUpdater can only do spiffs *or* the firmware
//      The choice is made by uncommenting one of those defines:
//
//#define TEST_gzStreamUpdater
#define TEST_tarGzStreamUpdater
//
// 2) Get the binaries of the firmware and/or spiffs partition you want to make available as gz/targz OTA update
//
// 3) Depending on the choice, make sure
//      - the firmware binary filename is suffixed by "ino.bin" (rename if necessary)
//      - the spiffs binary filename is suffixed by "spiffs.bin" (rename if necessary)
//
// 4) Create either:
//      - the .tar.gz archive:
//        $ cd /tmp/arduino_build_xxxxxx/ && tar cvzf partitions_bundle.tar.gz Your_Sketch.ino.bin Your_Sketch.spiffs.bin
//      - the .gz archive:
//        $ cd /tmp/arduino_build_xxxxxx/ && gzip -c Your_Sketch.spiffs.bin > firmware.gz
//
// 5) Publish the archive file on a web server
//
// 6) Edit the value of "const char* bundleURL" in this sketch to match the url to this archive file
//
// 7) Setup WIFI_SSID and WIFI_PASS if necessary (optional if your ESP32 had a previous successful connection to WiFi)
//
//#define WIFI_SSID "blahSSID"
//#define WIFI_PASS "blahPASSWORD"
//
// 8) Flash this sketch
//


#if (defined TEST_tarGzStreamUpdater && defined TEST_gzStreamUpdater ) || (!defined TEST_tarGzStreamUpdater && !defined TEST_gzStreamUpdater )

  #error "Please define either TEST_gzStreamUpdater or TEST_tarGzStreamUpdater"

#elif defined TEST_tarGzStreamUpdater

  const char* bundleURL = "https://raw.githubusercontent.com/tobozo/ESP32-targz/tarGzStreamUpdater/examples/Test_tar_gz_tgz/SD/partitions_bundle_esp32.tar.gz" ;

#else // TEST_gzStreamUpdater

  const char* bundleURL = "https://raw.githubusercontent.com/tobozo/ESP32-targz/tarGzStreamUpdater/examples/Test_tar_gz_tgz/data/firmware_example_esp32.gz" ;

#endif


const char *certificate = NULL; // change this as needed, leave as is for no TLS check (yolo security)


void stubbornConnect()
{
  uint8_t wifi_retry_count = 0;
  uint8_t max_retries = 10;
  unsigned long stubbornness_factor = 3000; // ms to wait between attempts

  Serial.print( "MAC Address: " );
  Serial.println(WiFi.macAddress());

  while (WiFi.status() != WL_CONNECTED && wifi_retry_count < max_retries) {
    #if defined WIFI_SSID && defined WIFI_PASS
      WiFi.begin( WIFI_SSID, WIFI_PASS ); // put your ssid / pass if required, only needed once
    #else
      WiFi.begin();
    #endif
    Serial.printf(" => WiFi connect - Attempt No. %d\n", wifi_retry_count+1);
    delay( stubbornness_factor );
    wifi_retry_count++;
  }
  if(wifi_retry_count >= max_retries ) {
    Serial.println("no connection, forcing restart");
    ESP.restart();
  }
  if (WiFi.waitForConnectResult() == WL_CONNECTED){
    Serial.println("Connected as");
    Serial.println(WiFi.localIP());
  }
}


WiFiClient *getFirmwareClientPtr( WiFiClientSecure *client, const char* url, const char *cert = NULL )
{
  if( cert == NULL ) client->setInsecure();
  else client->setCACert( cert );
  const char* UserAgent = "ESP32-HTTP-TarGzUpdater-Client";
  http.setReuse(true); // handle 301 redirects gracefully
  http.setUserAgent( UserAgent );
  http.setConnectTimeout( 10000 ); // 10s timeout = 10000
  if( ! http.begin(*client, url ) ) {
    log_e("Can't open url %s", url );
    return nullptr;
  }
  const char * headerKeys[] = {"location", "redirect", "Content-Type", "Content-Length", "Content-Disposition" };
  const size_t numberOfHeaders = 5;
  http.collectHeaders(headerKeys, numberOfHeaders);
  int httpCode = http.GET();
  // file found at server
  if (httpCode == HTTP_CODE_FOUND || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
    String newlocation = "";
    String headerLocation = http.header("location");
    String headerRedirect = http.header("redirect");
    if( headerLocation !="" ) {
      newlocation = headerLocation;
      Serial.printf("302 (location): %s => %s\n", url, headerLocation.c_str());
    } else if ( headerRedirect != "" ) {
      Serial.printf("301 (redirect): %s => %s\n", url, headerLocation.c_str());
      newlocation = headerRedirect;
    }
    http.end();
    if( newlocation != "" ) {
      log_w("Found 302/301 location header: %s", newlocation.c_str() );
      // delete client;
      return getFirmwareClientPtr( client, newlocation.c_str(), cert );
    } else {
      log_e("Empty redirect !!");
      return nullptr;
    }
  }
  if( httpCode != 200 ) return nullptr;
  return http.getStreamPtr();
}


void setup()
{

  Serial.begin( 115200 );
  tarGzFS.begin();
  #if defined  TEST_tarGzStreamUpdater
    TarGzUnpacker *Unpacker = new TarGzUnpacker();

    if( rtc_get_reset_reason(0) != 1 ) // software reset or crash
    {
      Serial.println("Listing destination Filesystem contents:");
      Unpacker->tarGzListDir( tarGzFS, "/", 3 );
      Serial.println("Press reset to restart test");
      return;
    }

    Serial.println("Pre formattings filesystem");
    tarGzFS.format();
    Serial.println("Done!");

  #elif defined TEST_gzStreamUpdater
    GzUnpacker *Unpacker = new GzUnpacker();

    if( rtc_get_reset_reason(0) != 1 ) // software reset or crash
    {
      Serial.println("Press reset to restart test");
      return;
    }

  #endif



  stubbornConnect();
  WiFiClientSecure *client = new WiFiClientSecure;
  Stream *streamptr = getFirmwareClientPtr( client, bundleURL, certificate );
  //size_t streamsize = UPDATE_SIZE_UNKNOWN;

  /*
  // it also works with filesystem
  SD.begin();
  if(!SD.exists(firmwareFile) ) {
    Serial.println("File isn't there");
    while(1) { yield(); }
  }
  fs::File file = SD.open( bundleFile, "r" );
  size_t streamsize = file.size();
  if (!file) {
    Serial.println("Can't open file");
    while(1) { yield(); }
  }
  Stream *streamptr = &file;
  */

  if( streamptr != nullptr ) {
    #if defined  TEST_tarGzStreamUpdater
      Unpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
      Unpacker->setTarVerify( false ); // disable verify as we're writing to a partition
      Unpacker->setGzProgressCallback( BaseUnpacker::targzNullProgressCallback ); // gz progress is irrelevant for this operation
      Unpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // print the untarring progress for each individual partition
      Unpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
      Unpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity
      if( !Unpacker->tarGzStreamUpdater( streamptr ) ) {
        Serial.printf("tarGzStreamUpdater failed with return code #%d\n", Unpacker->tarGzGetError() );
      }
    #elif defined TEST_gzStreamUpdater
      //GzUnpacker *Unpacker = new GzUnpacker();
      Unpacker->setGzProgressCallback( BaseUnpacker::targzNullProgressCallback );
      Unpacker->setPsram( true );
      if( !Unpacker->gzStreamUpdater( streamptr, UPDATE_SIZE_UNKNOWN, 0, false ) ) {
        Serial.printf("tarGzStreamUpdater failed with return code #%d\n", Unpacker->tarGzGetError() );
      }
    #endif
    else {
      Serial.println("Update successful, now loading the new firmware!");
      ESP.restart();
    }

  } else {
    Serial.println("Failed to establish http connection");
  }

}


void loop()
{

}
