#pragma once
/*
 *
 * ESP32-yaml
 * Project Page: https://github.com/tobozo/esp32-yaml
 *
 * Copyright 2022 tobozo http://github.com/tobozo
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files ("ESP32-yaml"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

// Emit logs in arduino style at software level rather than firmware level

#pragma once


#if defined ESP8266 || defined ESP32
  // those have OTA and common device API
  #define HEAP_AVAILABLE() ESP.getFreeHeap()
  #define DEVICE_RESTART() ESP.restart()

  #ifdef ESP8266
    // some ESP32 => ESP8266 syntax shim

    #define U_PART U_FS
    #define ARDUHAL_LOG_FORMAT(letter, format)  "[" #letter "][%s:%u] %s(): " format "\r\n", __FILE__, __LINE__, __FUNCTION__

    #if defined DEBUG_ESP_PORT
      // always show errors/warnings when debug port is there
      #define log_n(format, ...) DEBUG_ESP_PORT.printf(ARDUHAL_LOG_FORMAT(N, format), ##__VA_ARGS__);
      #define log_e(format, ...) DEBUG_ESP_PORT.printf(ARDUHAL_LOG_FORMAT(E, format), ##__VA_ARGS__);
      #define log_w(format, ...) DEBUG_ESP_PORT.printf(ARDUHAL_LOG_FORMAT(W, format), ##__VA_ARGS__);

      #if defined DEBUG_ESP_CORE
        // be verbose
        #define log_i(format, ...) DEBUG_ESP_PORT.printf(ARDUHAL_LOG_FORMAT(I, format), ##__VA_ARGS__);
        #define log_d(format, ...) DEBUG_ESP_PORT.printf(ARDUHAL_LOG_FORMAT(D, format), ##__VA_ARGS__);
        #define log_v(format, ...) DEBUG_ESP_PORT.printf(ARDUHAL_LOG_FORMAT(V, format), ##__VA_ARGS__);
      #else
        // don't be verbose, only errors+warnings
        #define log_i BaseUnpacker::targzNullLoggerCallback
        #define log_d BaseUnpacker::targzNullLoggerCallback
        #define log_v BaseUnpacker::targzNullLoggerCallback
      #endif

    #else
      #define log_n BaseUnpacker::targzNullLoggerCallback
      #define log_e BaseUnpacker::targzNullLoggerCallback
      #define log_w BaseUnpacker::targzNullLoggerCallback
      #define log_i BaseUnpacker::targzNullLoggerCallback
      #define log_d BaseUnpacker::targzNullLoggerCallback
      #define log_v BaseUnpacker::targzNullLoggerCallback
    #endif

  #else
    #define U_PART U_SPIFFS
  #endif

#elif defined ARDUINO_ARCH_RP2040
  // no OTA support
  #define DEVICE_RESTART() rp2040.restart()
  #define HEAP_AVAILABLE() rp2040.getFreeHeap()

  // ESP like log functions turned to macros to allow gathering of file name, log level, etc
  #define log_v(format, ...) TGZ::LOG(__FILE__, __LINE__, TGZ::LogLevelVerbose, format, ##__VA_ARGS__)
  #define log_d(format, ...) TGZ::LOG(__FILE__, __LINE__, TGZ::LogLevelDebug,   format, ##__VA_ARGS__)
  #define log_i(format, ...) TGZ::LOG(__FILE__, __LINE__, TGZ::LogLevelInfo,    format, ##__VA_ARGS__)
  #define log_w(format, ...) TGZ::LOG(__FILE__, __LINE__, TGZ::LogLevelWarning, format, ##__VA_ARGS__)
  #define log_e(format, ...) TGZ::LOG(__FILE__, __LINE__, TGZ::LogLevelError,   format, ##__VA_ARGS__)
  #define log_n(format, ...) TGZ::LOG(__FILE__, __LINE__, TGZ::LogLevelNone,    format, ##__VA_ARGS__)

  #include <Arduino.h>
  #define LOG_PRINTF Serial.printf

  #if !defined TGZ_PATHNAME
    #define TGZ_PATHNAME _pathToFileName
    static const char * _pathToFileName(const char * path)
    {
      size_t i = 0, pos = 0;
      char * p = (char *)path;
      while(*p){
        i++;
        if(*p == '/' || *p == '\\'){
          pos = i;
        }
        p++;
      }
      return path+pos;
    }
  #endif

  #if !defined LOG_PRINTF
    #define LOG_PRINTF printf
  #endif

  #if !defined TGZ_DEFAULT_LOG_LEVEL
    #define TGZ_DEFAULT_LOG_LEVEL LogLevelWarning
  #endif

  namespace TGZ
  {
    // maximum size of log string
    #define LOG_MAXLENGTH 215
    #define TGZ_LOGGER_attr __attribute__((unused)) static

    // logger function signature
    typedef void (*TGZ_LOGGER_t)(const char* path, int line, int loglevel, const char* fmr, ...);

    // supported log levels, inspired from esp32 arduhal
    enum LogLevel_t
    {
      LogLevelNone,    // no logging
      LogLevelError,   // err
      LogLevelWarning, // err+warn
      LogLevelInfo,    // err+warn+info
      LogLevelDebug,   // err+warn+info+debug
      LogLevelVerbose  // err+warn+info+debug+verbose
    };
    // log levels names
    TGZ_LOGGER_attr const char* levelNames[6] = {"None","Error","Warning","Info","Debug","Verbose"};
    // the default logging function
    TGZ_LOGGER_attr void _LOG(const char* path, int line, int loglevel, const char* fmr, ...);
    // the pointer to the logging function (can be overloaded with a custom logger)
    TGZ_LOGGER_attr void (*LOG)(const char* path, int line, int loglevel, const char* fmr, ...) = _LOG;
    // log level setter
    TGZ_LOGGER_attr void setLogLevel( LogLevel_t level );
    // the logging function setter
    TGZ_LOGGER_attr void setLoggerFunc( TGZ_LOGGER_t fn );
    // default log level
    TGZ_LOGGER_attr LogLevel_t _LOG_LEVEL = TGZ_DEFAULT_LOG_LEVEL;
    // log level getter (int)
    TGZ_LOGGER_attr LogLevel_t logLevelInt();
    // log level getter (string)
    TGZ_LOGGER_attr const char* logLevelStr();

    LogLevel_t logLevelInt()
    {
      return _LOG_LEVEL;
    }

    const char* logLevelStr()
    {
      return levelNames[_LOG_LEVEL];
    }

    void setLogLevel( LogLevel_t level )
    {
      TGZ::_LOG_LEVEL = level;
      log_n("New log level: %d", level );
    }

    void setLoggerFunc( TGZ_LOGGER_t fn )
    {
      LOG = fn;
    }

    void _LOG(const char* path, int line, int loglevel, const char* fmr, ...)
    {
      if( loglevel <= TGZ::_LOG_LEVEL ) {
        using namespace TGZ;
        char log_buffer[LOG_MAXLENGTH+1] = {0};
        va_list arg;
        va_start(arg, fmr);
        vsnprintf(log_buffer, LOG_MAXLENGTH, fmr, arg);
        va_end(arg);
        if( log_buffer[0] != '\0' ) {
          switch( loglevel ) {
            case LogLevelVerbose: LOG_PRINTF("[V][%d][%s:%d] %s\r\n", HEAP_AVAILABLE(), TGZ_PATHNAME(path), line, log_buffer); break;
            case LogLevelDebug:   LOG_PRINTF("[D][%d][%s:%d] %s\r\n", HEAP_AVAILABLE(), TGZ_PATHNAME(path), line, log_buffer); break;
            case LogLevelInfo:    LOG_PRINTF("[I][%d][%s:%d] %s\r\n", HEAP_AVAILABLE(), TGZ_PATHNAME(path), line, log_buffer); break;
            case LogLevelWarning: LOG_PRINTF("[W][%d][%s:%d] %s\r\n", HEAP_AVAILABLE(), TGZ_PATHNAME(path), line, log_buffer); break;
            case LogLevelError:   LOG_PRINTF("[E][%d][%s:%d] %s\r\n", HEAP_AVAILABLE(), TGZ_PATHNAME(path), line, log_buffer); break;
            case LogLevelNone:    LOG_PRINTF("[N][%d][%s:%d] %s\r\n", HEAP_AVAILABLE(), TGZ_PATHNAME(path), line, log_buffer); break;
          }
        }
      }
    }

  };

#else

  #error "Only ESP32, ESP8266 and RP2040 architectures are supported"

#endif
