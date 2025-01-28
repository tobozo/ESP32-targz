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
//
//   // batch process dirEntities
//   void pack_files();
//
//   void open();
//   void add_entity();
//   void close();
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
  // progress callback setter [](size_t bytes_read, size_t total_bytes)
  void setProgressCallBack(totalProgressCallback cb);
  void defaultProgressCallback( size_t progress, size_t total );

  size_t lzHeader(uint8_t* buf, bool gzip_header=true);
  size_t lzFooter(uint8_t* buf, size_t outlen, unsigned crc, bool terminate=false);
  struct GZ::uzlib_comp* lzInit();

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

  int compress(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, Stream* dstStream, const char* tar_prefix=nullptr);
  int compress(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, fs::FS *dstFS, const char* tgz_name, const char* tar_prefix=nullptr);

};


// using TarGzPacker::TarGzStreamReader;
