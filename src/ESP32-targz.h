/*\

  MIT License

  Copyright (c) 2020 tobozo

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  ESP32-tgz is a wrapper to uzlib.h and untar.h third party libraries.
  Those libraries have been adapted and/or modified to fit this project's needs
  and are bundled with their initial license files.

  - uzlib: https://github.com/pfalcon/uzlib
  - untar: https://github.com/dsoprea/TinyUntar

\*/

#ifndef _ESP_TGZ_H
#define _ESP_TGZ_H

#include <FS.h>

#if defined( ESP32 )
  #include <SPIFFS.h>
  #include <SD.h>
  #include <SD_MMC.h>
  #include <Update.h>
#elif defined( ESP8266 )
  #include "spiffs/spiffs.h"
  #include <LittleFS.h>
  #include <Updater.h>
  #define log_e tgzLogger
  #define log_w tgzLogger
  #define log_d tgzNullLogger
  #define log_v tgzNullLogger
#else
  #error Unsupported architecture
#endif


// unzip sourceFS://sourceFile.tar.gz contents into destFS://destFolder
int tarGzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFolder="/tmp" );
// unpack sourceFS://fileName.tar contents to destFS::/destFolder/
int tarExpander( fs::FS &sourceFS, const char* fileName, fs::FS &destFS, const char* destFolder );
// checks if gzFile is a valid gzip file
bool readGzHeaders(fs::File &gzFile);
// extract 4K of data from gzip
int gzProcessBlock();
// uncompresses *gzipped* sourceFile to destFile, filesystems may differ
void gzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFile );
// flashes the ESP with the content of a *gzipped* file
void gzUpdater( fs::FS &fs, const char* gz_filename );
// naive ls
void tarGzListDir( fs::FS &fs, const char * dirName, uint8_t levels=1 );
// fs helper
char *dirname(char *path);
// useful to share the buffer so it's not totally wasted memory outside targz scope
uint8_t *getGzBufferUint8();



#endif // #ifdef _ESP_TGZ_H
