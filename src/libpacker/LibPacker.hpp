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

#include "./common.hpp"

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
  // progress callback setter [](size_t bytes_read, size_t total_bytes)
  void setProgressCallBack(totalProgressCallback cb);
  void defaultProgressCallback( size_t progress, size_t total );
};



// .tar packager
namespace TarPacker
{
  using namespace TAR;

  // TAR pack from directory to a tar file on same filesystem
  size_t pack_files(fs::FS *srcFS, std::vector<dir_entity_t> *dirEntities, const char*tar_path, const char*dst_path);
  // TAR pack from directory entities to output stream
  //size_t pack_files(tar_files_packer_t *packer);
};



// .tar.gz packager+compressor
namespace TarGzPacker
{
  // tar to gz stream helper
  class TarGzStream : public Stream
  {
    private:
      fs::FS *srcFS = nullptr;
      std::vector<TAR::dir_entity_t> *dirEntities = nullptr;
      const char* tar_path = nullptr; // e.g. "/test.tar", or "/" if streaming to gz
      const char* tgz_path = nullptr; // e.g "/text.tar.gz"
      const char* dst_path = nullptr;
      ssize_t written_bytes = 0;
      ssize_t total_bytes = 0;
      ssize_t bytes_ready = 0;
      uint8_t *buffer = nullptr;
      size_t bufSize = 4096;
      TAR::tar_files_packer_t tar_files_packer;
      TAR::dir_iterator_t dirIterator;
      void _init();
    public:
      TarGzStream(fs::FS *srcFS, std::vector<TAR::dir_entity_t> *dirEntities, const char* tar_path, const char* tgz_path=NULL, const char* dst_path=NULL, size_t bufSize=4096)
        : srcFS(srcFS), dirEntities(dirEntities), tar_path(tar_path), dst_path(dst_path), bufSize(bufSize) { _init(); };
      ~TarGzStream();
      // write to gz input buffer
      virtual size_t write(const uint8_t* data, size_t len);
      // read bytes from input tarPacker
      virtual size_t readBytes(uint8_t* dest, size_t count);
      // return evaluated tar size
      ssize_t size() { return total_bytes; }
      inline bool ready() { return size()>0; };
      // Stream methods (not used in this implementation)
      virtual int available() { return 0; };
      virtual int read() { return 0; };
      virtual int peek() { return 0; };
      virtual void flush() { };
      // Print methods (not used in this implementation)
      virtual void end() { };
      virtual size_t write(uint8_t c) { return c?0:0; };
  };

  // tar gz methods
  size_t compress(fs::FS *srcFS, const char* src_path, const char* tgz_path=NULL, const char* dst_path=NULL);
  size_t compress(TarGzStream *tarStream, Stream *dstStream);
};

using TarGzPacker::TarGzStream;
