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

#include "LibPacker.hpp"


namespace TAR
{
  #include "../tar/libtar.h"
}

namespace GZ
{
  #include "../uzlib/uzlib.h"
}


#pragma GCC optimize("O2")


namespace LZPacker
{
  // LZPacker uses buffered streams
  static int inputBufferSize  = 4096;// lowest possible value = 256 or 512 if streaming from tar
  static int outputBufferSize = 4096;// lowest possible value = 1024

  Stream* dstStream = nullptr;
  Stream* srcStream = nullptr;

  void (*progressCb)( size_t progress, size_t total ) = nullptr;
  size_t lzHeader(uint8_t* buf, bool gzip_header=true);
  size_t lzFooter(uint8_t* buf, uint32_t outlen, uint32_t crc, bool terminate=false);
  struct GZ::uzlib_comp* lzInit();

  // write LZ77 header
  size_t lzHeader(uint8_t* buf, bool gzip_header)
  {
    assert(buf);
    size_t len = 0;

    // see https://www.ietf.org/rfc/rfc1950.txt and https://www.ietf.org/rfc/rfc1951.txt
    if(gzip_header) {
      buf[len++] = ((uint8_t)0x1f);
      buf[len++] = ((uint8_t)0x8b);
    }
    // CMF (Compression Method and flags)
    //     This byte is divided into a 4-bit compression method and a 4- bit information field depending on the compression method.
    //       bits 0 to 3  CM     Compression method
    //       bits 4 to 7  CINFO  Compression info
    // CM = 8 denotes the "deflate" compression method with a window size up to 32K
    buf[len++] = ((uint8_t)0x08);
    // FLG (FLaGs)
    //    This flag byte is divided as follows:
    //
    //       bits 0 to 4  FCHECK  (check bits for CMF and FLG)
    //       bit  5       FDICT   (preset dictionary)
    //       bits 6 to 7  FLEVEL  (compression level)
    buf[len++] = ((uint8_t)0x00); // FLG
    for(size_t i=0;i<sizeof(int);i++)
      buf[len++] = 0; // mtime
    buf[len++] = ((uint8_t)0x04); // XFL
    buf[len++] = ((uint8_t)0x03); // OS
    return len;
  }


  // write LZ77 footer
  size_t lzFooter(uint8_t* buf, uint32_t outlen, uint32_t crc, bool terminate)
  {
    assert(buf);
    size_t len = 0;
    for(size_t i=0;i<4;i++)
      buf[len++] = ((uint8_t*)&crc)[i];
    for(size_t i=0;i<4;i++)
      buf[len++] = ((uint8_t*)&outlen)[i];

    if( terminate)
      buf[len++] = 0;

    return len;
  }


  // uzlib comp object initializer
  struct GZ::uzlib_comp* lzInit()
  {
    auto c = (struct GZ::uzlib_comp*)calloc(1, sizeof(struct GZ::uzlib_comp)+1);
    if( c == NULL ) {
      log_e("unable to alloc %d bytes for compressor", sizeof(struct GZ::uzlib_comp)+1);
      return nullptr;
    }

    c->dict_size   = 32768;
    c->hash_bits   = 12;
    c->grow_buffer = 1;
    size_t hash_size = sizeof(GZ::uzlib_hash_entry_t) * (1 << c->hash_bits);
    c->hash_table = (const uint8_t**)calloc(hash_size, sizeof(uint8_t));
    if( c->hash_table == NULL ) {
      printf("lz77 error: unable to allocate %d bytes for hash table\n", hash_size );
      return nullptr;
    }
    c->checksum_type = TINF_CHKSUM_CRC;
    c->checksum_cb = GZ::uzlib_crc32;    // more reliable but slightly slower
    // comp.checksum_cb = uzlib_adler32; // slightly faster but more prone to checksum miss
    c->checksum = ~0;
    if( LZPacker::progressCb != nullptr )
      c->progress_cb = LZPacker::progressCb;

    c->outbuf = NULL;
    return c;
  }

