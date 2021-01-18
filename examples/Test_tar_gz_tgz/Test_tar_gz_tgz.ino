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

#include <EEPROM.h>
#if defined ESP8266
  extern "C" {
    #include <user_interface.h>
  }
#endif
#if defined ESP32
  #include <rom/rtc.h>
#endif





bool isManualReset()
{
  bool ret = false;

  #if defined ESP8266
    rst_info *resetInfo = ESP.getResetInfoPtr();
    switch( resetInfo->reason )
    {
      case REASON_DEFAULT_RST:      log_d("Reset reason:%s\n", "REASON_DEFAULT_RST");      ret = false; break; // = 0, /* normal startup by power on */
      case REASON_WDT_RST:          log_d("Reset reason:%s\n", "REASON_WDT_RST");          ret = false; break; // = 1, /* hardware watch dog reset */
      case REASON_EXCEPTION_RST:    log_d("Reset reason:%s\n", "REASON_EXCEPTION_RST");    ret = false; break; // = 2, /* exception reset, GPIO status won't change */
      case REASON_SOFT_WDT_RST:     log_d("Reset reason:%s\n", "REASON_SOFT_WDT_RST");     ret = false; break; // = 3, /* software watch dog reset, GPIO status won't change */
      case REASON_SOFT_RESTART:     log_d("Reset reason:%s\n", "REASON_SOFT_RESTART");     ret = false; break; // = 4, /* ESP.restart() , GPIO status won't change */
      case REASON_DEEP_SLEEP_AWAKE: log_d("Reset reason:%s\n", "REASON_DEEP_SLEEP_AWAKE"); ret = false; break; // = 5, /* wake up from deep-sleep */
      case REASON_EXT_SYS_RST:      log_d("Reset reason:%s\n", "REASON_EXT_SYS_RST");      ret = true;  break; // = 6 /* external system reset, like after flashing the ESP */
      default: break;
    }
  #endif

  #if defined ESP32
    switch ( rtc_get_reset_reason(0) )
    {
      case 1 :  log_d("Reset reason:%s\n", "POWERON_RESET");          ret = true;  break;/**<1, Vbat power on reset*/
      case 3 :  log_d("Reset reason:%s\n", "SW_RESET");               ret = false; break;/**<3, Software reset digital core*/
      case 4 :  log_d("Reset reason:%s\n", "OWDT_RESET");             ret = false; break;/**<4, Legacy watch dog reset digital core*/
      case 5 :  log_d("Reset reason:%s\n", "DEEPSLEEP_RESET");        ret = false; break;/**<5, Deep Sleep reset digital core*/
      case 6 :  log_d("Reset reason:%s\n", "SDIO_RESET");             ret = false; break;/**<6, Reset by SLC module, reset digital core*/
      case 7 :  log_d("Reset reason:%s\n", "TG0WDT_SYS_RESET");       ret = false; break;/**<7, Timer Group0 Watch dog reset digital core*/
      case 8 :  log_d("Reset reason:%s\n", "TG1WDT_SYS_RESET");       ret = false; break;/**<8, Timer Group1 Watch dog reset digital core*/
      case 9 :  log_d("Reset reason:%s\n", "RTCWDT_SYS_RESET");       ret = false; break;/**<9, RTC Watch dog Reset digital core*/
      case 10 : log_d("Reset reason:%s\n", "INTRUSION_RESET");        ret = false; break;/**<10, Instrusion tested to reset CPU*/
      case 11 : log_d("Reset reason:%s\n", "TGWDT_CPU_RESET");        ret = false; break;/**<11, Time Group reset CPU*/
      case 12 : log_d("Reset reason:%s\n", "SW_CPU_RESET");           ret = false; break;/**<12, Software reset CPU*/
      case 13 : log_d("Reset reason:%s\n", "RTCWDT_CPU_RESET");       ret = false; break;/**<13, RTC Watch dog Reset CPU*/
      case 14 : log_d("Reset reason:%s\n", "EXT_CPU_RESET");          ret = false; break;/**<14, for APP CPU, reseted by PRO CPU*/
      case 15 : log_d("Reset reason:%s\n", "RTCWDT_BROWN_OUT_RESET"); ret = false; break;/**<15, Reset when the vdd voltage is not stable*/
      case 16 : log_d("Reset reason:%s\n", "RTCWDT_RTC_RESET");       ret = false; break;/**<16, RTC Watch dog reset digital core and rtc module*/
      default : log_d("Reset reason:%s\n", "NO_MEAN");
    }
  #endif

  return ret;
}

