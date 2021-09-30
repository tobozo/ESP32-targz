/*\
 *
 * Test_tar_gz_tgz.ino
 * Test suite for ESP32-targz
 * https://github.com/tobozo/ESP32-targz
 *
\*/
// Set **destination** filesystem by uncommenting one of these:
//#define DEST_FS_USES_SPIFFS // WARN: SPIFFS is full of bugs
#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only
//#define DEST_FS_USES_PSRAMFS // ESP32 only
#include <ESP32-targz.h>

#define sourceFS tarGzFS // assume source = destination unless stated otherwise in the test function

#if defined ESP32 && defined BOARD_HAS_PSRAM && defined DEST_FS_USES_PSRAMFS
  #undef sourceFS

  #if defined ESP_IDF_VERSION_MAJOR && ESP_IDF_VERSION_MAJOR >= 4
    #include <LittleFS.h> // core 2.0.0 has built-in LittleFS
    #define sourceFS LittleFS
  #else
    #include <LITTLEFS.h> // https://github.com/lorol/LITTLEFS
    #define sourceFS LITTLEFS
  #endif
  #include <PSRamFS.h> // https://github.com/tobozo/ESP32-PsRamFS
  #undef tarGzFS
  #undef FS_NAME

  #define SOURCE_FS_NAME "LittleFS"

  #define tarGzFS PSRamFS
  #define FS_NAME "PsRamFS"

  #define SOURCE_AND_DEST_DIFFER

#endif


#include "test_tools.h"