  // LZ77 Stream writer e.g. size_t compressed_size = LZStreamWriter::write(uncompressedBytes, count)
  class LZStreamWriter : public Stream
  {
  private:
    Stream* dstStream;
    size_t srcLen;
    const size_t bufSize = 4096;
    size_t outputBufIdx = 0;
    unsigned char* outputBuffer = nullptr;
    unsigned char* inputBuffer = nullptr;
    struct GZ::uzlib_comp* compressor = nullptr;
    GZ::uzlib_stream uzstream;
    int prev_state;
    size_t dstLen;           // gz output size
    size_t total_bytes = 0;  // progress meter and end health check
    int defl_mode = Z_BLOCK; // gz loop control
    bool success = false;    // return status
    bool in_loop = false;    // stream loop control

  public:

    LZStreamWriter() { }

    LZStreamWriter(Stream* dstStream, size_t srcLen, const size_t bufSize=4096) : dstStream(dstStream), srcLen(srcLen), bufSize(bufSize)
    {
      assert(dstStream);
      setup();
    }

    ~LZStreamWriter()
    {
      free(compressor->hash_table);
      compressor->hash_table = NULL;
      if( compressor->outbuf != NULL )
        free(compressor->outbuf);
      if( outputBuffer )
        free(outputBuffer);
      if( inputBuffer )
        free(inputBuffer);
      if( compressor )
        free(compressor);
    };


    size_t size()
    {
      return success ? dstLen : -1;
    }

    void setup()
    {
      if( srcLen == 0) {
        log_e("Bad source length, aborting");
        return;
      }
      if(!dstStream) {
        log_e("No destination stream, aborting");
        return;
      }

      uint8_t header[10];
      size_t header_size = LZPacker::lzHeader(header);
      dstLen = dstStream->write(header, header_size);
      if( dstLen == 0 ) {
        log_e("Failed to write lz header");
        return;
      }

      outputBuffer = (unsigned char*)malloc(bufSize);
      if(!outputBuffer) {
        log_e("Failed to malloc %d bytes", bufSize);
      }

      inputBuffer = (unsigned char*)malloc(bufSize);
      if(!inputBuffer) {
        log_e("Failed to malloc %d bytes", bufSize);
      }

      compressor = LZPacker::lzInit();

      if(!compressor)
        return;

      compressor->checksum_type = TINF_CHKSUM_CRC;
      compressor->writeDestByte = NULL;
      compressor->slen = srcLen;

      prev_state = uzlib_deflate_init_stream(compressor, &uzstream);

      if(prev_state != Z_OK) {
        log_e("failed to init lz77");
        return;
      }

      uzstream.out.avail = bufSize;
      total_bytes = 0;      // reset processed bytes
      outputBufIdx = 0;     // reset buffer index
      defl_mode  = Z_BLOCK; // init GZ loop
      in_loop = true;       // ready to loop
      success = true;       // reset success state
    }