// this sketch only serves compilation test purpose

const char* wtfUnicode = "********************************************************************";
const char* OpenLine   = "┌──────────────────────────────────────────────────────────────────┐";
const char* MiddleLine = "├──────────────────────────────────────────────────────────────────┤";
const char* CloseLine  = "└──────────────────────────────────────────────────────────────────┘";


void SerialPrintCentered(const char *s, bool open = false, bool close = false ) {
  const char* leftStr  = "| ";
  const char* rightStr = " |";
  uint8_t maxWidth   = strlen(wtfUnicode)+1;
  uint8_t availWidth = maxWidth - ( strlen(leftStr)-1 + strlen(rightStr)-1 );
  uint8_t strWidth   = strlen(s);
  uint8_t padwidth   = availWidth/2;

  //log_w("max: %d, avail: %d, w: %d, p: %d", maxWidth, availWidth, strWidth, padwidth );

  int left  = padwidth+strWidth/2;
  int right = padwidth-strWidth/2;

  if( strWidth%2 != 0 ) {
    right--;
    left++;
  }

  char *out = new char[maxWidth];
  snprintf( out, maxWidth, "%s%*s%*s%s", leftStr, left, s, right, rightStr, "" );

  if( open )  Serial.println( OpenLine );
  Serial.println( out );
  if( close ) Serial.println( CloseLine );
  delete out;
}

void SerialPrintfCentered(const char* format, ... )
{
  char out[255] = {0};
  va_list args;
  va_start(args, format);
  vsnprintf( out, 255, format, args);
  SerialPrintCentered( out, false, false );
  va_end(args);
}


