/*\
 *
 * Update_from_gz_stream.ino
 * Example code for ESP32-targz
 * https://github.com/tobozo/ESP32-targz
 *
\*/

#ifndef ESP32
  #error "gzStreamUpdater is only available on ESP32 architecture"
#endif

// Set **destination** filesystem by uncommenting one of these:
#define DEST_FS_USES_SPIFFS
//#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only
#include <ESP32-targz.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
HTTPClient http;


// 1) gzip the other firmware you need to flash:
//
//    #> gzip -c /tmp/arduino/firmware.bin > /tmp/firmware.gz
//
// 2) Publish the firmware.gz on a web server
//
// 3) Edit the value of "const char* firmwareFile" in this sketch to match the url of the gzip file
//
// 4) Setup WIFI_SSID and WIFI_PASS if necessary
//
// 5) Flash this sketch

const char* firmwareURL = "https://raw.githubusercontent.com/tobozo/ESP32-targz/master/examples/Update_from_gz/data/esp32_example_firmware.gz" ;
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
  const char* UserAgent = "ESP32-HTTP-GzUpdater-Client";
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

  stubbornConnect();
  WiFiClientSecure *client = new WiFiClientSecure;
  Stream *streamptr = getFirmwareClientPtr( client, firmwareURL, certificate );
  size_t streamsize = UPDATE_SIZE_UNKNOWN;

  /*
  // it also works with filesystem
  tarGzFS.begin();
  if(!tarGzFS.exists(firmwareFile) ) {
    Serial.println("File isn't there");
    while(1) { yield(); }
  }
  fs::File file = tarGzFS.open( firmwareFile, "r" );
  size_t streamsize = file.size();
  if (!file) {
    Serial.println("Can't open file");
    while(1) { yield(); }
  }
  Stream *streamptr = &file;
  */

  if( streamptr != nullptr ) {

    GzUnpacker *GZUnpacker = new GzUnpacker();
    GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
    GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
    GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
    GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
    if( !GZUnpacker->gzStreamUpdater( streamptr, streamsize ) ) {
      Serial.printf("gzHTTPUpdater failed with return code #%d\n", GZUnpacker->tarGzGetError() );
    }

  } else {
    Serial.println("Failed to establish http connection");
  }

}


void loop()
{

}