    virtual size_t write(const uint8_t* buf, size_t size)
    {
      if(!success || !in_loop || prev_state != Z_OK) {
        log_d("Returning from previous error");
        return -1;
      }

      if( size > bufSize ) {
        log_e("Writing %d bytes would overflow the %d-bytes buffer, aborting", size);
        return -1;
      }

      // index to write in the gz input buffer
      size_t buf_idx = outputBufIdx%bufSize;
      // append to buffer
      memcpy(&inputBuffer[buf_idx], buf, size);
      outputBufIdx += size;

      if( outputBufIdx < srcLen   // not last chunk
       && buf_idx+size < bufSize) // buffer not full
      {
        log_v("cached chunk %d bytes", size);
        return size;
      }

      // full buffer, or maybe-partial buffer if last chunk
      size_t input_bytes =  outputBufIdx < srcLen ? bufSize : buf_idx+size;

      // print some debug if in verbose mode
      if( outputBufIdx+1 >= srcLen ) {
        log_v("last chunk");
      } else {
        log_v("chunk %d bytes, now at idx %d", input_bytes, outputBufIdx);
      }

      if(uzstream.out.avail > 0) {
        total_bytes += input_bytes;

        if( compressor->progress_cb )
            compressor->progress_cb(total_bytes, srcLen);

        uzstream.in.next   = (uint8_t*)inputBuffer;
        uzstream.in.avail  = input_bytes;

        if(total_bytes==srcLen) {
          log_v("Last chunk");
          defl_mode = Z_FINISH;
        }
      }

      uzstream.out.next  = outputBuffer;
      uzstream.out.avail = bufSize;

      prev_state = uzlib_deflate_stream(&uzstream, defl_mode);

      size_t write_size = uzstream.out.next - outputBuffer;
      size_t written_bytes = dstStream->write(outputBuffer, write_size); // write to output stream

      if( written_bytes == 0 ) {
        log_e("Write failed at offset %d", outputBufIdx );
        in_loop = false;
        return -1;
      }

      dstLen += written_bytes;

      if( total_bytes > srcLen ) {
        log_e("Read more bytes (%d) than source contains (%d), something is wrong", total_bytes, srcLen);
        in_loop = false;
        return -1;
      }

      if(prev_state==Z_STREAM_END) {
        in_loop = false;
        end();
      }

      return size;
    }

    void end()
    {
      if(prev_state!=Z_STREAM_END) {
        log_e("Premature end of gz stream (state=%d)", prev_state);
        success = false;
      }

      if( compressor->progress_cb )
          compressor->progress_cb(srcLen, srcLen); // send progress end signal, whatever the outcome

      if( total_bytes != srcLen ) {
        success = false;
        int diff = srcLen - total_bytes;
        if( diff>0 ) {
          log_e("Bad input stream size: could not read every %d bytes, missed %d bytes.", srcLen, diff);
          log_e("The file has been truncated, it is likely corrupted");
        } else {
          log_e("Bad input stream size: read %d further than %d requested bytes.", -diff, srcLen);
          log_e("The file has been padded, it is likely corrupted");
        }
        // fix the size so the corrupted gzip file can be saved for inspection
        srcLen = total_bytes;
      }

      uint8_t footer[8];
      size_t footer_len = 0;

      footer_len = LZPacker::lzFooter(footer, srcLen, ~compressor->checksum);
      log_v("Writing lz footer");
      dstLen += dstStream->write(footer, footer_len);

    };


    virtual size_t write(uint8_t c) { log_e("This function should not be called"); return this->write(&c, 1); }
    virtual int available() { log_e("This function should not be called"); return 0; }
    virtual int read() { log_e("This function should not be called"); return 0; }
    virtual int peek() { log_e("This function should not be called"); return 0; }

  };


  // Stream with in/out buffers to help with uzlib custom stream compressor
  class LZBufferWriter : public Stream
  {
    uint8_t * buffer;
    int writePosition;
    int readPosition;
    int size;
    size_t capacity = 0;
    size_t grow_size = 64;
    public:
      LZBufferWriter(uint8_t * buffer=nullptr, int size=0) : writePosition(0), readPosition(0) {
        this->buffer = buffer;
        this->size = size;
        if( buffer == nullptr ) { // write mode
          this->buffer = (uint8_t*)malloc(grow_size);
          this->capacity = grow_size;
        }
      }
      uint8_t * getBuffer() { return buffer; }
      size_t getSize() { return size; }
      // Stream methods
      virtual int available(){ return writePosition - readPosition; }
      virtual int read(){ if(readPosition == writePosition) return -1; return buffer[readPosition++]; }
      virtual int peek(){ if(readPosition == writePosition) return -1; return buffer[readPosition]; }
      virtual void flush() { readPosition=0; writePosition = 0; }
      // Print methods
      virtual void end(){ buffer[writePosition] = '\0'; }
      virtual size_t write(uint8_t c) { return this->write(&c, 1); }
      virtual size_t write(const uint8_t* data, size_t len) {
        size_t target_size = size + len;
        if (target_size >= capacity) {
          while( capacity<target_size )
              capacity += grow_size;
          buffer = (uint8_t*)realloc(buffer, capacity);
        }
        memcpy(buffer + size, data, len);
        size += len;
        writePosition += len;
        return len;
      }
  };



