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



namespace TarPacker
{
  uint32_t readbuf_size = 1024;
  uint8_t *readbuf = NULL;
  uint32_t readbuf_numblocks = 0;

  uint32_t writebuf_size = 4096;
  uint8_t *writebuf = NULL;
  uint32_t writebuf_numblocks = 0;

  fs::File fileRO; // file handle for input files to read
  fs::File fileRW; // file handle for output tar to write


  namespace io
  {
    void * open(void *_fs, const char *filename, const char *mode);
    int close(void *fs, void *file);
    int stat(void *_fs, const char *path, void *_stat);
    ssize_t read(void *_fs, void *_stream, void * buf, size_t count);
    ssize_t write_buffered(void *_fs, void *_stream, void * buf, size_t count);
    ssize_t write_stream(void *_fs, void *_stream, void * buf, size_t count);
    ssize_t write_finalize(void*_fs, void*_stream);


    tar_callback_t TarIOFunctions = {
      .openfunc       = io::open,
      .closefunc      = io::close,
      .readfunc       = io::read,
      .writefunc      = io::write_buffered,
      .closewritefunc = io::write_finalize,
      .statfunc       = io::stat
    };

  };

  using io::TarIOFunctions;

};




namespace LZPacker
{
  // LZPacker uses buffered streams
  static constexpr const size_t inputBufferSize  = 4096;// lowest possible value = 256
  static constexpr const size_t outputBufferSize = 4096;// lowest possible value = 1024

  Stream* dstStream = nullptr;
  Stream* srcStream = nullptr;

  void (*progressCb)( size_t progress, size_t total ) = nullptr;

  // write LZ77 header
  size_t lzHeader(uint8_t* buf, bool gzip_header=true)
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
  size_t lzFooter(uint8_t* buf, size_t outlen, unsigned crc, bool terminate=false)
  {
    assert(buf);
    size_t len = 0;
    for(size_t i=0;i<sizeof(crc);i++)
      buf[len++] = ((uint8_t*)&crc)[i];
    for(size_t i=0;i<sizeof(outlen);i++)
      buf[len++] = ((uint8_t*)&outlen)[i];

    if( terminate)
      buf[len++] = 0;

    return len;
  }


