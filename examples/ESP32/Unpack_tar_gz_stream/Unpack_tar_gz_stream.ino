/*\
 *
 * Unpack_tar_gz_stream.ino
 * Example code for ESP32-targz
 * https://github.com/tobozo/ESP32-targz
 *
\*/

#ifndef ESP32
  #error "gzStreamExpander is only available on ESP32 architecture"
#endif

// Set **destination** filesystem by uncommenting one of these:
//#define DEST_FS_USES_SPIFFS
//#define DEST_FS_USES_LITTLEFS
#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only
#include <ESP32-targz.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
HTTPClient http;


// 1) Edit the value of "const char* tgzURL" in this sketch to match the url of your gzip file
//
// 2) Setup WIFI_SSID and WIFI_PASS if necessary
//
// 3) Flash this sketch

const char* tgzURL = "https://raw.githubusercontent.com/tobozo/ESP32-targz/master/examples/Test_tar_gz_tgz/data/targz_example.tar.gz";
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


WiFiClient *getTarGzHTTPClientPtr( WiFiClientSecure *client, const char* url, const char *cert = NULL )
{
  if( cert == NULL ) {
    client->setInsecure();
  } else {
    client->setCACert( cert );
  }
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
      return getTarGzHTTPClientPtr( client, newlocation.c_str(), cert );
    } else {
      log_e("Empty redirect !!");
      return nullptr;
    }
  }
  if( httpCode != 200 ) return nullptr;
  return http.getStreamPtr();
}



void myTarProgressCallback( uint8_t progress )
{
  static int8_t myLastProgress = -1;
  if( myLastProgress != progress ) {
    myLastProgress = progress;
    if( progress == 0 ) {
      Serial.print("Progress: [0% ");
    } else if( progress == 100 ) {
      Serial.println(" 100%]\n");
    } else {
      switch( progress ) {
        case 25: Serial.print(" 25% ");break;
        case 50: Serial.print(" 50% ");break;
        case 75: Serial.print(" 75% ");break;
        default: Serial.print("T"); break;
      }
    }
  }
}



void setup()
{

  Serial.begin( 115200 );

#if defined DEST_FS_USES_SPIFFS || defined DEST_FS_USES_LITTLEFS
  tarGzFS.format();
#endif
  if( !tarGzFS.begin(4) ) {
    Serial.println("Could not start filesystem");
    while(1);
  }

  stubbornConnect();
  WiFiClientSecure *client = new WiFiClientSecure;
  Stream *streamptr = getTarGzHTTPClientPtr( client, tgzURL, certificate );

  if( streamptr != nullptr ) {

    TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();
    TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
    TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
    TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
    TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
    TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
    TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
    TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
    TARGZUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity

    if( !TARGZUnpacker->tarGzStreamExpander( streamptr, tarGzFS ) ) {
      Serial.printf("tarGzStreamExpander failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
    } else {
      // print leftover bytes if any (probably zero-fill from the server)
      while(http.connected() ) {
        size_t streamSize = streamptr->available();
        if (streamSize) {
          Serial.printf( "%02x ", streamptr->read() );
        } else break;
      }

      Serial.println();
    }

  } else {
    Serial.println("Failed to establish http connection");
  }

}


void loop()
{

}