  void defaultProgressCallback( size_t progress, size_t total )
  {
    assert(total>0);
    static size_t lastprogress = 0xff; // percent progress
    size_t pg = 100*(float(progress) / float(total));

    if( progress !=0 && pg > 100 ) {
      // log_w("Bogus value for progress progress=%d, total=%d, pg=%d, lastprogress=%d\n", progress, total, pg, lastprogress);
      return;
    }

    if( pg != lastprogress ) {
      if( pg == 0 )
        Serial.printf("Processed 0%% ");
      else if( pg == 100 )
        Serial.printf(" 100%%\n");
      else {
        if( pg%25==0)
          Serial.printf("%d", pg);
        else if( pg%10==0)
          Serial.printf(".");

      }
      lastprogress = pg;
    }
  }


  // progress callback setter
  void setProgressCallBack(totalProgressCallback cb)
  {
    LZPacker::progressCb = cb;
  }


  // stream to buffer
  size_t compress( Stream* srcStream, size_t srcLen, uint8_t** dstBuf )
  {
    log_d("Stream to buffer (source=%d bytes)", srcLen);
    LZBufferWriter dstStream(nullptr, 0);
    size_t dstLen = LZPacker::compress( srcStream, srcLen, &dstStream);
    if( dstLen == 0 || dstLen != dstStream.getSize() )
      return 0;
    *dstBuf = dstStream.getBuffer();
    return dstStream.getSize();
  }


  // buffer to buffer
  size_t compress( uint8_t* srcBuf, size_t srcBufLen, uint8_t** dstBuf )
  {
    log_d("Buffer to buffer (source=%d bytes)", srcBufLen);
    LZBufferWriter dstStream(nullptr, 0);
    size_t dstLen = LZPacker::compress( srcBuf, srcBufLen, &dstStream);
    if( dstLen == 0 || dstLen != dstStream.getSize() )
      return 0;
    *dstBuf = dstStream.getBuffer();
    return dstStream.getSize();
  }


  // stream to stream
  size_t compress( Stream* srcStream, size_t srcLen, Stream* dstStream )
  {
    log_d("Stream to Stream (source=%d bytes)", srcLen);
    if( !srcStream || srcLen==0 || !dstStream )
      return -1;
    LZPacker::LZStreamWriter lzStream( dstStream, srcLen, LZPacker::outputBufferSize );
    size_t total_source_bytes = 0;
    size_t total_gz_bytes = 0;
    unsigned char* inputBuffer  = (unsigned char*)malloc(LZPacker::inputBufferSize);
    bool success = true;
    do {
      size_t read_bytes = srcStream->readBytes(inputBuffer, LZPacker::inputBufferSize);
      if(read_bytes==0) {
        log_e("Failed to read %d bytes from source", LZPacker::inputBufferSize);
        success = false;
        break;
      }
      total_source_bytes += read_bytes;
      size_t written_bytes = lzStream.write(inputBuffer, read_bytes);
      if( written_bytes == 0 ) {
        log_e("Failed to compress/write %d bytes", read_bytes);
        success = false;
        break;
      }
      total_gz_bytes += written_bytes;
    } while( total_source_bytes < srcLen );
    free(inputBuffer);
    return success ? lzStream.size() : -1;
  }


