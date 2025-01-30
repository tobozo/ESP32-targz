/*\

  MIT License

  Copyright (c) 2025-now tobozo

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
  - libtar (some functions inspired by): https://repo.or.cz/libtar.git

\*/

#pragma once

#include <FS.h>
#include "../types/esp32_targz_types.h"
#include "../ESP32-targz-log.hpp"



// Work in progress: move namespaced functions to 1x Base class and 3x Polymorphic classes
//
// struct BasePacker
// {
//   BasePacker();
//   void haltOnError( bool halt );
// };
//
// struct TarPacker : virtual public BasePacker
// {
//   TarPacker();
//
//   std::vector<dir_entity_t> *dirEntities = nullptr;
//   fs::FS* fs = nullptr;
//
//   // input data
//   std::vector<dir_entity_t> dirEntities getEntities(fs::FS* fs, const char* tar_inputdir);
//   void setEntities(fs::FS* fs, std::vector<dir_entity_t> dirEntities);
//
//   // tar archive property
//   void setRoot(const char* tar_rootdir);
//
//   // output data
//   void setOutput(fs::FS*, const char* tar_output_filename);
//   void setOutputStream(Stream* stream);
//   void setOutputFile(Stream* stream);
//   void setOutputBuffer(uint8_t*buffer);
//
//   // batch process dirEntities
//   void pack_files();
//
// };
//
// struct GzPacker : virtual public BasePacker
// {
//
// };
//
// struct TarGzPacker : public TarPacker, public GzPacker
// {
//
// };




// .gz compressor (LZ77/deflate)
namespace LZPacker
{
  // buffer to stream (best compression)
  size_t compress( uint8_t* srcBuf, size_t srcBufLen, Stream* dstStream );
  // buffer to buffer (best compression)
  size_t compress( uint8_t* srcBuf, size_t srcBufLen, uint8_t** dstBufPtr );
  // stream to buffer (average compression)
  size_t compress( Stream* srcStream, size_t srcLen, uint8_t** dstBufPtr );
  // stream to stream (average compression)
  size_t compress( Stream* srcStream, size_t srcLen, Stream* dstStream );

  // TODO:
  size_t compress( Stream* srcStream, size_t srcLen, fs::FS*dstFS, const char* dstFilename );
  size_t compress( fs::FS *srcFS, const char* srcFilename, fs::FS*dstFS, const char* dstFilename );
  size_t compress( fs::FS *srcFS, const char* srcFilename, Stream* dstStream );

  // progress callback setter [](size_t bytes_read, size_t total_bytes)
  void setProgressCallBack(totalProgressCallback cb);
  void defaultProgressCallback( size_t progress, size_t total );

};



namespace TarPacker
{
  using namespace TAR;

  int pack_files(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, Stream* dstStream, const char* tar_prefix=nullptr);
  int pack_files(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, fs::FS *dstFS, const char*tar_output_file_path, const char* tar_prefix=nullptr);

  void setProgressCallBack(totalProgressCallback cb);
  void defaultProgressCallback( size_t progress, size_t total );
}



namespace TarGzPacker
{
  using namespace TAR;

  // indirect tar-to-gz compression, using a temporary tar file created on srcFS and deleted afterwards
  int compress(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, const char* tmp_tar_filename, Stream* dstStream, const char* tar_prefix=nullptr);
  int compress(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, const char* tmp_tar_filename, fs::FS *dstFS, const char* tgz_name, const char* tar_prefix=nullptr);

  // ESP32, RP2040, or any FreeRTOS friendly SDK: direct tar-to-gz compression, no intermediate file, using FreeRTOS task/queues
  // These functions may be disabled if the plaform doesn't support FreeRTOS, see if `TARGZ_USE_TASKS` is defined after loading the library
  int compress(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, Stream* dstStream, const char* tar_prefix=nullptr);
  int compress(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, fs::FS *dstFS, const char* tgz_name, const char* tar_prefix=nullptr);
};



namespace TAR
{
  // shim for file.path()//fullName() not being consistent across arduino cores
  inline const char* fsFilePath(fs::File file)
  {
    return
      #if defined ESP32
        file.path()
      #elif defined ESP8266 || defined ARDUINO_ARCH_RP2040
        file.fullName()
      #else
        nullptr
        #error "unsupported architecture"
      #endif
    ;
  }

  // helper function to collect dirEntities from the contents of a given folder
  inline void collectDirEntities(std::vector<dir_entity_t> *dirEntities, fs::FS *fs, const char *dirname, uint8_t levels)
  {
    assert(fs);
    assert(dirname);

    File root = fs->open(dirname, "r");
    if (!root) {
      log_e("Failed to open directory %s", dirname);
      return;
    }
    if (!root.isDirectory()) {
      log_e("Not a directory %s", dirname);
      return;
    }

    if( String(dirname) != "/" )
      dirEntities->push_back( { String(dirname), true, 0 } );

    File file = root.openNextFile();

    while (file) {
      const char* file_path = fsFilePath(file);

      const String filePath =
        #if defined ESP32
          String( file_path )
        #elif defined ESP8266 || defined ARDUINO_ARCH_RP2040 // RP2040: fullName() isn't full, misses the leading slash
          file_path[0] == '/' ? String(file_path) : "/" + String(file_path)
        #else
          #error "unsupported architecture"
        #endif
      ;
      if (file.isDirectory()) {
        if (levels) {
          collectDirEntities(dirEntities, fs, filePath.c_str(), levels - 1);
        }
      } else {
        dirEntities->push_back( { filePath, false, file.size() } );
        log_d("  FILE: %-16s\tSIZE: %6d", filePath.c_str(), file.size() );
      }
      file = root.openNextFile();
    }
  }
};

