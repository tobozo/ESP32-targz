/*
 * Example use of TarGzPacker::compress() from SD Card to LittleFS
 */
#define SD_FAT_TYPE 3 // no other mode available with esp32-targz
#include "SD.h"
//#include "sdios.h"
#include <LittleFS.h>

#include <ESP32-targz.h>

#ifndef SDCARD_SS_PIN // SDCARD_SS_PIN is defined for the built-in SD on some boards.
  const uint8_t SD_CS_PIN = SS;
#else  // SDCARD_SS_PIN
  // Assume built-in SD is used.
  const uint8_t SD_CS_PIN = SDCARD_SS_PIN;
#endif  // SDCARD_SS_PIN

// Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
#define SPI_CLOCK SD_SCK_MHZ(25)

// Try to select the best SD card configuration.
#if HAS_SDIO_CLASS
  #define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif  ENABLE_DEDICATED_SPI
  #define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
#else  // HAS_SDIO_CLASS
  #define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)
#endif  // HAS_SDIO_CLASS
//------------------------------------------------------------------------------

// #if ARDUINO_TEENSY41
// #error blah
// #endif

SdFs sd;

LittleFS_Program LittleFSProgram;

// NOTE: This option is only available on the Teensy 4.0, Teensy 4.1 and Teensy Micromod boards.
// With the additonal option for security on the T4 the maximum flash available for a
// program disk with LittleFS is 960 blocks of 1024 bytes
#define PROG_FLASH_SIZE 1024 * 1024 * 1 // Specify size to use of onboard Teensy Program Flash chip
                                        // This creates a LittleFS drive in Teensy PCB FLash.

// Wrap SdFat in a fs::FS layer, this is only necessary for functions that expect that type.
// Other Stream based functions work normally.
fs::FS tarGzFs = unifyFS(sd);


void setup() {
  Serial.begin(115200);

  // Wait for USB Serial
  while (!Serial) {
    yield();
  }
  delay(1000);
  // Wait for Serial input
  printf("Type any character to start\n");
  while (!Serial.available()) {
    yield();
  }

  // Initialize the SD card.
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt(&Serial);
  }

  // checks that the LittleFS program has started with the disk size specified
  if (!LittleFSProgram.begin(PROG_FLASH_SIZE)) {
    Serial.printf("Error starting PROGRAM FLASH DISK, halting\n");
    while (1) {}
  }
  Serial.println("LittleFS initialized.");

  File out = LittleFSProgram.open("/out.tgz", FILE_WRITE);

  std::vector<TAR::dir_entity_t> dirEntities; // storage for scanned dir entities
  TarPacker::collectDirEntities(&dirEntities, &tarGzFs, "/"); // collect dir and files
  size_t compressed_size = TarGzPacker::compress(&tarGzFs, dirEntities, &out);

  out.close();

  Serial.printf("Saved %d compressed bytes\n", compressed_size);
}


void loop() {}