  // buffer to stream
  size_t compress( uint8_t* srcBuf, size_t srcBufLen, Stream* dstStream )
  {
    log_d("Buffer to Stream (source=%d bytes)", srcBufLen);
    assert(srcBuf);
    assert(srcBufLen>0);
    assert(dstStream);

    LZPacker::dstStream = dstStream;
    auto c = lzInit();

    if(!c)
      return 0;

    // wrap dstStream->write() in lambda byteWriter
    c->writeDestByte  = []([[maybe_unused]]struct GZ::uzlib_comp *data, unsigned char byte) -> unsigned int {
      return LZPacker::dstStream->write(byte);
    };
    c->grow_buffer = 0; // use direct writes, don't grow output buffer

    uint8_t header[10];
    size_t header_len = lzHeader(header);
    dstStream->write(header, header_len);

    GZ::zlib_start_block(c);
    GZ::uzlib_compress(c, srcBuf, srcBufLen);
    GZ::zlib_finish_block(c);

    uint8_t footer[8];
    c->checksum = c->checksum_cb(srcBuf, srcBufLen, c->checksum);
    size_t footer_len = lzFooter(footer, srcBufLen, ~c->checksum);
    dstStream->write(footer, footer_len);

    free(c->hash_table);
    c->hash_table = NULL;
    auto ret = c->outlen;
    free(c);

    return header_len + ret + footer_len;
  }


  // stream to file
  size_t compress( Stream* srcStream, size_t srcLen, fs::FS*dstFS, const char* dstFilename )
  {
    log_d("Stream to file (source=%d bytes)", srcLen);
    if( !srcStream || srcLen>0 || !dstFS || !dstFilename)
      return 0;
    fs::File dstFile = dstFS->open(dstFilename, "w");
    if( !dstFile )
      return 0;
    auto ret = LZPacker::compress(srcStream, srcLen, &dstFile);
    dstFile.close();
    return ret;
  }


  // file to file
  size_t compress( fs::FS *srcFS, const char* srcFilename, fs::FS*dstFS, const char* dstFilename )
  {
    if( !srcFS || !srcFilename || !dstFS || !dstFilename)
      return 0;
    fs::File srcFile = srcFS->open(srcFilename, "r");
    if(!srcFile)
      return 0;
    auto ret = LZPacker::compress( &srcFile, srcFile.size(), dstFS, dstFilename);
    srcFile.close();
    return ret;
  }


  // file to stream
  size_t compress( fs::FS *srcFS, const char* srcFilename, Stream* dstStream )
  {
    if( !srcFS || !srcFilename || !dstStream)
      return 0;
    fs::File srcFile = srcFS->open(srcFilename, "r");
    if(!srcFile)
      return 0;
    log_d("File to stream (source=%d bytes)", srcFile.size());
    auto ret = LZPacker::compress( &srcFile, srcFile.size(), dstStream );
    srcFile.close();
    return ret;
  }


}; // end namespace LZPacker



namespace TarPacker
{
  using namespace TAR;

  uint8_t block_buf[T_BLOCKSIZE]; // 512 bytes, not worth a malloc()

  void (*progressCb)( size_t progress, size_t total ) = nullptr;
  int pack_tar_impl(tar_params_t *p = nullptr);

  // progress callback setter
  void setProgressCallBack(totalProgressCallback cb);


  namespace io
  {
    fs::File fileRO;
    fs::File fileRW;

    void * open(void *_fs, const char *filename, const char *mode);
    int close(void *fs, void *file);
    int stat(void *_fs, const char *path, void *_stat);
    int read(void *_fs, void *_stream, void * buf, size_t count);
    int write_finalize(void*_fs, void*_stream);
    int write_stream(void *_fs, void *_stream, void * buf, size_t count);
  };

  static tar_callback_t TarIOFuncs =
  {
    .src_fs         = nullptr,
    .dst_fs         = nullptr,
    .openfunc       = io::open,  // r/w
    .closefunc      = io::close, // r/w
    .readfunc       = io::read,         // ro
    .writefunc      = io::write_stream, // wo
    .closewritefunc = io::write_finalize,
    .statfunc       = io::stat
  };

  static std::vector<tar_entity_t> _tarEntities;
  static size_t _tar_estimated_filesize = 0;
  static TAR::TAR* _tar;


  // tar i/o functions
  namespace io
  {

    // Many POSIX inherited functions don't make use of at least one argument.
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-parameter"

