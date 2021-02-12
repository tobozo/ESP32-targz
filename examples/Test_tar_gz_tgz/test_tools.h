// using EEPROM to store test-suite progress
#include <EEPROM.h>
// using reset reason to avoid infinite loop
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
      case 0:  log_d("Reset reason:%s\n", "REASON_DEFAULT_RST");      ret = false; break; // = 0, /* normal startup by power on */
      case 1:  log_d("Reset reason:%s\n", "REASON_WDT_RST");          ret = false; break; // = 1, /* hardware watch dog reset */
      case 2:  log_d("Reset reason:%s\n", "REASON_EXCEPTION_RST");    ret = false; break; // = 2, /* exception reset, GPIO status won't change */
      case 3:  log_d("Reset reason:%s\n", "REASON_SOFT_WDT_RST");     ret = false; break; // = 3, /* software watch dog reset, GPIO status won't change */
      case 4:  log_d("Reset reason:%s\n", "REASON_SOFT_RESTART");     ret = false; break; // = 4, /* ESP.restart() , GPIO status won't change */
      case 5:  log_d("Reset reason:%s\n", "REASON_DEEP_SLEEP_AWAKE"); ret = false; break; // = 5, /* wake up from deep-sleep */
      case 6:  log_d("Reset reason:%s\n", "REASON_EXT_SYS_RST");      ret = true;  break; // = 6 /* external system reset, like after flashing the ESP */
      default: log_d("Reset reason:%s\n", "NO_MEAN");
    }
  #endif

  #if defined ESP32
    switch ( rtc_get_reset_reason(0) )
    {
      case 1:  log_d("Reset reason:%s\n", "POWERON_RESET");           ret = true;  break;/**<1, Vbat power on reset*/
      case 3:  log_d("Reset reason:%s\n", "SW_RESET");                ret = false; break;/**<3, Software reset digital core*/
      case 4:  log_d("Reset reason:%s\n", "OWDT_RESET");              ret = false; break;/**<4, Legacy watch dog reset digital core*/
      case 5:  log_d("Reset reason:%s\n", "DEEPSLEEP_RESET");         ret = false; break;/**<5, Deep Sleep reset digital core*/
      case 6:  log_d("Reset reason:%s\n", "SDIO_RESET");              ret = false; break;/**<6, Reset by SLC module, reset digital core*/
      case 7:  log_d("Reset reason:%s\n", "TG0WDT_SYS_RESET");        ret = false; break;/**<7, Timer Group0 Watch dog reset digital core*/
      case 8:  log_d("Reset reason:%s\n", "TG1WDT_SYS_RESET");        ret = false; break;/**<8, Timer Group1 Watch dog reset digital core*/
      case 9:  log_d("Reset reason:%s\n", "RTCWDT_SYS_RESET");        ret = false; break;/**<9, RTC Watch dog Reset digital core*/
      case 10: log_d("Reset reason:%s\n", "INTRUSION_RESET");         ret = false; break;/**<10, Instrusion tested to reset CPU*/
      case 11: log_d("Reset reason:%s\n", "TGWDT_CPU_RESET");         ret = false; break;/**<11, Time Group reset CPU*/
      case 12: log_d("Reset reason:%s\n", "SW_CPU_RESET");            ret = false; break;/**<12, Software reset CPU*/
      case 13: log_d("Reset reason:%s\n", "RTCWDT_CPU_RESET");        ret = false; break;/**<13, RTC Watch dog Reset CPU*/
      case 14: log_d("Reset reason:%s\n", "EXT_CPU_RESET");           ret = false; break;/**<14, for APP CPU, reseted by PRO CPU*/
      case 15: log_d("Reset reason:%s\n", "RTCWDT_BROWN_OUT_RESET");  ret = false; break;/**<15, Reset when the vdd voltage is not stable*/
      case 16: log_d("Reset reason:%s\n", "RTCWDT_RTC_RESET");        ret = false; break;/**<16, RTC Watch dog reset digital core and rtc module*/
      default: log_d("Reset reason:%s\n", "NO_MEAN");
    }
  #endif

  return ret;
}

// some elements to make the test fancy in the console

// for some reason arduino sees extended ASCII as unicode so this is here for counting
const char* wtfUnicode = "********************************************************************";
// these are extended ASCII characters as Unicode
const char* OpenLine   = "┌──────────────────────────────────────────────────────────────────┐";
const char* MiddleLine = "├──────────────────────────────────────────────────────────────────┤";
const char* CloseLine  = "└──────────────────────────────────────────────────────────────────┘";