  // uzlib comp object initializer
  struct GZ::uzlib_comp* lzInit()
  {
    auto c = (struct GZ::uzlib_comp*)calloc(1, sizeof(struct GZ::uzlib_comp)+1);
    c->dict_size   = 32768;
    c->hash_bits   = 12;
    c->grow_buffer = 1;
    size_t hash_size = sizeof(GZ::uzlib_hash_entry_t) * (1 << c->hash_bits);
    c->hash_table = (const uint8_t**)calloc(hash_size, sizeof(uint8_t));
    if( c->hash_table == NULL ) {
      printf("lz77 error: unable to allocate %d bytes for hash table, halting\n", hash_size );
      while(1) yield();
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


  // Stream with in/out buffers to help with uzlib custom stream compressor
  class StreamBuffer : public Stream
  {
    uint8_t * buffer;
    int writePosition;
    int readPosition;
    int size;
    size_t capacity = 0;
    size_t grow_size = 64;
    public:
      StreamBuffer(uint8_t * buffer=nullptr, int size=0) : writePosition(0), readPosition(0) {
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
      else
        // Serial.printf(".");
        Serial.printf("%d ", pg);
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
    StreamBuffer dstStream(nullptr, 0);
    size_t dstLen = LZPacker::compress( srcStream, srcLen, &dstStream);
    if( dstLen == 0 || dstLen != dstStream.getSize() )
      return 0;
    *dstBuf = dstStream.getBuffer();
    return dstStream.getSize();
  }



  // buffer to buffer
  size_t compress( uint8_t* srcBuf, size_t srcBufLen, uint8_t** dstBuf )
  {
    StreamBuffer dstStream(nullptr, 0);
    size_t dstLen = LZPacker::compress( srcBuf, srcBufLen, &dstStream);
    if( dstLen == 0 || dstLen != dstStream.getSize() )
      return 0;
    *dstBuf = dstStream.getBuffer();
    return dstStream.getSize();
  }


  // stream to stream
  size_t compress( Stream* srcStream, size_t srcLen, Stream* dstStream )
  {
    assert(srcStream);
    assert(srcLen>0);
    assert(dstStream);

    LZPacker::dstStream = dstStream;
    LZPacker::srcStream = srcStream;

    uint8_t header[10];
    size_t header_size = lzHeader(header);
    size_t dstLen = dstStream->write(header, header_size);

    bool success = true;

    unsigned char* inputBuffer  = (unsigned char*)malloc(LZPacker::inputBufferSize);
    unsigned char* outputBuffer = (unsigned char*)malloc(LZPacker::outputBufferSize);

    auto c = lzInit();
    c->checksum_type = TINF_CHKSUM_CRC;
    c->writeDestByte = NULL;
    c->slen = srcLen;

    GZ::uzlib_stream uzstream;
    int prev_state = uzlib_deflate_init_stream(c, &uzstream);

    if(prev_state != Z_OK) {
        // abort !
        free(inputBuffer);
        free(outputBuffer);
        return 0;
    }

    uzstream.out.avail = LZPacker::outputBufferSize;

    int input_bytes = 0; // for do..while state
    size_t total_bytes = 0; // for progress meter

    int defl_mode  = Z_BLOCK;

    uint8_t footer[8];
    size_t footer_len = 0;
    size_t write_size = 0;

    do {
        if(uzstream.out.avail > 0){
            input_bytes = srcStream->readBytes(inputBuffer, LZPacker::inputBufferSize); // consume input stream
            total_bytes += input_bytes;

            if( c->progress_cb )
                c->progress_cb(total_bytes, srcLen);

            if(input_bytes == 0) {
              log_e("No more input bytes, srcStream->readBytes() miss?");
              //prev_state = Z_STREAM_END;
              break;
            }
            if(input_bytes == -1) {
              log_e("srcStream->readBytes() failed");
              return -1;
            }
            uzstream.in.next   = inputBuffer;
            uzstream.in.avail  = input_bytes;

            if( input_bytes != LZPacker::inputBufferSize ) {
                log_v("Last chunk");
                defl_mode = Z_FINISH;
            }
        }

        uzstream.out.next  = outputBuffer;
        uzstream.out.avail = LZPacker::outputBufferSize;

        prev_state = uzlib_deflate_stream(&uzstream, defl_mode);

        write_size = uzstream.out.next - outputBuffer;

        dstLen += dstStream->write(outputBuffer, write_size); // write to output stream

    } while(prev_state==Z_OK);


    if(prev_state!=Z_STREAM_END) {
      log_e("Premature end of gz stream (state=%d)", prev_state);
      success = false;
    }

    if( total_bytes != srcLen ) {
      success = false;
      int diff = srcLen - total_bytes;
      if( diff>0 ) {
        log_e("Bad input stream size: could not read all of %d requested bytes, missed %d bytes.", srcLen, diff);
      } else {
        log_e("Bad input stream size: read more than %d requested bytes, got %d extra bytes.", srcLen, -diff);
      }

      srcLen = total_bytes;
    }

    footer_len = lzFooter(footer, srcLen, ~c->checksum);
    log_d("Writing lz footer");
    dstLen += dstStream->write(footer, footer_len);

    free(c->hash_table);
    c->hash_table = NULL;
    if( c->outbuf != NULL ) free(c->outbuf);

    free(inputBuffer);
    free(outputBuffer);
    free(c);

    return success ? dstLen : -1;
  }



  // buffer to stream
  size_t compress( uint8_t* srcBuf, size_t srcBufLen, Stream* dstStream )
  {
    assert(srcBuf);
    assert(srcBufLen>0);
    assert(dstStream);

    LZPacker::dstStream = dstStream;
    auto c = lzInit();

    // wrap dstStream->write() in lambda byteWriter
    c->writeDestByte  = [](struct GZ::uzlib_comp *data, unsigned char byte) -> unsigned int { return LZPacker::dstStream->write(byte); };
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

}; // end namespace LZPacker








namespace TarPacker
{

  using namespace TAR;


  void deallocReadBuffer()
  {
    free(TarPacker::readbuf);
    TarPacker::readbuf = NULL;
  }

  void deallocWriteBuffer()
  {
    free(TarPacker::writebuf);
    TarPacker::writebuf = NULL;
  }

  void deallocBuffers()
  {
    deallocReadBuffer();
    deallocWriteBuffer();
  }


  bool allocReadBuffer(uint32_t readbuf_size)
  {
    TarPacker::readbuf = (uint8_t*)calloc(1, readbuf_size);
    if( TarPacker::readbuf==NULL ) {
      log_e("Failed to alloc %lu bytes for read buffer", readbuf_size);
      return false;
    }
    return true;
  }


  bool allocWriteBuffer(uint32_t writebuf_size)
  {
    TarPacker::writebuf = (uint8_t*)calloc(1, writebuf_size);
    if( TarPacker::writebuf==NULL ) {
      log_e("Failed to alloc %lu bytes for read buffer", writebuf_size);
      return false;
    }
    return true;
  }


  bool allocBuffers()
  {
    if( !allocReadBuffer(TarPacker::readbuf_size) ) {
      return false;
    }
    if( !allocWriteBuffer(TarPacker::writebuf_size) ) {
      deallocReadBuffer();
      return false;
    }
    return true;
  }



  // i/o functions
  namespace io
  {

    void * open(void *_fs, const char *filename, const char *mode)
    {
      assert(_fs);
      fs::FS* fs = (fs::FS*)_fs;
      String flagStr;
      void* retPtr;

      log_d("io::open(%s, mode=%s)", filename, mode);

      if( String(mode) == "r" ) {
        readbuf_numblocks = 0; // reset read buffer
        flagStr = "r";
        TarPacker::fileRO = fs->open(filename, "r");
        if(!TarPacker::fileRO) {
          log_e("Unable to open %s for reading", filename);
          return (void*)-1;
        }
        retPtr = &TarPacker::fileRO;
      } else {
        writebuf_numblocks = 0; // reset write buffer
        flagStr = "w";
        TarPacker::fileRW = fs->open(filename, "w");
        if(!TarPacker::fileRW) {
          log_e("Unable to open %s for writing", filename);
          return (void*)-1;
        }
        retPtr = &TarPacker::fileRW;
      }

      return retPtr;
    }



    int close(void *fs, void *file)
    {
      assert(file);
      fs::File* filePtr = (fs::File*)file;

      log_d("io::close(fs, %s)", filePtr->name() );

      filePtr->close();
      return 0;
    }



    int stat(void *_fs, const char *path, void *_stat)
    {
      assert(_fs);
      assert(_stat);
      fs::FS* fs = (fs::FS*)_fs;
      struct stat *s = (struct stat *)_stat;
      static int inode_num = 0;

      if(!fs->exists(path)) {
        log_e("Path %s does not exist", path);
        return -1;
      }
      log_v("stat_func: stating %s", path );
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

      return 0;
    }


    ssize_t read(void *_fs, void *_stream, void * buf, size_t count)
    {
      assert(_stream);
      Stream* streamPtr = (Stream*)_stream;

      uint32_t buf_idx = (readbuf_numblocks*T_BLOCKSIZE)%readbuf_size;
      ssize_t readbytes = count;
      if( buf_idx == 0 ) {
        readbytes = streamPtr->readBytes(TarPacker::readbuf, readbuf_size);
        if( readbytes<count ) {
          log_d("asked to read %d bytes, got %d\n", count, readbytes);
          count = readbytes;
        }
      }
      if( buf_idx + count > readbuf_size ) {
        log_e("Aborting read to prevent buffer overflow\n");
        return -1;
      }
      memcpy(buf, &TarPacker::readbuf[buf_idx], count );

      readbuf_numblocks++;

      return count > 0 ? count : -1;
    }


    ssize_t write_finalize(void*_fs, void*_stream)
    {
      assert(_stream);
      Stream* streamPtr = (Stream*)_stream;

      uint32_t leftover_bytes = (writebuf_numblocks*T_BLOCKSIZE)%writebuf_size;
      if( leftover_bytes == 0 ) {
        log_d("writefunc::commit_finalize(no leftover bytes at block %d)", writebuf_numblocks);
        return 0; // no data leftover in buffer
      }
      log_d("writefunc::commit_finalize(%lu leftover bytes)", leftover_bytes );
      return streamPtr->write(TarPacker::writebuf, leftover_bytes);
    }


    // direct writes
    ssize_t write_stream(void *_fs, void *_stream, void * buf, size_t count)
    {
      Stream* streamPtr = (Stream*)_stream;
      return streamPtr->write(TarPacker::writebuf, writebuf_size);
    }


    // buffered writes
    ssize_t write_buffered(void *_fs, void *_stream, void * buf, size_t count)
    {
      assert(_stream);
      Stream* streamPtr = (Stream*)_stream;
      // printf("write: Got output tar file handle: %p at position %lu\n", _stream );
      uint32_t buf_idx = (writebuf_numblocks*T_BLOCKSIZE)%writebuf_size;

      if( buf_idx + count > TarPacker::writebuf_size ) {
        log_e("Aborting write to prevent buffer overflow");
        return -1;
      }

      if( writebuf_numblocks>0 && buf_idx==0 ) { // commit buffer write
        log_d("writefunc::commit(%lu bytes, block=%lu, idx=%lu)", writebuf_size, writebuf_numblocks, buf_idx);
        ssize_t written = streamPtr->write(TarPacker::writebuf, writebuf_size);
        if( written != writebuf_size ) {
          log_e("Buffer write fail (req %u bytes, wrote %d/%lu bytes)", count, written, writebuf_size );
          return -1;
        }
      } else {
        log_d("writefunc::buffer(req %u/%lu bytes, block=%lu, idx=%lu)", count, writebuf_size, writebuf_numblocks, buf_idx);
      }

      memcpy(&TarPacker::writebuf[buf_idx], buf, count );

      writebuf_numblocks++;

      return count;
    }

  }; // end namespace io



  bool pack_files_step(tar_files_packer_t *packer);

  // private: batch append files
  int append_files(tar_files_packer_t*p)
  {

    assert(p);
    do {
      if(!pack_files_step(p)) {
        p->status = false;
        break;
      }
    } while( p->step != PACK_FILES_END );

    // free(p->tar);

    return  p->status ? 0 : 1;
  }



  // private: state machine stepper when appending files to a tar archive
  int append_files_step(tar_files_packer_t*p)
  {
    if( p == nullptr || p->dirIterator == nullptr ) { // prevent crashing
      log_e("Malformed tar_files_packer, aborting");
      return -1;
    }

    dir_iterator_t* dirIterator = p->dirIterator; // local alias to make code easier to read

    enum steps_t { init, loop, eof, end };

    static steps_t step = init;

    if(!p->tar)
      step = init;

    if(step!=loop) {
      log_d("Step: %d p->tar = %p", step, p->tar);
    }

    switch(step)
    {
      case init:
        p->tar = (TAR::TAR*)calloc(1, sizeof(TAR::TAR));
        if( p->tar == NULL ) {
          log_e("Failed to alloc %d bytes for p->tar", sizeof(TAR::TAR) );
        }
        p->status = tar_open(p->tar, p->tar_path, p->io, "w", p->fs) == 0 ? true : false;
        log_d("INIT p->tar = %p", p->tar);
        if(!p->status) {
          log_e("Failed to open input tar for writing");
          return -1;
        }
        // basic health check, first entity in tar should be the root directory
        if( !dirIterator->available() ) {
          log_e("Error: first item should be a directory e.g. '/'");
          return -1;
        }
        step = loop; // begin loop
        if( !dirIterator->next() ) { // append root directory entity
          return -1;
        }
        log_d("ROOTDIR (%s -> %s)", dirIterator->dirEntity.path.c_str(), dirIterator->SavePath.c_str());
        // process TAR header
        do {
          if( tar_append_entity_step(&dirIterator->entity) != 0 )
            return -1;
        } while( ! dirIterator->stepComplete() );
        // fallthrough
      case loop:
        if( dirIterator->iterator > 1 && !dirIterator->stepComplete() ) { // in loop doing entity step
          log_v("-> [ x ] ENTITY STEP (%s)", dirIterator->dirEntity.path.c_str());
          return tar_append_entity_step(&dirIterator->entity);
        } else {
          if( dirIterator->stepComplete() ) {
            log_d("Finished adding %s", dirIterator->dirEntity.path.c_str());
          }
        }
        // check for main loop end
        if( dirIterator->complete() ) {
          if( dirIterator->stepComplete() ) {
            log_d("No more files to add");
            step = eof;
          }
          return 0;
        }
        // in loop assigning next entity, or skipping if entity is filtered
        if( ! dirIterator->next() ) {
          log_d("Skipping entity #%d (%s)", dirIterator->iterator, dirIterator->dirEntity.path.c_str());
          dirIterator->entity.step = ENTITY_END;
        }
        return 0;
      case eof: log_d("Appending EOF to TAR archive (p->tar = %p)", p->tar);
        if( tar_append_eof(p->tar, p->written_bytes) != 0 )
          return -1;
        step = end;
        // return 0;
        // fallthrough
      case end: log_d("Calling io::close(p->tar = %p)", p->tar);
        p->entity_step = PACK_FILES_END;
        if( tar_close(p->tar) !=0 ) {
          log_e("Failed to close archive");
          return -1;
        }
        free( p->tar );
        p->tar = NULL;
        p->status = true;
        step = init;
      break;
    }
    return p->status ? 0 : -1;
  }



  // private: state machine stepper when packing files
  bool pack_files_step(tar_files_packer_t *packer)
  {
    static bool ret = false;

    switch(packer->step)
    {
      case PACK_FILES_SETUP:
        log_d("Pack files setup");
        ret = false;
        if( !allocBuffers())
          return false;
        packer->step = PACK_FILES_STEP;
        // fallthrough
      case PACK_FILES_STEP :
        log_v("Pack step");
        if(TarPacker::append_files_step(packer) != 0 )
          return false;
        if(packer->entity_step == PACK_FILES_END ) {
          packer->status = true; // success!
          // TODO: trigger last write?
          packer->step = PACK_FILES_END;
        } else
          return true;
        // fallthrough
      case PACK_FILES_END  :
        log_v("PACK_FILES_END");
        deallocBuffers();
        ret = packer->status;
      break;
    }

    return ret;
  }



  // public
  size_t pack_files(fs::FS *fs, std::vector<dir_entity_t> *dirEntities, const char*tar_path, const char*dst_path)
  {
    assert(fs);
    assert(dirEntities);

    TarGzStream tarStream(fs, dirEntities, tar_path, NULL, dst_path);

    if( tarStream.size() <= 0 ) {
      log_e("Nothing to archive!");
      return -1;
    }

    ssize_t bytes_ready = 0;
    uint8_t buf[4096];

    fs::File out = fs->open(tar_path, "w");
    if(!out)
      return -1;

    do {
      ssize_t bytes_read = tarStream.readBytes(buf, 4096);
      if( bytes_read<=0 ) {// EOF
        log_v("EOF");
        break;
      }

      ssize_t bytes_written = out.write(buf, bytes_read);
      if( bytes_written != bytes_read ) {
        log_e("Write error, got %d bytes to write but wrote %d (total %d/%d)", bytes_read, bytes_written, bytes_ready, tarStream.size() );
        out.close();
        return -1;
      }

      bytes_ready += bytes_read;
      log_v("Bytes ready: %d", bytes_ready);
    } while( bytes_ready<tarStream.size() );

    out.close();
    return bytes_ready;
  }


}; // end namespace TarPacker



namespace TarGzPacker
{
  using namespace TAR;


  TarGzStream* tarGzStreamPtr = NULL;
  TAR::tar_callback_t IOFunctions = { NULL, NULL, NULL, NULL, NULL, NULL };

  TarGzStream::~TarGzStream()
  {
    if(buffer) {
      free(buffer);
      buffer=NULL;
    }

    tarGzStreamPtr = NULL;

    using namespace TarPacker;

    // restore the original function before lambda is destroyed
    IOFunctions = TarIOFunctions;
  }


  void TarGzStream::_init()
  {
    // init tar files packer
    assert(srcFS);
    assert(dirEntities);
    assert(tar_path);
    assert(bufSize>=4096);

    using namespace TarPacker;

    buffer = (uint8_t*)malloc(bufSize); // create tar<->gz buffer
    if( buffer == NULL ) {
      log_e("Failed to allocate %d bytes for tar to gz buffer, halting", bufSize);
      while(1) yield();
    }

    log_d("Stream to Stream mode enabled");

    // for lambda capture
    tarGzStreamPtr = this;

    IOFunctions = {
      // Override the default open() callback triggered from tar_open() to optionally return this stream:
      //   - opening mode = read only  -> return (void*) file descriptor (legacy behaviour)
      //   - opening mode = write only -> return (Stream*)[this]
      .openfunc = [](void *_fs, const char *filename, const char *mode) -> void*
      {
        if(String(mode)=="r") { // open input file to archive on behalf of tar
          readbuf_numblocks = 0; // reset gz read buffer
          return TarIOFunctions.openfunc(_fs, filename, mode); // return file descriptor
        } else { // opening gz output on behalf of LZPacker
          writebuf_numblocks = 0; // reset gz write buffer
          return tarGzStreamPtr; // return stream
        }
      },
      .closefunc = [](void *fs, void *file) -> int {
        if( file == tarGzStreamPtr ) {
          log_d("GZ: Closing stream");
          return 0;
        }
        log_d("TAR: Closing input file %s", ((fs::File*)file)->name() );
        return TarIOFunctions.closefunc(fs, file);
      },
      .readfunc       = io::read,
      .writefunc      = io::write_buffered,
      .closewritefunc = io::write_finalize,
      .statfunc       = io::stat
    };

    // estimate the full tar size so that LZPacker can write the gz header correctly
    total_bytes = 0;

    log_i("[%4s] %-32s %-8s\t%-8s", "Type", "Path", "FileSize", "TarSize" );
    log_i("[%4s] %-32s %-8s\t%-8s", "----", "--------------------------------", "--------", "--------" );

    for(int i=1; i<dirEntities->size();i++) {
      auto dirEntity = dirEntities->at(i);
      if(   dirEntity.path == String(tar_path)  // skip root dir
         || dirEntity.path == String(tgz_path)) // skip tgz file
        continue;
      size_t tar_size = 512; // tar entity header
      if(! dirEntity.is_dir )
        tar_size += dirEntity.size + (512 - (dirEntity.size%512)); // add file size aligned to 512 bytes
      log_i("[%4s] %-32s %8d\t%8d", dirEntity.is_dir?"dir":"file", dirEntity.path.c_str(), dirEntity.size, tar_size );
      total_bytes += tar_size;
    }

    total_bytes += 1024; // tar eof

    log_i("Tar estimated file size for %d elements: %d", dirEntities->size(), total_bytes);

    dirIterator = { &tar_files_packer, dirEntities };

    tar_files_packer = TAR::tar_files_packer_t{
      .tar           = NULL,
      .fs            = srcFS,
      .tar_path      = tar_path,
      .tgz_path      = tgz_path,
      .dst_path      = dst_path,
      .written_bytes = &written_bytes,
      .io            = &IOFunctions,
      .status        = false,
      .step          = PACK_FILES_SETUP,
      .entity_step   = PACK_FILES_SETUP,
      .dirIterator   = &dirIterator
    };

  }



  // write to gz input buffer
  size_t TarGzStream::write(const uint8_t* data, size_t len)
  {
    if( len > bufSize ) {
      log_w("Bad Write request (%d bytes) exceeds targz buffer size (total %d), aborting", len, bufSize);
      return -1;
    }
    log_d("writestream: store(%d bytes)", len);
    bytes_ready = len;
    memcpy(buffer, data, len);
    return len;
  }



  // read bytes from input tarPacker
  size_t TarGzStream::readBytes(uint8_t* dest, size_t len)
  {
    using namespace TAR;

    if( len > bufSize ) {
      log_e("Bad Read request (%d bytes) exceeds targz buffer size (%d bytes), aborting", len, bufSize);
      return -1;
    }

    bytes_ready = 0;

    do {
      if(!TarPacker::pack_files_step(&tar_files_packer)) {
        log_w("pack_files_step failed");
        tar_files_packer.status = false;
        break;
      }
      // until buffer full or end of tar
    } while( bytes_ready==0 && tar_files_packer.step != PACK_FILES_END );

    if( bytes_ready != len ) { // not using the entire buffer ?
      log_d("Zerofilling buffer (len=%d, ready=%d)", len, bytes_ready);
      memset(dest, 0, len); // zerofill buffer
    }

    memcpy(dest, buffer, bytes_ready );

    return bytes_ready;
  }



  size_t compress(fs::FS *srcFS, const char* src_path, const char* tgz_path, const char* dst_path)
  {

    log_d("compress(fs::FS*, src_path=%s, tgz_path=%s, dst_path=%s)", src_path, tgz_path?tgz_path:"null", dst_path?dst_path:"null");

    std::vector<dir_entity_t> dirEntities;

    collectDirEntities(&dirEntities, srcFS, src_path, 3);

    TarGzStream tarStream(srcFS, &dirEntities, src_path, tgz_path, dst_path, LZPacker::inputBufferSize);

    if( tarStream.size() <= 0 ) {
      log_e("Nothing to compress, aborting");
      return 0;
    }

    auto tarGzFile = srcFS->open(tgz_path, "w");

    if(!tarGzFile) {
      log_e("Can't open %s for writing, aborting", tgz_path);
      return 0;
    }

    auto compressedSize = TarGzPacker::compress(&tarStream, &tarGzFile);

    tarGzFile.close();

    return compressedSize;

  }

  size_t compress(TarGzStream *tarStream, Stream *dstStream)
  {
    log_d("compress(TarGzStream*, Stream *)");
    assert(tarStream);
    assert(dstStream);

    if( !tarStream->ready() ) {
      log_e("TarGz creation failed, aborting");
      return 0;
    }

    log_d("Input size: %d bytes", tarStream->size());

    return LZPacker::compress( tarStream, tarStream->size(), dstStream );
  }

}; // end namespace TarGzPacker

