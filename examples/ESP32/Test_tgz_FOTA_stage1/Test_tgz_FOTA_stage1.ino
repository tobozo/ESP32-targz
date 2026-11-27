#include <LittleFS.h>
#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>

// file path in the targz archive to the stage 2 firmware (see steps in Test_tgz_FOTA_stage2.ino example)
const char* firmWareFile = "firmware_example.ino.bin";
// tgz archive name (in the /data folder of this project, to upload with Flash Filesystem Uploader plugin)
const char* tgzFile = "/targz_example.tar.gz";


// Files created/deleted by stage 1 and stage 2 of the FOTA test, do not to edit!
const char* testFile = "/blah.txt";
const char* testCompleteFile = "/testComplete";


void setup()
{
  Serial.begin(115200);

  Serial.println("┌──────────────────────────────────────────────────────────────────┐");
  Serial.println("|     Routed tarGzStreamExpander example, with firmware update     |");


  if( !tarGzFS.begin(true) ) {
    Serial.println("|               Error: Could not start LittleFS                    |");
    Serial.println("└──────────────────────────────────────────────────────────────────┘");
    return;
  }

  bool halt = false;

  if(tarGzFS.exists(testFile)) {
    tarGzFS.remove(testFile);
    halt = true;
  }
  if(tarGzFS.exists(testCompleteFile)) {
    tarGzFS.remove(testCompleteFile);
    halt = true;
  }

  if(halt) {
    Serial.println("|             Test complete, press reset to restart                |");
    Serial.println("└──────────────────────────────────────────────────────────────────┘");
    while(1);
  }

  // IMPORTANT NOTE: the targz_example.tar.gz must contain the `firmware_example_esp32xx.ino.bin` compiled when #define SECONDARY_SKETCH is set
  File archive = tarGzFS.open(tgzFile, "r");
  if( !archive ) {
    Serial.printf("|             Upload %s to LittleFS first                    |\n", tgzFile);
    Serial.println("└──────────────────────────────────────────────────────────────────┘");
    return;
  }

  TarGzUnpacker unpacker;
  unpacker.setLoggerCallback(BaseUnpacker::targzPrintLoggerCallback);
  // Expand all files to filesystem except when the filename matches `const char* firmwareFile`.
  bool ota_succeeded = false;
  unpacker.flashEntryOnExtract(firmWareFile, &ota_succeeded);

  bool success = unpacker.tarGzStreamExpander(&archive, tarGzFS, "/", archive.size());
  archive.close();

  if( !success ) {
    Serial.printf("Expansion failed with error %d\n", unpacker.tarGzGetError());
    Serial.println("|                     Test failed, now halting                     |");
    Serial.println("└──────────────────────────────────────────────────────────────────┘");
    return;
  }

  if( !ota_succeeded ) {
    Serial.println("|                      OTA failed, now halting                     |");
    Serial.println("└──────────────────────────────────────────────────────────────────┘");
    return;
  }


  BaseUnpacker *Unpacker = new BaseUnpacker();
  Unpacker->tarGzListDir( tarGzFS, "/", 3 );

  auto file = tarGzFS.open(testFile, "w");
  file.print("blah blah blah\n");
  file.close();
  Serial.println("|                Test complete, will now restart                   |");
  Serial.println("└──────────────────────────────────────────────────────────────────┘");
  ESP.restart();

}

void loop() { }