void SerialPrintCentered(const char *s, bool open = false, bool close = false )
{
  const char* leftStr  = "| ";
  const char* rightStr = " |";
  uint8_t maxWidth   = strlen(wtfUnicode)+1;
  uint8_t availWidth = maxWidth - ( strlen(leftStr)-1 + strlen(rightStr)-1 );
  uint8_t strWidth   = strlen(s);
  uint8_t padwidth   = availWidth/2;
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



struct fileMeta
{
  size_t size;
  const char* md5sum;
  const char* path;
};

struct packageMeta
{
  const char* folder;
  size_t files_count;
  fileMeta *files;
};


#include "test_files.h"

/*

// or hardcode them

fileMeta myFiles[15] =
{
  { 279200, "2297aacd9380d9b438490d6002bc83be", "firmware_example_esp32" },
  { 120188, "42ad7a973640e95f1cbfbd7b45a94ef1", "firmware_example_esp32.gz" },
  { 266496, "e5956fa1f561e8e4657bfcdd2d92b447", "firmware_example_esp8266" },
  { 196204, "774b98d8880c82a60e14040a76f636f7", "firmware_example_esp8266.gz" },
  { 145783, "0650b35fbbaff218f46388764453d951", "gz_example.jpg" },
  { 133877, "815f9a5dd4ae37915e9842b4229181ab", "gz_example.jpg.gz" },
  { 31624 , "9ba1fdface48283cbf3bedfd1046a629", "mgmadchat.jpg" },
  { 145783, "0650b35fbbaff218f46388764453d951", "Miaou-Goldwyn-Mayer.jpg" },
  { 179712, "f5b3c789ceb322f70dc28ff61efb9529", "tar_example.tar" },
  { 16787 , "7f37ba4ed2bb127a8f9e4d9ee6e3c19e", "targz_example.tar.gz" },
  { 48    , "44ffda4b76b4fd0b13b6a1cf84003488", "zombo.com/css/blah.css" },
  { 4701  , "28913d007a7127a5b009e8584640717a", "zombo.com/img/logo.png" },
  { 7172  , "cf1f1f596ab7a726334ffcee1100a912", "zombo.com/img/spinner.png" },
  { 2040  , "808a446d1885a65202fc8f7dc5fb7341", "zombo.com/index.html" },
  { 7925  , "22ed447b5be77f1bd7a8c3f08f9b702a", "zombo.com/snd/inrozxa.swf" },
};
packageMeta myPackage =
{
  nullptr, 15, myFiles
};

*/

char tmp_path[255] = {0};

void myTarMessageCallback(const char* format, ...)
{

  if( myPackage.folder == nullptr ) return;

  char *md5sum;

  va_list args;
  va_start(args, format);
  vsnprintf(tmp_path, 255, format, args);
  va_end(args);

  String filePath;
  int found = -1;
  for( size_t i=0;i<myPackage.files_count;i++ ) {
    if( strcmp( myPackage.files[i].path, tmp_path ) == 0 ) {// exact path check
      found = i;
      break;
    } else {// extended path check
      if( String( myPackage.folder ).endsWith("/") ) {
        filePath = String( myPackage.folder ) + String( myPackage.files[i].path );
      } else {
        filePath = String( myPackage.folder ) + "/" + String( myPackage.files[i].path );
      }
      if( strcmp( filePath.c_str(), tmp_path ) == 0 ) {
        found = i;
        break;
      }
    }
  }
  if( found > -1 ) {
    //delay(100);
    filePath = String( myPackage.folder ) + "/" + String( myPackage.files[found].path );
    if( !tarGzFS.exists( filePath ) ) {
      log_w("[TAR] %-16s MD5 FAIL! File can't be opened\n", tmp_path );
      return;
    }
    fs::File tarFile = tarGzFS.open( filePath, "r" );
    if( !tarGzFS.exists( filePath ) ) {
      log_w("[TAR] %-16s MD5 FAIL! File can't be reached\n", tmp_path );
      return;
    }
    size_t tarFileSize = tarFile.size();
    if( tarFileSize == 0 ) {
      log_w("[TAR] %-16s MD5 FAIL! File is empty\n", tmp_path );
      return;
    }
    md5sum = MD5Sum::fromFile( tarFile );
    tarFile.close();

    if( strcmp( md5sum, myPackage.files[found].md5sum ) == 0 ) {
      Serial.printf("[TAR] %-16s MD5 check SUCCESS!\n", tmp_path );
    } else {
      Serial.printf("[TAR] %-16s MD5 check FAIL! Expected vs Real: [ %s:%d ] / [ %s:%d ]\n", tmp_path, myPackage.files[found].md5sum, myPackage.files[found].size, md5sum, tarFileSize );
      BaseUnpacker::setGeneralError( ESP32_TARGZ_INTEGRITY_FAIL );
    }

  } else {
    Serial.printf("[TAR] %-16s can't be checked\n", tmp_path );
  }

}

