
#if defined USE_WEBSERVER
  // Comment out `USE_ETHERNET` to use WiFi instead of Ethernet
  // #define USE_ETHERNET

  #if !defined USE_ETHERNET
    #if defined ESP32
      #include <WiFi.h>
      #include <WebServer.h>
      #include <ESPmDNS.h>
      WebServer server(80);
    #elif defined ESP8266
      #include <ESP8266WiFi.h>
      #include <ESP8266WebServer.h>
      #include <ESP8266mDNS.h>
      ESP8266WebServer server(80);
      //Call to ESpressif SDK
      extern "C" {
        #include <user_interface.h>
      }
      uint8_t mac[6] {0x24, 0x0A, 0xC4, 0x11, 0xCF, 0x30};
    #else
      #error "Please include your wifi/webserver library from here"
    #endif
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


  void setupNetwork()
  {
    #if !defined USE_ETHERNET // start WiFI
      #if defined ESP8266
        wifi_set_macaddr(0, const_cast<uint8*>(mac)); // prevent ESP8266 from randomizing the mac address
      #endif
      Serial.println("Setting WiFi mode to WIFI_STA");
      WiFi.mode(WIFI_STA);
      Serial.println("Starting WiFi");
      WiFi.begin(/*WIFI_SSID, WIFI_PASS*/);
      Serial.printf("Connect to WiFi...\n");
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.printf(".");
      }
      Serial.printf("connected.\n");
      Serial.printf("open <http://%s>\n", WiFi.localIP().toString().c_str());
    #else // start ethernet
      Network.onEvent(onEvent);  // Will call onEvent() from another thread.
      ETH.begin();
      while (!eth_connected) {
        delay(500);
        printf(".");
      }
    #endif
    // start webserver, serve filesystem at root
    log_d("Serving static");
    server.serveStatic("/", tarGzFS, "/", nullptr);
    log_d("Server begin");
    server.begin();
  }


  void handleNetwork()
  {
    server.handleClient();
  }



#endif