bool test_tarExpander()
{
  bool ret = false;

  SerialPrintCentered("Testing tarExpander", false, true );
  TarUnpacker *TARUnpacker = new TarUnpacker();
  TARUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  TARUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
  TARUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  //TARUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  //TARUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  TARUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
  TARUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
  TARUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity
  if(  !TARUnpacker->tarExpander(tarGzFS, "/tar_example.tar", tarGzFS, "/") ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("tarExpander failed with return code #%d", TARUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}



bool test_gzExpander()
{
  bool ret = false;
  SerialPrintCentered("Testing gzExpander", false, true );
  GzUnpacker *GZUnpacker = new GzUnpacker();
  GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  if( !GZUnpacker->gzExpander(tarGzFS, "/gz_example.gz", tarGzFS, "/tbz.jpg") ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("gzExpander failed with return code #%d", GZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}



bool test_tarGzExpander()
{
  bool ret = false;
  SerialPrintCentered("Testing tarGzExpander with intermediate file", false, true );
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
    Serial.println( OpenLine );
    SerialPrintfCentered("tarGzExpander+intermediate file failed with return code #%d", TARGZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}

bool test_tarGzExpander_no_intermediate()
{
  bool ret = false;
  SerialPrintCentered("Testing tarGzExpander without intermediate file", false, true );
  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();
  TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
#ifdef ESP32
  TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
#else
  TARGZUnpacker->setTarVerify( false ); // true = enables health checks but slows down the overall process
#endif
  TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
  TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
  TARGZUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity
  Serial.println("Testing tarGzExpander without intermediate file");
  if( !TARGZUnpacker->tarGzExpander(tarGzFS, "/targz_example.tar.gz", tarGzFS, "/tmp", nullptr ) ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("tarGzExpander+intermediate file failed with return code #%d", TARGZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}



bool test_tarGzStreamExpander()
{
  bool ret = false;
  SerialPrintCentered("Testing tarGzStreamExpander", false, true );
  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();
  TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
#ifdef ESP32
  TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
#else
  TARGZUnpacker->setTarVerify( false ); // true = enables health checks but slows down the overall process
#endif
  TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  TARGZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  TARGZUnpacker->setTarProgressCallback( BaseUnpacker::defaultProgressCallback ); // prints the untarring progress for each individual file
  TARGZUnpacker->setTarStatusProgressCallback( BaseUnpacker::defaultTarStatusProgressCallback ); // print the filenames as they're expanded
  TARGZUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity
  fs::File file = tarGzFS.open( "/targz_example.tar.gz", "r" );
  if (!file) {
    Serial.println( OpenLine );
    SerialPrintfCentered("Can't open file");
    Serial.println( CloseLine );
    while(1) { yield(); }
  }
  Stream *streamptr = &file;
  if( !TARGZUnpacker->tarGzStreamExpander( streamptr, tarGzFS ) ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("tarGzStreamExpander failed with return code #%d", TARGZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}


//#if defined ESP32
bool test_gzStreamUpdater()
{
  bool ret = false;
  SerialPrintCentered("Testing gzStreamUpdater", false, true );
  GzUnpacker *GZUnpacker = new GzUnpacker();
  GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  fs::File file = tarGzFS.open( "/firmware_example_esp32.gz", "r" );
  if (!file) {
    Serial.println( OpenLine );
    SerialPrintfCentered("Can't open file");
    Serial.println( CloseLine );
    while(1) { yield(); }
  }
  Stream *streamptr = &file;
  if( streamptr == nullptr ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("Failed to establish http connection");
    Serial.println( CloseLine );
    while(1) { yield(); }
  }
  #ifdef ESP32
  size_t streamSize = UPDATE_SIZE_UNKNOWN;
  #else
  size_t streamSize = 265952;// this is the UNcompressed size, not the gzip size
  #endif

  if( !GZUnpacker->gzStreamUpdater( streamptr, streamSize ) ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("gzHTTPUpdater failed with return code #%d", GZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}
//#endif


bool test_gzUpdater()
{
  bool ret = false;
  SerialPrintCentered("Testing gzUpdater", false, true );
  GzUnpacker *GZUnpacker = new GzUnpacker();
  GZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  GZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  GZUnpacker->setGzProgressCallback( BaseUnpacker::defaultProgressCallback ); // targzNullProgressCallback or defaultProgressCallback
  GZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  #if defined ESP32
  const char* firmwareFile = "/firmware_example_esp32.gz";
  #elif defined ESP8266
  const char* firmwareFile = "/firmware_example_esp8266.gz";
  #endif
  if( ! GZUnpacker->gzUpdater(tarGzFS, firmwareFile, /*don't restart after update*/false ) ) {
    Serial.println( OpenLine );
    SerialPrintfCentered("gzUpdater failed with return code #%d", GZUnpacker->tarGzGetError() );
    Serial.println( CloseLine );
  } else {
    ret = true;
  }
  return ret;
}



void setup()
{



  Serial.begin( 115200 );
  EEPROM.begin(512);
  delay(1000);
  Serial.println();

  uint8_t testNum = EEPROM.read(0);
  if( isManualReset() || testNum == 255 ) testNum = 0;

  SerialPrintCentered("ESP32-Targz Test Suite", true);
  Serial.println( MiddleLine );

  if( testNum < 7 ) {
    //Serial.println( OpenLine );
    if (!tarGzFS.begin()) {
      SerialPrintfCentered("Filesystem Mount Failed, halting");
      while(1) yield();
    } else {
      SerialPrintfCentered("Filesystem Mount Successful");
    }
    Serial.println( MiddleLine );
    SerialPrintfCentered("System Available heap: %d bytes", ESP.getFreeHeap() );
    Serial.println( MiddleLine );

    SerialPrintfCentered("ESP ready for test #%d", testNum+1 );
    Serial.println( MiddleLine );
  }

  bool test_succeeded = false;
  bool tests_finished = false;

  EEPROM.write(0, testNum+1 );
  EEPROM.commit();

  switch( testNum )
  {
    case 0: test_succeeded = test_tarExpander(); break;
    case 1: test_succeeded = test_gzExpander(); break;
    case 2: test_succeeded = test_tarGzExpander(); break;
    case 3: test_succeeded = test_tarGzExpander_no_intermediate(); break;
    case 4: test_succeeded = test_gzUpdater(); break;
    #if defined ESP32
    // HALP: those tests are still failing on ESP8266
    case 5: test_succeeded = test_tarGzStreamExpander(); break;
    case 6: test_succeeded = test_gzStreamUpdater(); break;
    #endif
    default:
      tests_finished = true;
    break;
  }

  if( tests_finished ) {
    SerialPrintCentered("All tests performed, press reset to restart", false, true );
    EEPROM.write(0, 0 );
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
