/*\
 *
 * Test_tar_gz_tgz.ino
 * Example code for ESP32-targz
 * https://github.com/tobozo/ESP32-targz
 *
\*/
// Set **destination** filesystem by uncommenting one of these:
#define DEST_FS_USES_SPIFFS
//#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only
#include <ESP32-targz.h>


// this sketch only serves compilation test purpose


void test_tarExpander()
{
  Serial.println("Testing tarExpander");
  TarUnpacker *TARUnpacker = new TarUnpacker();
  TARUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  TARUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
  TARUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  //TARUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  TARUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  TARUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
  TARUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
  TARUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity
  if(  !TARUnpacker->tarExpander(tarGzFS, "/tar_example.tar", tarGzFS, "/") ) {
    Serial.printf("tarExpander failed with return code #%d\n", TARUnpacker->tarGzGetError() );
  } else {
    Serial.println("YAY");
    TARUnpacker->tarGzListDir( tarGzFS, "/", 3 );
  }
  Serial.println("\n\n\n\n\n");
}



void test_gzExpander()
{

  Serial.println("Testing gzExpander");
  GzUnpacker *GZUnpacker = new GzUnpacker();
  GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  if( !GZUnpacker->gzExpander(tarGzFS, "/gz_example.gz", tarGzFS, "/tbz.jpg") ) {
    Serial.printf("gzExpander failed with return code #%d\n", GZUnpacker->tarGzGetError() );
  } else {
    Serial.println("YAY");
    GZUnpacker->tarGzListDir( tarGzFS, "/", 3 );
  }
  Serial.println("\n\n\n\n\n");
}



void test_tarGzExpander()
{
  Serial.println("Testing tarGzExpander with intermediate file");
  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();
  TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
  TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
  TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
  TARGZUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity
  if( !TARGZUnpacker->tarGzExpander(tarGzFS, "/targz_example.tar.gz", tarGzFS, "/tmp") ) {
    Serial.printf("tarGzExpander+intermediate file failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
  } else {
    Serial.println("YAY");
    TARGZUnpacker->tarGzListDir( tarGzFS, "/", 3 );
  }
  Serial.println("Testing tarGzExpander without intermediate file");
  if( !TARGZUnpacker->tarGzExpander(tarGzFS, "/targz_example.tar.gz", tarGzFS, "/tmp", nullptr ) ) {
    Serial.printf("tarGzExpander+intermediate file failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
  } else {
    Serial.println("YAY");
    TARGZUnpacker->tarGzListDir( tarGzFS, "/", 3 );
  }
  Serial.println("\n\n\n\n\n");
}



void test_tarGzStreamExpander()
{
  Serial.println("Testing tarGzStreamExpander");
  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();
  TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
  TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
  TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
  TARGZUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity
  fs::File file = tarGzFS.open( "/targz_example.tar.gz", "r" );
  if (!file) {
    Serial.println("Can't open file");
    while(1) { yield(); }
  }
  Stream *streamptr = &file;
  if( !TARGZUnpacker->tarGzStreamExpander( streamptr, tarGzFS ) ) {
    Serial.printf("tarGzStreamExpander failed with return code #%d", TARGZUnpacker->tarGzGetError() );
  } else {
    Serial.println("YAY");
    TARGZUnpacker->tarGzListDir( tarGzFS, "/", 3 );
  }
  Serial.println("\n\n\n\n\n");
}



void test_gzStreamUpdater()
{
  Serial.println("Testing gzStreamUpdater");
  GzUnpacker *GZUnpacker = new GzUnpacker();
  GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  fs::File file = tarGzFS.open( "/firmware_example.gz", "r" );
  //size_t streamsize = file.size();
  if (!file) {
    Serial.println("Can't open file");
    while(1) { yield(); }
  }
  Stream *streamptr = &file;
  if( streamptr == nullptr ) {
    Serial.println("Failed to establish http connection");
    while(1) { yield(); }
  }
  if( !GZUnpacker->gzStreamUpdater( streamptr, UPDATE_SIZE_UNKNOWN ) ) {
    Serial.printf("gzHTTPUpdater failed with return code #%d", GZUnpacker->tarGzGetError() );
  } else {
    Serial.println("YAY");
    GZUnpacker->tarGzListDir( tarGzFS, "/", 3 );
  }
  Serial.println("\n\n\n\n\n");
}



void test_gzUpdater()
{
  Serial.println("Testing gzUpdater");
  GzUnpacker *GZUnpacker = new GzUnpacker();
  GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  if( ! GZUnpacker->gzUpdater(tarGzFS, "/firmware_example.gz" ) ) {
    Serial.printf("gzUpdater failed with return code #%d", GZUnpacker->tarGzGetError() );
  } else {
    Serial.println("YAY");
    GZUnpacker->tarGzListDir( tarGzFS, "/", 3 );
  }
  Serial.println("\n\n\n\n\n");
}



void setup()
{

  Serial.begin( 115200 );
  Serial.println("Initializing Filesystem...");

  if (!tarGzFS.begin()) {
    Serial.println("Filesystem Mount Failed");
    while(1);
  } else {
    Serial.println("Filesystem Mount Successful");
  }


  test_tarGzStreamExpander();
  test_tarExpander();
  test_gzExpander();
  test_tarGzExpander();
  test_gzStreamUpdater();
  test_gzUpdater();

}



void loop()
{
}