    void * open(void *_fs, const char *filename, const char *mode)
    {
      assert(_fs);
      fs::FS* fs = (fs::FS*)_fs;
      String flagStr;
      void* retPtr;
      log_v("io::open(%s, mode=%s)", filename, mode);
      if( String(mode) == "r" ) {
        flagStr = "r";
        fileRO = fs->open(filename, "r");
        if(!fileRO) {
          log_e("Unable to open %s for reading", filename);
          return (void*)-1;
        }
        retPtr = &fileRO;
      } else {
        flagStr = "w";
        fileRW = fs->open(filename, "w");
        if(!fileRW) {
          log_e("Unable to open %s for writing", filename);
          return (void*)-1;
        }
        retPtr = &fileRW;
      }
      return retPtr;
    }


    int close(void *fs, void *file)
    {
      assert(file);
      fs::File* filePtr = (fs::File*)file;
      log_v("io::close(fs, %s)", filePtr->name() );
      filePtr->close();
      return 0;
    }


    int stat(void *_fs, const char *path, void *_stat)
    {
      if(!_fs || !_stat)
        return -1;
      fs::FS* fs = (fs::FS*)_fs;
      struct_stat_t *s = (struct_stat_t *)_stat;
      static int inode_num = 0;

      fs::File f = fs->open(path, "r");
      if(!f) {
        log_e("Unable to open %s for stat", path);
        return -1;
      }

      auto is_dir = f.isDirectory();
      // things to set when mocking stat():
      s->st_mode = is_dir ? 040755 : 0100755;
      s->st_size = is_dir ? 0 : f.size();
      s->st_ino = ++inode_num;
      s->st_uid = 0; // root user
      s->st_gid = 0; // root group
      s->st_mtime = strcmp(path, "/") == 0 ? 0 : f.getLastWrite();

      f.close();

      log_v("stat_func: [%s] %s %d bytes", is_dir?"dir":"file", path, s->st_size );

      return 0;
    }


    int read(void *_fs, void *_stream, void * buf, size_t count)
    {
      assert(_stream);
      Stream* streamPtr = (Stream*)_stream;
      return streamPtr->readBytes(block_buf, count);
    }


    int write_finalize(void*_fs, void*_stream)
    {
      assert(_stream);
      // Stream* streamPtr = (Stream*)_stream;
      return 0;
    }


    // direct writes
    int write_stream(void *_fs, void *_stream, void * buf, size_t count)
    {
      Stream* streamPtr = (Stream*)_stream;
      return streamPtr->write((uint8_t*)buf, count);
    }

    #pragma GCC diagnostic pop

  }; // end namespace io




  void setProgressCallBack(totalProgressCallback cb)
  {
    TarPacker::progressCb = cb;
  }



  int add_header(TAR::TAR* tar, tar_entity_t tarEntity)
  {
    struct_stat_t entity_stat;

    if( tar->io->statfunc(tar->io->src_fs, tarEntity.realpath.c_str(), &entity_stat) != 0) {
      // file or dir not found
      log_e("File not found: %s", tarEntity.realpath.c_str() );
      return -1;
    }
    memset(&(tar->th_buf), 0, sizeof(struct tar_header)); // clear header buffer
    th_set_from_stat(tar, &entity_stat); // set header block
    th_set_path(tar, tarEntity.savepath.c_str()); // set the header path
    int written_bytes = 0;
    if (th_write(tar, &written_bytes) != 0) { // header write failed?
      return -1;
    }
    if( written_bytes != T_BLOCKSIZE ) { // th_write() made a boo boo!
      log_e("header write failed for: %s", tarEntity.realpath.c_str() );
      return -1;
    }
    return T_BLOCKSIZE;
  }



  int add_body_chunk(TAR::TAR* tar)
  {
    memset(block_buf, 0, T_BLOCKSIZE);
    int read_bytes = tar->io->readfunc(tar->io->src_fs, tar->src_file, block_buf, T_BLOCKSIZE);
    if( read_bytes <= 0 ) {
      log_e("ReadBytes Failed");
      return -1;
    }
    if( read_bytes != T_BLOCKSIZE ) { // block was zero padded anyway
      log_v("last TAR block was zero-padded after %d bytes", read_bytes );
    }
    auto ret = tar->io->writefunc(tar->io->dst_fs, tar->dst_file, block_buf, T_BLOCKSIZE);
    return ret;
  }