// Test #1: tarExpander()
// Requires: tar file
bool test_tarExpander()
{
  bool ret = false;
  const char* tarFile = "/tar_example.tar";
  myPackage.folder = "/tmp"; // for md5 tests

  SerialPrintCentered("Testing tarExpander", false, true );

  TarUnpacker *TARUnpacker = new TarUnpacker();

  TARUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)

  #if defined ESP32
    TARUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
  #endif
  #if defined ESP8266
    TARUnpacker->setTarVerify( false ); // true = enables health checks but slows down the overall process
  #endif

  TARUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  //TARUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );  // log verbosity
  TARUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
  TARUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
  TARUnpacker->setTarMessageCallback( myTarMessageCallback/*BaseUnpacker::targzPrintLoggerCallback*/ ); // tar log verbosity

  if(  !TARUnpacker->tarExpander(sourceFS, tarFile, tarGzFS, myPackage.folder ) ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("tarExpander failed with return code #%d", TARUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}



// Test #2: gzExpander()
// Requires: gzipped file, filename ends with ".gz"
bool test_gzExpander()
{
  bool ret = false;
  const char* gzFile = "/gz_example.jpg.gz";
  myPackage.folder = "/"; // for md5 tests

  SerialPrintCentered("Testing gzExpander", false, true );

  GzUnpacker *GZUnpacker = new GzUnpacker();

  GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  GZUnpacker->setGzMessageCallback( myTarMessageCallback/*BaseUnpacker::targzPrintLoggerCallback*/ );

  #ifdef ESP32
  GZUnpacker->setPsram( true );
  #endif

  if( !GZUnpacker->gzExpander(sourceFS, gzFile, tarGzFS ) ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("gzExpander failed with return code #%d", GZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}


// test #2.5 gzStreamExpander()
// Requires: gz stream (file, http) + stream write callback function
bool test_gzStreamExpander()
{
  bool ret = false;
  const char* gzFile = "/gz_example.jpg.gz";
  myPackage.folder = "/"; // for md5 tests

  SerialPrintCentered("Testing gzExpander", false, true );

  GzUnpacker *GZUnpacker = new GzUnpacker();
  GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  GZUnpacker->setGzMessageCallback( myTarMessageCallback/*BaseUnpacker::targzPrintLoggerCallback*/ );

  #ifdef ESP32
  GZUnpacker->setPsram( true );
  #endif

  GZUnpacker->setStreamWriter( myStreamWriter ); // puke all data in the serial console

  fs::File file = sourceFS.open( gzFile, "r" );

  if (!file) {
    Serial.println( OpenLine );
    SerialPrintfCentered("Can't open file");
    Serial.println( CloseLine );
    while(1) { yield(); }
  }
  Stream *streamptr = &file;

  if( !GZUnpacker->gzStreamExpander( streamptr, file.size() ) ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("test_gzStreamExpander failed with return code #%d", GZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}


// Test #3: tarGzExpander() with intermediate file
// Requires: targz file, no filename requirements
bool test_tarGzExpander()
{
  bool ret = false;
  const char* tarGzFile = "/targz_example.tar.gz";
  myPackage.folder = "/tmp"; // for md5 tests

  SerialPrintCentered("Testing tarGzExpander with intermediate file", false, true );

  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

  TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  #if defined ESP32
    TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
  #endif
  #if defined ESP8266
    TARGZUnpacker->setTarVerify( false ); // true = enables health checks but slows down the overall process
  #endif
  TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
  TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
  TARGZUnpacker->setTarMessageCallback( myTarMessageCallback/*BaseUnpacker::targzPrintLoggerCallback*/ ); // tar log verbosity

  // include/exclude filters, can be set/omitted both or separately
  TARGZUnpacker->setTarExcludeFilter( myTarExcludeFilter ); // will ignore files/folders
  TARGZUnpacker->setTarIncludeFilter( myTarIncludeFilter ); // will allow files/folders

  #ifdef ESP32
  TARGZUnpacker->setPsram( true );
  #endif

  if( !TARGZUnpacker->tarGzExpander(sourceFS, tarGzFile, tarGzFS, myPackage.folder ) ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("tarGzExpander+intermediate file failed with return code #%d", TARGZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}



// Test #4: tarGzExpander() without intermediate file
// Requires: targz file, no filename requirements
bool test_tarGzExpander_no_intermediate()
{
  bool ret = false;
  const char* tarGzFile = "/targz_example.tar.gz";
  myPackage.folder = "/tmp"; // for md5 tests

  SerialPrintCentered("Testing tarGzExpander without intermediate file", false, true );

  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

  TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  #if defined ESP32
    TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
  #endif
  #if defined ESP8266
    TARGZUnpacker->setTarVerify( false ); // true = enables health checks but slows down the overall process
    //TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::targzNullLoggerCallback ); // print the filenames as they're expanded
  #endif

  TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
  TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
  TARGZUnpacker->setTarMessageCallback( myTarMessageCallback/*BaseUnpacker::targzPrintLoggerCallback*/ ); // tar log verbosity

  #ifdef ESP32
  TARGZUnpacker->setPsram( true );
  #endif

  Serial.println("Testing tarGzExpander without intermediate file");
  if( !TARGZUnpacker->tarGzExpander(sourceFS, tarGzFile, tarGzFS, myPackage.folder, nullptr ) ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("tarGzExpander direct expanding failed with return code #%d", TARGZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
    #if defined ESP8266
    //TARGZUnpacker->tarGzListDir( tarGzFS, myPackage.folder, 3 );
    #endif
  }
  return ret;
}



// Test #5: gzUpdater() and gzStreamUpdater()
// Requires: gzipped firmware file, filename must end with ".gz"
bool test_gzUpdater()
{
  bool ret = false;
  #if defined ESP32
    const char* firmwareFile = "/firmware_example_esp32.gz";
  #endif
  #if defined ESP8266
    const char* firmwareFile = "/firmware_example_esp8266.gz";
  #endif

  SerialPrintCentered("Testing gzUpdater / gzStreamUpdater", false, true );

  GzUnpacker *GZUnpacker = new GzUnpacker();

  GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity

  #ifdef ESP32
  GZUnpacker->setPsram( true );
  #endif

  if( ! GZUnpacker->gzUpdater( sourceFS, firmwareFile, U_FLASH, /*restart on update*/false ) ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("gzUpdater failed with return code #%d", GZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}


// NOT (yet?) working on ESP8266
// Test #6 tarGzStreamExpander()
// Requires: a valid stream (http or file) to a targz file
bool test_tarGzStreamExpander()
{
  bool ret = false;
  const char* tarGzFile = "/targz_example.tar.gz";
  myPackage.folder = "/"; // for md5 tests

  SerialPrintCentered("Testing tarGzStreamExpander", false, true );

  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

  TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  #if defined ESP32
    TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
  #endif
  #if defined ESP8266
    TARGZUnpacker->setTarVerify( false ); // true = enables health checks but slows down the overall process
  #endif
  TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
  TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
  TARGZUnpacker->setTarMessageCallback( myTarMessageCallback/*BaseUnpacker::targzPrintLoggerCallback*/ ); // tar log verbosity

  #ifdef ESP32
  TARGZUnpacker->setPsram( true );
  #endif


  #if __has_include(<PSRamFS.h>)
    TARGZUnpacker->setTarVerify( false ); // disable health checks when working with an exotic stream :-)
    RomDiskStream memoryStream( data_targz_example_tar_gz, data_targz_example_tar_gz_len );
    Stream *streamptr = &memoryStream;
  #else
    fs::File file = sourceFS.open( tarGzFile, "r" );

    if (!file) {
      Serial.println( OpenLine );
      SerialPrintfCentered("Can't open file");
      Serial.println( CloseLine );
      while(1) { yield(); }
    }
    Stream *streamptr = &file;
  #endif

  if( !TARGZUnpacker->tarGzStreamExpander( streamptr, tarGzFS ) ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("tarGzStreamExpander failed with return code #%d", TARGZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}



#if defined ESP32

// this is arbitrary, the partitions_bundle_esp32.tar.gz file contains a spiffs.bin partition
// but it should also work with mklittlefs.bin partitions
#include <SPIFFS.h>

bool test_tarGzStreamUpdater()
{
  bool ret = false;
  #if defined ESP32
    const char* bundleGzFile = "/partitions_bundle_esp32.tar.gz"; // archive containing both partitions for app and spiffs
  #endif
  #if defined ESP8266
    // unsupported yet
    return true;
  #endif

  SerialPrintCentered("Testing tarGzStreamUpdater", false, true );

  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();
  TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  TARGZUnpacker->setTarVerify( false ); // nothing to verify as we're writing a partition
  // TARGZUnpacker->setupFSCallbacks( nullptr, nullptr ); // Update.h already takes care of that
  TARGZUnpacker->setGzProgressCallback( BaseUnpacker::targzNullProgressCallback ); // don't care about gz progress
  TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual partition
  TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
  TARGZUnpacker->setTarMessageCallback( myTarMessageCallback/*BaseUnpacker::targzPrintLoggerCallback*/ ); // tar log verbosity

  #ifdef ESP32
  TARGZUnpacker->setPsram( true );
  #endif

  SerialPrintCentered("Pre formattings SPIFFS", true, false );

  SPIFFS.format();

  SerialPrintCentered("Done!", false, true );

  fs::File file = sourceFS.open( bundleGzFile, "r" );
  if (!file) {
    Serial.println( OpenLine );
    SerialPrintfCentered("Can't open file");
    Serial.println( CloseLine );
    while(1) { yield(); }
  }
  Stream *streamptr = &file;
  if( !TARGZUnpacker->tarGzStreamUpdater( streamptr ) ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("tarGzStreamUpdater failed with return code #%d", TARGZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}


#endif







void setup()
{
  Serial.begin( 115200 );
  EEPROM.begin(512);

  #ifdef ESP8266
    // WTF on ESP8266 you must include <user_interface.h> and load WiFi in order not to use WiFi :-(
    wifi_station_disconnect();
    wifi_set_opmode(NULL_MODE);
    // now let's do the tests without the interferences of wifiscan or dhcp lease
    int max_tests = 6;
  #else
    int max_tests = 7;
  #endif

  delay(1000);
  Serial.println();

  uint8_t testNum = EEPROM.read(0);
  if( isManualReset() || testNum == 255 ) testNum = 0;

  SerialPrintCentered("ESP32-Targz Test Suite", true);
  Serial.println( MiddleLine );

  #if defined DEST_FS_USES_SD
    #if defined ESP8266
      SDFSConfig sdConf(4, SD_SCK_MHZ(20) );
      tarGzFS.setConfig(sdConf);
      if (!tarGzFS.begin())
    #else // ESP32 specific SD settings
      // if (!tarGzFS.begin( TFCARD_CS_PIN, 16000000 ))
      if (!tarGzFS.begin())
    #endif
  #else // LITTLEFS or SPIFFS
    //tarGzFS.format();
    if (!tarGzFS.begin())
  #endif
  {
    SerialPrintfCentered("%s Mount Failed, halting", FS_NAME );
    while(1) yield();
  } else {
    SerialPrintfCentered("%s Mount Successful", FS_NAME);
  }
  #if defined SOURCE_AND_DEST_DIFFER
    if (!sourceFS.begin())
    {
      SerialPrintfCentered("%s Mount Failed, halting", SOURCE_FS_NAME );
      while(1) yield();
    } else {
      SerialPrintfCentered("%s Mount Successful", SOURCE_FS_NAME);
    }
  #endif

  if( testNum < max_tests ) {
    Serial.println( MiddleLine );
    SerialPrintfCentered("System Available heap: %d bytes", ESP.getFreeHeap() );
    Serial.println( MiddleLine );

    SerialPrintfCentered("ESP ready for test #%d", testNum+1 );
    Serial.println( MiddleLine );
  }

  bool test_succeeded = false;
  bool tests_finished = false;

  // save the current test-suite progress in EEPROM before it eventually crashes
  EEPROM.write(0, testNum+1 );
  EEPROM.commit();

  switch( testNum )
  {
    case 0: test_succeeded = test_tarExpander(); break;
    case 1: test_succeeded = test_gzExpander() /*&& test_gzStreamExpander()*/; break;
    case 2: test_succeeded = test_tarGzExpander(); break;
    case 3: test_succeeded = test_tarGzExpander_no_intermediate(); break;
    case 4: test_succeeded = test_tarGzStreamExpander(); break;
    case 5: test_succeeded = test_gzUpdater(); break;
    #if defined ESP32 && !__has_include(<PSRamFS.h>)
    case 6: test_succeeded = test_tarGzStreamUpdater(); break;
    #endif
    default:
      tests_finished = true;
    break;
  }

  if( tests_finished ) {
    EEPROM.write(0, 0 );
    EEPROM.commit();
    #if !__has_include(<PSRamFS.h>)
    BaseUnpacker *Unpacker = new BaseUnpacker();
    SPIFFS.begin();
      Unpacker->tarGzListDir( SPIFFS, "/", 3 );
    #endif
    SerialPrintCentered("All tests performed, press reset to restart", false, true );
  }

  if( test_succeeded ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("Test #%d succeeded, will restart to proceed to the next test", testNum+1);
    Serial.println( CloseLine );
    // go on with next test
    ESP.restart();
  }

}



void loop()
{
}
