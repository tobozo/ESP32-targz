// This sketch runs stage 2 of the ESP32-tagz FOTA test.
// It is flashed from a targz archive during a test
// to demonstrate OTA, all it does is check for context,
// print some messages, and reboot if the context checks out.


// 1) Compile this sketch (don't flash it)
// 2) Find the .ino.bin file (e.g. `find ~/.cache/arduino/sketches | grep Test_tgz_FOTA.ino.bin`)
//    -> rename it to "firmware_example.ino.bin" (to match the value of `firmWareFile` in this sketch)
// 3) Find the `targz_example.tar.gz` example file in one of the other example folders
//    -> manually add the "firmware_example.ino.bin" file to the "targz_example.tar.gz" archive
//    -> use the LittleFS Filesystem Uploader plugin to upload the archive
// 4) Compile and flash the Test_tgz_FOTA_stage2.ino example

#include <LittleFS.h>
#include <esp_ota_ops.h>

// files created/deleted by the Test_tgz_FOTA* sketches, do not to edit that
const char* testFile = "/blah.txt";
const char* testCompleteFile = "/testComplete";


bool switchPartition()
{
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* target = esp_ota_get_next_update_partition(running);

  if (target == nullptr)
    return false; // No alternate app partition available

  return (esp_ota_set_boot_partition(target) == ESP_OK);
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("┌──────────────────────────────────────────────────────────────────┐");
  Serial.println("|             Basic Example Sketch for ESP32-Targz                 |");
  Serial.println("|       If you can read this, then the gzUpdater worked :-)        |");
  LittleFS.begin();
  if(LittleFS.exists(testFile)) {
    Serial.println("|      Now printing contents of the custom upstream file....       |");
    auto file = LittleFS.open(testFile);
    while(file.available())
      Serial.write(file.read());
    file.close();
    Serial.println("|           Now deleting the custom upstream file....              |");
    LittleFS.remove(testFile);
  }

  if( LittleFS.exists(testCompleteFile) ) {
    // Sketch rebooted on itself, prevent endless loop
    Serial.println("|  Found traces of previous run, aborting to prevend endless loop  |");
    Serial.println("└──────────────────────────────────────────────────────────────────┘");
    return;
  }

  Serial.println("├──────────────────────────────────────────────────────────────────┤");
  Serial.println("|                        Now rolling back                          |");
  Serial.println("└──────────────────────────────────────────────────────────────────┘");
  {
    auto file = LittleFS.open(testCompleteFile, "w");
    file.print("ok");
    file.close();
  }

  if(switchPartition())
    ESP.restart();
  else
    Serial.println("OTA Rollback failed :(");

}

void loop() { }
