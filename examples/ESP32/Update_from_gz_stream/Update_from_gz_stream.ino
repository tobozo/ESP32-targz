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
#define DEST_FS_USES_SPIFFS
//#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <rom/rtc.h> // to get reset reason
HTTPClient http;


// 1) Produce the binaries for both the firmware the spiffs
// 2) Make sure the firmware binary filename is suffixed by "ino.bin" (rename if necessary)
// 3) Make sure the spiffs binary filename is suffixed by "spiffs.bin" (rename if necessary)
// 4) Create the .tar.gz archive:
//      $ cd /tmp/arduino_build_xxxxxx/ && tar cvzf partitions_bundle.tar.gz Your_Sketch.ino.bin Your_Sketch.spiffs.bin
// 2) Publish the partitions_bundle.tar.gz file on a web server
// 3) Edit the value of "const char* bundleURL" in this sketch to match the url to this .tar.gz file
// 4) Setup WIFI_SSID and WIFI_PASS if necessary
// 5) Flash this sketch

const char* bundleURL = "https://raw.githubusercontent.com/tobozo/ESP32-targz/tarGzStreamUpdater/examples/Test_tar_gz_tgz/SD/partitions_bundle_esp32.tar.gz" ;
const char *certificate = NULL; // change this as needed, leave as is for no TLS check (yolo security)

//#define WIFI_SSID "blahSSID"
//#define WIFI_PASS "blahPASSWORD"

void stubbornConnect()
{
  uint8_t wifi_retry_count = 0;
  uint8_t max_retries = 10;
  unsigned long stubbornness_factor = 3000; // ms to wait between attempts
  while (WiFi.status() != WL_CONNECTED && wifi_retry_count < max_retries) {
    #if defined WIFI_SSID && defined WIFI_PASS
      WiFi.begin( WIFI_SSID, WIFI_PASS ); // put your ssid / pass if required, only needed once
    #else
      WiFi.begin();
    #endif
    Serial.print(WiFi.macAddress());
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
  client->setCACert( cert );
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
  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

  if( rtc_get_reset_reason(0) != 1 ) // software reset or crash
  {
    Serial.println("Listing destination Filesystem contents:");
    TARGZUnpacker->tarGzListDir( tarGzFS, "/", 3 );
    Serial.println("Press reset to restart test");
    return;
  }

  Serial.println("Pre formattings filesystem");
  tarGzFS.format();
  Serial.println("Done!");

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

    TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
    TARGZUnpacker->setTarVerify( false ); // disable verify as we're writing to a partition
    TARGZUnpacker->setGzProgressCallback( BaseUnpacker::targzNullProgressCallback ); // gz progress is irrelevant for this operation
    TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // print the untarring progress for each individual partition
    TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
    TARGZUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity

    if( !TARGZUnpacker->tarGzStreamUpdater( streamptr ) ) {
      Serial.printf("tarGzStreamUpdater failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
    } else {
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
