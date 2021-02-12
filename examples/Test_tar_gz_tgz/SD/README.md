
# Use with tarGzStreamUpdater( Stream *stream )

`tarGzStreamUpdater()` will find and extract binaries from a `.tar.gz` archive to OTA and/or SPIFFS partitions using the UpdateClass from the ESP32 Arduino core.

In the example sketch of this library, the file `partitions_bundle_esp32.tar.gz` should be copied on a SD card.
This file should contain both binaries for the compiled application and the spiffs partition, their path and order of extraction do not matter although you may want to have the app binary to show first for more resilience in failure situations.

Since this function accepts streams as its only argument, passing a http client or a wifi secure client should also be possible.

File naming requirements
-------------------------
  - Archive must be `.tar.gz`
  - Application binary file name must end with `ino.bin`
  - Spiffs binary file name must end with `spiffs.bin`
  - All other files/folders in the archive are ignored
