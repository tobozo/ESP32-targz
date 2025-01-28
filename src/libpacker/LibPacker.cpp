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



namespace LZPacker
{
  // LZPacker uses buffered streams
  static constexpr const size_t inputBufferSize  = 4096;// lowest possible value = 256
  static constexpr const size_t outputBufferSize = 4096;// lowest possible value = 1024

  Stream* dstStream = nullptr;
  Stream* srcStream = nullptr;

  void (*progressCb)( size_t progress, size_t total ) = nullptr;

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
  size_t lzFooter(uint8_t* buf, size_t outlen, unsigned crc, bool terminate)
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
    log_d("LZPacker::compress(stream, size=%d, stream)", srcLen);
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
    //size_t total_read_bytes = 0;

    int defl_mode  = Z_BLOCK;

    uint8_t footer[8];
    size_t footer_len = 0;
    size_t write_size = 0;
    size_t written_bytes = 0;

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
            } else if(input_bytes == -1) {
              log_e("srcStream->readBytes() failed");
              return -1;
            } else {
              // log_d("srcStream->readBytes() returned %d bytes", input_bytes);
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

        written_bytes = dstStream->write(outputBuffer, write_size); // write to output stream

        if( written_bytes == 0 ) {
          log_e("Write failed at offset %d", input_bytes );
          break;
        } else {
          log_v("Wrote %d/%d bytes", written_bytes, write_size );
        }

        dstLen += written_bytes;

        if( total_bytes > srcLen ) {
          log_e("Read more bytes (%d) than source contains (%d), something is wrong", total_bytes, srcLen);
          break;
        }

    } while(prev_state==Z_OK);


    if(prev_state!=Z_STREAM_END) {
      log_e("Premature end of gz stream (state=%d)", prev_state);
      success = false;
    }

    if( c->progress_cb )
        c->progress_cb(srcLen, srcLen); // send progress end signal, whatever the outcome

    if( total_bytes != srcLen ) {
      success = false;
      int diff = srcLen - total_bytes;
      if( diff>0 ) {
        log_e("Bad input stream size: could not read every %d bytes, missed %d bytes.", srcLen, diff);
      } else {
        log_e("Bad input stream size: read %d further than %d requested bytes.", -diff, srcLen);
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

  uint8_t block_buf[512];

  static bool use_lock = false;
  static bool targzlock = false;

  void (*progressCb)( size_t progress, size_t total ) = nullptr;

  // progress callback setter
  void setProgressCallBack(totalProgressCallback cb)
  {
    TarPacker::progressCb = cb;
  }

  size_t readBytesAsync(tar_params_t *params, uint8_t* buf, size_t len);

  void takeLock();
  void setLock(bool set=true);
  void releaseLock();

  namespace io
  {
    fs::File fileRO;
    fs::File fileRW;

    void * open(void *_fs, const char *filename, const char *mode);
    int close(void *fs, void *file);
    int stat(void *_fs, const char *path, void *_stat);
    ssize_t read(void *_fs, void *_stream, void * buf, size_t count);
    ssize_t write_finalize(void*_fs, void*_stream);
    ssize_t write_stream(void *_fs, void *_stream, void * buf, size_t count);
  };


  tar_callback_t TarIOFunctions = {
    .src_fs         = nullptr,
    .dst_fs         = nullptr,
    .openfunc       = io::open,  // r/w
    .closefunc      = io::close, // r/w
    .readfunc       = io::read,         // ro
    .writefunc      = io::write_stream, // wo
    .closewritefunc = io::write_finalize,
    .statfunc       = io::stat
  };

  std::vector<tar_entity_t> _tarEntities;
  size_t _tar_estimated_filesize = 0;
  TAR::TAR* _tar;

  size_t readBytesAsync(tar_params_t *params, uint8_t* buf, size_t len)
  {
    if(len%512!=0) { // not a multiple of 512
      log_e("%d is not a multiple of 512!", len);
      return 0;
    }
    int idx = 0;
    do {
      while(!targzlock)
        vTaskDelay(1);
      memcpy(&buf[idx], block_buf, 512);
      releaseLock();
      idx += 512;
      if( idx == len )
        return len;
    } while(params->ret == 1);

    return idx;
  }


  void takeLock()
  {
    if( use_lock ) {
      while(targzlock)
        vTaskDelay(1);
    }
  }


  void setLock(bool set)
  {
    if( use_lock ) {
      targzlock = set;
    }
  }


  void releaseLock()
  {
    if( use_lock ) {
      setLock( false );
    }
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
      struct stat *s = (struct stat *)_stat;
      static int inode_num = 0;

      if(!fs->exists(path)) {
        log_e("Path %s does not exist", path);
        return -1;
      }

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


    ssize_t read(void *_fs, void *_stream, void * buf, size_t count)
    {
      assert(_stream);
      Stream* streamPtr = (Stream*)_stream;
      return streamPtr->readBytes(block_buf, count);
    }


    ssize_t write_finalize(void*_fs, void*_stream)
    {
      assert(_stream);
      // Stream* streamPtr = (Stream*)_stream;
      return 0;
    }


    // direct writes
    ssize_t write_stream(void *_fs, void *_stream, void * buf, size_t count)
    {
      Stream* streamPtr = (Stream*)_stream;
      return streamPtr->write((uint8_t*)buf, count);
    }

  }; // end namespace io




  int add_header(TAR::TAR* tar, tar_entity_t tarEntity)
  {
    takeLock();

    struct stat entity_stat;

    if( tar->io->statfunc(tar->io->src_fs, tarEntity.realpath.c_str(), &entity_stat) != 0) {
      // file or dir not found
      log_e("File not found: %s", tarEntity.realpath.c_str() );
      return -1;
    }
    memset(&(tar->th_buf), 0, sizeof(struct tar_header)); // clear header buffer
    th_set_from_stat(tar, &entity_stat); // set header block
    th_set_path(tar, tarEntity.savepath.c_str()); // set the header path
    ssize_t written_bytes = 0;
    if (th_write(tar, &written_bytes) != 0) { // header write failed?
      return -1;
    }
    if( written_bytes != 512 ) { // th_write() made a boo boo!
      log_e("header write failed for: %s", tarEntity.realpath.c_str() );
      return -1;
    }
    setLock();
    return 512;
  }



  int add_body_chunk(TAR::TAR* tar)
  {
    takeLock();
    memset(block_buf, 0, 512);
    ssize_t read_bytes = tar->io->readfunc(tar->io->src_fs, tar->src_file, block_buf, 512);
    if( read_bytes <= 0 ) {
      log_e("ReadBytes Failed");
      return -1;
    }
    if( read_bytes != 512 ) { // block was zero padded anyway
      log_v("only got %d bytes of %d requested", read_bytes, 512 );
    }
    auto ret = tar->io->writefunc(tar->io->dst_fs, tar->dst_file, block_buf, 512);
    setLock();
    return ret;
  }



  int add_eof_chunk(TAR::TAR* tar)
  {
    takeLock();
    memset(block_buf, 0, 512);
    size_t written_bytes = tar->io->writefunc(tar->io->dst_fs, tar->dst_file, block_buf, 512);
    setLock();
    return written_bytes;
  }



  int pack_tar_init(tar_callback_t *io, fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, fs::FS *dstFS, const char*output_file_path, const char* tar_prefix=nullptr)
  {
    _tarEntities.clear();

    _tar_estimated_filesize = 0;

    for(int i=0;i<dirEntities.size();i++) {
      auto d = dirEntities.at(i);
      if( String(output_file_path)==d.path ) // ignore self
        continue;

      _tar_estimated_filesize += 512; // entity header
      if(d.size>0) {
        _tar_estimated_filesize += d.size + (512 - (d.size%512)); // align to 512 bytes
      }

      auto realpath = d.path;
      auto savepath = tar_prefix ? String(tar_prefix)+realpath : realpath;

      if( !tar_prefix && savepath.startsWith("/") ) // tar paths can't be slash-prepended, add a dot
        savepath = "." + savepath;

      _tarEntities.push_back( { realpath, savepath, d.is_dir, d.size } );
      log_w("Add entity( [%4s]\t%-32s\t%d bytes -> %s", d.is_dir?"DIR":"FILE", realpath.c_str(), d.size, savepath.c_str() );
    }

    _tar_estimated_filesize += 1024; //tar footer

    log_i("TAR estimated file size: %d", _tar_estimated_filesize );

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



  int pack_tar_impl()
  {
    size_t total_bytes = 0;
    size_t chunk_size = 0;
    size_t chunks = 0;
    size_t entities_size = _tarEntities.size();
    tar_entity_t current_entity;

    if(_tar_estimated_filesize==0)
      return -1;

    if( TarPacker::progressCb )
      TarPacker::progressCb(0,_tar_estimated_filesize);

    for(int i=0;i<entities_size;i++) {
      current_entity = _tarEntities.at(i);
      chunk_size = add_header(_tar, current_entity);
      if( chunk_size == -1 ) {
        return -1;
      }
      total_bytes += chunk_size;
      if( current_entity.size>0 ) {
        chunks = ceil(float(current_entity.size)/512.0);
        _tar->src_file = _tar->io->openfunc( _tar->io->src_fs, current_entity.realpath.c_str(), "r" );
        if( _tar->src_file == (void*)-1 ) {
          log_e("Open failed for: %s", current_entity.realpath.c_str() );
          return -1;
        }
        // log_d("got %d chunks in %d bytes for %s", chunks, current_entity.size, current_entity.realpath.c_str() );
        for(int c=0;c<chunks;c++) {
          chunk_size = add_body_chunk(_tar);
          if( chunk_size == -1 ) {
            return -1;
          }
          total_bytes += chunk_size;
          if( TarPacker::progressCb )
            TarPacker::progressCb(total_bytes,_tar_estimated_filesize);
        }
        _tar->io->closefunc( _tar->io->src_fs, _tar->src_file );
      }
    }
    total_bytes += add_eof_chunk(_tar);
    total_bytes += add_eof_chunk(_tar);

    if( TarPacker::progressCb )
      TarPacker::progressCb(_tar_estimated_filesize,_tar_estimated_filesize); // send end signal, whatever the outcome

    tar_close(_tar);

    return total_bytes;
  }



  void pack_tar_task(void* params)
  {
    tar_params_t *p = (tar_params_t*)params;
    p->ret = pack_tar_impl();
    vTaskDelete(NULL);
  }



  int pack_files(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, Stream* dstStream, const char* tar_prefix)
  {
    auto TarStreamFunctions = TarIOFunctions;
    TarStreamFunctions.src_fs = srcFS;
    TarStreamFunctions.dst_fs = nullptr;

    tar_params_t params =
    {
      .srcFS            = srcFS,
      .dirEntities      = dirEntities,
      .dstFS            = nullptr,
      .output_file_path = nullptr,
      .tar_prefix       = tar_prefix,
      .io               = &TarStreamFunctions,
      .ret              = 1
    };

    ssize_t tar_estimated_filesize = pack_tar_init(params.io, params.srcFS, params.dirEntities, params.dstFS, params.output_file_path, params.tar_prefix);
    if( tar_estimated_filesize <=0 )
      return -1;

    _tar->dst_file = dstStream;
    use_lock = false;

    TaskHandle_t tarTaskHandle = NULL;
    xTaskCreate(pack_tar_task, "pack_tar_task", 4096, &params, tskIDLE_PRIORITY, &tarTaskHandle );

    while(params.ret == 1) {
      vTaskDelay(1);
    }

    vTaskDelay(1);

    free(_tar);

    return params.ret;
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


  class TarGzStreamReader : public Stream
  {
    private:
      TAR::tar_params_t* tar_params;
    public:
      TarGzStreamReader(TAR::tar_params_t* tar_params) : tar_params(tar_params) { };
      ~TarGzStreamReader() { };
      // read bytes from input tarPacker
      virtual size_t readBytes(uint8_t* dest, size_t count) { return TarPacker::readBytesAsync(tar_params, dest, count); }
      // all other methods are unused
      virtual size_t write(const uint8_t* data, size_t len) { return 0; };
      virtual int available() { return 0; };
      virtual int read() { return 0; };
      virtual int peek() { return 0; };
      virtual void flush() { };
      virtual void end() { };
      virtual size_t write(uint8_t c) { return c?0:0; };
  };


  int compress(fs::FS *srcFS, std::vector<dir_entity_t> dirEntities, Stream* dstStream, const char* tar_prefix)
  {
    auto TarGzStreamFunctions = TarIOFunctions;

    TarGzStreamFunctions.src_fs = srcFS;
    TarGzStreamFunctions.dst_fs = nullptr;

    TarGzStreamFunctions.writefunc = [](void *_fs, void *_stream, void * buf, size_t count)->ssize_t {
      if(count!=512) {
        log_e("BAD block write (req=%d, avail=512)", count );
        return -1;
      }
      memcpy(block_buf, buf, count);
      return 512;
    };

    tar_params_t tp =
    {
      .srcFS            = srcFS,
      .dirEntities      = dirEntities,
      .dstFS            = nullptr, // none when streaming
      .output_file_path = nullptr, // none when streaming
      .tar_prefix       = tar_prefix,
      .io               = &TarGzStreamFunctions,
      .ret              = 1
    };

    ssize_t srcLen = pack_tar_init(tp.io, tp.srcFS, tp.dirEntities, tp.dstFS, tp.output_file_path, tp.tar_prefix);
    if( srcLen <=0 )
      return -1;

    TarGzStreamReader tarStream(&tp);

    log_d("Tar estimated data size: %d bytes", srcLen);

    // set async
    use_lock = true;
    releaseLock();

    TaskHandle_t tarGzTaskHandle = NULL;
    xTaskCreate(pack_tar_task, "pack_tar_task", 4096, &tp, tskIDLE_PRIORITY, &tarGzTaskHandle );

    auto ret = LZPacker::compress(&tarStream, srcLen, dstStream);

    vTaskDelay(1);

    free(_tar);

    return ret;
  }

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



}; // end namespace TarGzPacker