  int add_eof_chunk(TAR::TAR* tar)
  {
    log_v("add_eof_chunk");
    memset(block_buf, 0, T_BLOCKSIZE);
    size_t written_bytes = tar->io->writefunc(tar->io->dst_fs, tar->dst_file, block_buf, T_BLOCKSIZE);
    return written_bytes;
  }



  int pack_tar_init(tar_callback_t *io, fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, fs::FS *dstFS, const char*output_file_path, const char* tar_prefix=nullptr)
  {
    _tarEntities.clear();

    _tar_estimated_filesize = 0;

    for(size_t i=0;i<dirEntities.size();i++) {
      auto d = dirEntities.at(i);
      if( String(output_file_path)==d.path ) // ignore self
        continue;

      size_t tar_entity_size = T_BLOCKSIZE; // entity header
      if(d.size>0) {
        tar_entity_size += d.size;
        if( d.size%T_BLOCKSIZE != 0 ) // file size isn't a multiple of 512
          tar_entity_size += (T_BLOCKSIZE - (d.size%T_BLOCKSIZE)); // align to 512 bytes
      }

      _tar_estimated_filesize += tar_entity_size;

      auto realpath = d.path;
      auto savepath = tar_prefix ? String(tar_prefix)+realpath : realpath;

      if( !tar_prefix && savepath.startsWith("/") ) // tar paths can't be slash-prepended, add a dot
        savepath = "." + savepath;

      tar_entity_t e;
      e.realpath = realpath;
      e.savepath = savepath;
      e.is_dir = d.is_dir;
      e.size = d.size;
      _tarEntities.push_back( e );
      log_w("Add entity( [%4s]\t%-32s\t%d bytes -> %s (%d tar bytes)", d.is_dir?"DIR":"FILE", realpath.c_str(), d.size, savepath.c_str(), tar_entity_size );
    }

    _tar_estimated_filesize += 1024; //tar footer

    log_i("TAR estimated file size: %d bytes", _tar_estimated_filesize );

    _tar = (TAR::TAR*)calloc(1, sizeof(TAR::TAR));
    if( _tar == NULL ) {
      log_e("Failed to alloc %d bytes for tar", sizeof(TAR::TAR) );
    }

    io->src_fs = srcFS;
    io->dst_fs = dstFS;

    int status = tar_open(_tar, output_file_path, io);

    if(status!=0) {
      log_e("Failed to open input tar for writing");
      return -1;
    }
    return _tar_estimated_filesize;
  }



  int pack_tar_impl(tar_params_t *p)
  {
    size_t total_bytes = 0;
    size_t chunks = 0;
    size_t entities_size = _tarEntities.size();
    tar_entity_t current_entity;
    int chunk_size;

    if(_tar_estimated_filesize==0)
      return -1;

    if( TarPacker::progressCb )
      TarPacker::progressCb(0,_tar_estimated_filesize);

    for(size_t i=0;i<entities_size;i++) {
      current_entity = _tarEntities.at(i);
      chunk_size = add_header(_tar, current_entity);
      if( chunk_size == -1 )
        return -1;
      total_bytes += chunk_size;
      if( current_entity.size>0 ) {
        chunks = ceil(float(current_entity.size)/T_BLOCKSIZE);
        _tar->src_file = _tar->io->openfunc( _tar->io->src_fs, current_entity.realpath.c_str(), "r" );
        if( _tar->src_file == (void*)-1 ) {
          log_e("Open failed for: %s", current_entity.realpath.c_str() );
          return -1;
        }
        // log_d("got %d chunks in %d bytes for %s", chunks, current_entity.size, current_entity.realpath.c_str() );
        for(size_t c=0;c<chunks;c++) {
          chunk_size = add_body_chunk(_tar);
          if( chunk_size == -1 )
            return -1;
          total_bytes += chunk_size;
          if( TarPacker::progressCb )
            TarPacker::progressCb(total_bytes,_tar_estimated_filesize);
        }
        _tar->io->closefunc( _tar->io->src_fs, _tar->src_file );
      }
    }
    total_bytes += add_eof_chunk(_tar);
    total_bytes += add_eof_chunk(_tar);

    // all writes have been done, the rest is decoration

    if( TarPacker::progressCb ) {
      TarPacker::progressCb(_tar_estimated_filesize, _tar_estimated_filesize); // send end signal, whatever the outcome
    }

    if( _tar_estimated_filesize != total_bytes ) {
      log_e("Tar data length (%d bytes) does not match the initial estimation (%d bytes)!", total_bytes, _tar_estimated_filesize);
    } else {
      log_d("Success: estimated size %d matches output size.", total_bytes);
    }

    tar_close(_tar);

    if(p)
      p->ret = total_bytes;

    return total_bytes;
  }




