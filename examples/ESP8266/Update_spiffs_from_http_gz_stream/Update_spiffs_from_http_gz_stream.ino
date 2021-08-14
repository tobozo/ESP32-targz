/*
    HTTP over TLS (HTTPS) example sketch

    This example demonstrates how to use
    WiFiClientSecure class to access HTTPS API.
    We fetch and display the status of
    esp8266/Arduino project continuous integration
    build.

    Created by Ivan Grokhotkov, 2015.
    This example is in public domain.
*/
#define DEBUG_ESP_PORT Serial

// Set **destination** filesystem by uncommenting one of these:
//#define DEST_FS_USES_SPIFFS
#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only
#include <ESP32-targz.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>

//#define STASSID "your-ssid"
//#define STAPSK  "your-password"

#if defined STASSID && defined STAPSK
const char* ssid = STASSID;
const char* password = STAPSK;
#else
const char* ssid = nullptr;
const char* password = nullptr;
#endif

const char* host     = "raw.githubusercontent.com";
//const char* filepath = "/tobozo/ESP32-targz/master/examples/Test_tar_gz_tgz/data/firmware_example_esp8266.gz";
const char* filepath = "/tobozo/ESP32-targz/master/examples/Test_tar_gz_tgz/data/targz_example.tar.gz";
const int httpsPort  = 443;

// DigiCert High Assurance EV Root CA
const char trustRoot[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDxTCCAq2gAwIBAgIQAqxcJmoLQJuPC3nyrkYldzANBgkqhkiG9w0BAQUFADBs
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSswKQYDVQQDEyJEaWdpQ2VydCBIaWdoIEFzc3VyYW5j
ZSBFViBSb290IENBMB4XDTA2MTExMDAwMDAwMFoXDTMxMTExMDAwMDAwMFowbDEL
MAkGA1UEBhMCVVMxFTATBgNVBAoTDERpZ2lDZXJ0IEluYzEZMBcGA1UECxMQd3d3
LmRpZ2ljZXJ0LmNvbTErMCkGA1UEAxMiRGlnaUNlcnQgSGlnaCBBc3N1cmFuY2Ug
RVYgUm9vdCBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMbM5XPm
+9S75S0tMqbf5YE/yc0lSbZxKsPVlDRnogocsF9ppkCxxLeyj9CYpKlBWTrT3JTW
PNt0OKRKzE0lgvdKpVMSOO7zSW1xkX5jtqumX8OkhPhPYlG++MXs2ziS4wblCJEM
xChBVfvLWokVfnHoNb9Ncgk9vjo4UFt3MRuNs8ckRZqnrG0AFFoEt7oT61EKmEFB
Ik5lYYeBQVCmeVyJ3hlKV9Uu5l0cUyx+mM0aBhakaHPQNAQTXKFx01p8VdteZOE3
hzBWBOURtCmAEvF5OYiiAhF8J2a3iLd48soKqDirCmTCv2ZdlYTBoSUeh10aUAsg
EsxBu24LUTi4S8sCAwEAAaNjMGEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB/wQF
MAMBAf8wHQYDVR0OBBYEFLE+w2kD+L9HAdSYJhoIAu9jZCvDMB8GA1UdIwQYMBaA
FLE+w2kD+L9HAdSYJhoIAu9jZCvDMA0GCSqGSIb3DQEBBQUAA4IBAQAcGgaX3Nec
nzyIZgYIVyHbIUf4KmeqvxgydkAQV8GK83rZEWWONfqe/EW1ntlMMUu4kehDLI6z
eM7b41N5cdblIZQB2lWHmiRk9opmzN6cN82oNLFpmyPInngiK3BD41VHMWEZ71jF
hS9OMPagMRYjyOfiZRYzy78aG6A9+MpeizGLYAiJLQwGXFK3xPkKmNEVX58Svnw2
Yzi9RKR/5CYrCsSXaQ3pjOLAEFe4yHYSkVXySGnYvCoCWw9E1CAx2/S6cCZdkGCe
vEsXCS+0yx5DaMkHJ8HSXPfqIbloEpw8nL+e/IBcm2PN7EeqJSdnoDfzAIJ9VNep
+OkuE6N36B9K
-----END CERTIFICATE-----
)EOF";

X509List cert(trustRoot);



void setup()
{

  Serial.begin(115200);
  Serial.println();
  if( !tarGzFS.begin() ) {
    Serial.println("Could not start filesystem");
    while(1) yield();
  } else {
    //Serial.println("Formatting filesystem");
    //tarGzFS.format();
    //Serial.println("Filesystem formatted");
  }
  Serial.println("MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Connecting to " + String( ssid ) );
  WiFi.mode(WIFI_STA);
  if( ssid != nullptr && password != nullptr ) {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin();
  }
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // Set time via NTP, as required for x.509 validation
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));

  // Use WiFiClientSecure class to create TLS connection
  WiFiClientSecure client;
  Serial.print("Connecting to ");
  Serial.println(host);

  client.setTrustAnchors(&cert);

  if (!client.connect(host, httpsPort)) {
    Serial.println("Connection failed");
    return;
  }

  String url = String(filepath);
  Serial.print("Requesting URL: ");
  Serial.println(url);

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: SpiffsUpdaterESP8266\r\n" +
               "Connection: close\r\n\r\n");

  Serial.println("Request sent");

  size_t streamsize = 95820; // hardcoded just in case the server does not send content-length header

  while (client.connected()) {
    // read headers
    String line = client.readStringUntil('\n');
    //Serial.println( line );
    if( line.startsWith("Content-Length: ")) {
      line.replace("Content-Length: ", "");
      streamsize = atoi( line.c_str() );
      continue;
    }
    if (line == "\r") {
      Serial.println("Headers received");

      TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

      TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
      TARGZUnpacker->setTarVerify( false ); // true = enables health checks but slows down the overall process
      TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
      TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
      TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
      TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
      TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
      TARGZUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity

      if( !TARGZUnpacker->tarGzStreamExpander( (Stream*)&client, tarGzFS, "/tmp" ) ) {
        Serial.printf("tarGzStreamExpander failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
      } else {
        // print leftover bytes if any (probably zero-fill from the server)
        while(client.connected() ) {
          size_t streamSize = client.available();
          if (streamSize) {
            Serial.printf( "Leftover byte: 0x%02x\n", client.read() );
          } else break;
        }
        Serial.println();
      }

      TARGZUnpacker->tarGzListDir( tarGzFS, "/", 3 );

      /*
      GzUnpacker *GZUnpacker = new GzUnpacker();
      GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
      GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
      GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
      GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
      if( !GZUnpacker->gzStreamUpdater( (Stream*)&client, streamsize ) ) {
        Serial.printf("gzHTTPUpdater failed with return code #%d\n", GZUnpacker->tarGzGetError() );
      }
      */
      break;
    }
  }

}

void loop() {
}