  int pack_files(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, Stream* dstStream, const char* tar_prefix)
  {
    auto TarStreamFunctions = TarIOFuncs;
    TarStreamFunctions.src_fs = srcFS;
    TarStreamFunctions.dst_fs = nullptr;

    int tar_estimated_filesize = pack_tar_init(&TarStreamFunctions, srcFS, dirEntities, nullptr, nullptr, tar_prefix);
    if( tar_estimated_filesize <=0 )
      return -1;

    _tar->dst_file = dstStream;

    auto ret = pack_tar_impl();

    free(_tar);

    return ret;
  }



  int pack_files(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, fs::FS *dstFS, const char*tar_output_file_path, const char* tar_prefix)
  {
    auto tar = dstFS->open(tar_output_file_path, "w");
    if(!tar)
      return -1;
    auto ret = pack_files(srcFS, dirEntities, &tar, tar_prefix);
    tar.close();
    return ret;
  }


}; // end namespace TarPacker



namespace TarGzPacker
{
  using namespace TAR;
  using namespace TarPacker;

  static tar_params_t targz_params;


  // tar-to-gz compression from files/folders list
  int compress(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, fs::FS *dstFS, const char* tgz_name, const char* tar_prefix)
  {
    auto dstFile = dstFS->open(tgz_name, "w");
    if(!dstFile) {
      log_e("Can't open %s for writing", tgz_name);
      return -1;
    }
    auto ret = compress(srcFS, dirEntities, &dstFile, tar_prefix);
    dstFile.close();
    return ret;
  }


  // tar-to-gz compression from files/folders list
  int compress(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, Stream* dstStream, const char* tar_prefix)
  {
    auto TarStreamFunctions = TarIOFuncs;
    TarStreamFunctions.src_fs = srcFS;
    TarStreamFunctions.dst_fs = nullptr;

    int tar_estimated_filesize = pack_tar_init(&TarStreamFunctions, srcFS, dirEntities, nullptr, nullptr, tar_prefix);
    if( tar_estimated_filesize <=0 )
      return -1;

    LZPacker::LZStreamWriter lzStream( dstStream, tar_estimated_filesize );

    _tar->dst_file = &lzStream; // attach gz stream to tar i/o

    auto ret = pack_tar_impl();

    free(_tar);

    return ret ? lzStream.size() : -1;
  }


  // tar-to-gz compression from path
  int compress(fs::FS *srcFS, const char* srcDir, Stream* dstStream, const char* tar_prefix)
  {
    std::vector<dir_entity_t> dirEntities;
    TarPacker::collectDirEntities(&dirEntities, srcFS, srcDir);
    return compress(srcFS, dirEntities, dstStream, tar_prefix);
  }

  // tar-to-gz compression from path
  int compress(fs::FS *srcFS, const char* srcDir, fs::FS *dstFS, const char* tgz_name, const char* tar_prefix)
  {
    std::vector<dir_entity_t> dirEntities;
    TarPacker::collectDirEntities(&dirEntities, srcFS, srcDir);
    return compress(srcFS, dirEntities, dstFS, tgz_name, tar_prefix);
  }


}; // end namespace TarGzPacker

