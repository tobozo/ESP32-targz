#if defined TEENSYDUINO

  // Teensyduino <FS.h> implementation is incomplete compared to esp32, esp8266 and rp2040
  // Moreover, SdFat and LittleFS have many mismatched types and conflicting method names.
  // This fs::FS implentation (stolen from esp32 core) will be used as a mocking layer to
  // shim SdFat and Teensy's very custom LittleFS using a unified fs::FS API.

  #ifndef TGZ_FSIMPL_H
  #define TGZ_FSIMPL_H

    #include <stddef.h>
    #include <stdint.h>
    #include <memory>
    #include <FS.h>


    namespace fs
    {

      #define TGZFS_FILE_READ   "r"
      #define TGZFS_FILE_WRITE  "w"
      #define TGZFS_FILE_APPEND "a"

      class File;

      class FileImpl;
      typedef std::shared_ptr<FileImpl> FileImplPtr;
      class FSImpl;
      typedef std::shared_ptr<FSImpl> FSImplPtr;

      class File : public Stream {
      public:
        File(FileImplPtr p = FileImplPtr()) : _p(p) {
          _timeout = 0;
        }

        size_t write(uint8_t) override;
        size_t write(const uint8_t *buf, size_t size) override;
        int available() override;
        int read() override;
        int peek() override;
        void flush() override;
        size_t read(uint8_t *buf, size_t size);
        size_t readBytes(char *buffer, size_t length) {
          return read((uint8_t *)buffer, length);
        }

        bool seek(uint32_t pos, SeekMode mode);
        bool seek(uint32_t pos) {
          return seek(pos, SeekSet);
        }
        size_t position() const;  // returns (size_t)-1 on error
        size_t size() const;
        bool setBufferSize(size_t size);
        void close();
        operator bool() const;
        time_t getLastWrite();
        const char *path() const;
        const char *name() const;

        boolean isDirectory(void);
        boolean seekDir(long position);
        File openNextFile(const char *mode = TGZFS_FILE_READ);
        String getNextFileName(void);
        String getNextFileName(boolean *isDir);
        void rewindDirectory(void);

      protected:
        FileImplPtr _p;
      };

      class FS {
      public:
        FS(FSImplPtr impl) : _impl(impl) {}

        File open(const char *path, const char *mode = TGZFS_FILE_READ, const bool create = false);
        File open(const String &path, const char *mode = TGZFS_FILE_READ, const bool create = false);

        bool exists(const char *path);
        bool exists(const String &path);

        bool remove(const char *path);
        bool remove(const String &path);

        bool rename(const char *pathFrom, const char *pathTo);
        bool rename(const String &pathFrom, const String &pathTo);

        bool mkdir(const char *path);
        bool mkdir(const String &path);

        bool rmdir(const char *path);
        bool rmdir(const String &path);

        const char *mountpoint();

      protected:
        FSImplPtr _impl;
      };



      class FileImpl {
      public:
        virtual ~FileImpl() {}
        virtual size_t write(const uint8_t *buf, size_t size) = 0;
        virtual size_t read(uint8_t *buf, size_t size) = 0;
        virtual void flush() = 0;
        virtual bool seek(uint32_t pos, SeekMode mode) = 0;
        virtual size_t position() const = 0;
        virtual size_t size() const = 0;
        virtual bool setBufferSize(size_t size) = 0;
        virtual void close() = 0;
        virtual time_t getLastWrite() = 0;
        virtual const char *path() const = 0;
        virtual const char *name() const = 0;
        virtual boolean isDirectory(void) = 0;
        virtual FileImplPtr openNextFile(const char *mode) = 0;
        virtual boolean seekDir(long position) = 0;
        virtual String getNextFileName(void) = 0;
        virtual String getNextFileName(bool *isDir) = 0;
        virtual void rewindDirectory(void) = 0;
        virtual operator bool() = 0;
      };

      class FSImpl {
      protected:
        const char *_mountpoint;

      public:
        FSImpl() : _mountpoint(NULL) {}
        virtual ~FSImpl() {}
        virtual FileImplPtr open(const char *path, const char *mode, const bool create) = 0;
        virtual bool exists(const char *path) = 0;
        virtual bool rename(const char *pathFrom, const char *pathTo) = 0;
        virtual bool remove(const char *path) = 0;
        virtual bool mkdir(const char *path) = 0;
        virtual bool rmdir(const char *path) = 0;
        void mountpoint(const char *);
        const char *mountpoint();
      };



      inline size_t File::write(uint8_t c) {
        if (!*this) {
          return 0;
        }
        return _p->write(&c, 1);
      }

      inline time_t File::getLastWrite() {
        if (!*this) {
          return 0;
        }
        return _p->getLastWrite();
      }

      inline size_t File::write(const uint8_t *buf, size_t size) {
        if (!*this) {
          return 0;
        }
        return _p->write(buf, size);
      }

      inline int File::available() {
        if (!*this) {
          return false;
        }
        return _p->size() - _p->position();
      }

      inline int File::read() {
        if (!*this) {
          return -1;
        }
        uint8_t result;
        if (_p->read(&result, 1) != 1) {
          return -1;
        }
        return result;
      }

      inline size_t File::read(uint8_t *buf, size_t size) {
        if (!*this) {
          return -1;
        }
        return _p->read(buf, size);
      }

      inline int File::peek() {
        if (!*this) {
          return -1;
        }
        size_t curPos = _p->position();
        int result = read();
        seek(curPos, SeekSet);
        return result;
      }

      inline void File::flush() {
        if (!*this) {
          return;
        }
        _p->flush();
      }

      inline bool File::seek(uint32_t pos, SeekMode mode) {
        if (!*this) {
          return false;
        }
        return _p->seek(pos, mode);
      }

      inline size_t File::position() const {
        if (!*this) {
          return (size_t)-1;
        }
        return _p->position();
      }

      inline size_t File::size() const {
        if (!*this) {
          return 0;
        }
        return _p->size();
      }

      inline bool File::setBufferSize(size_t size) {
        if (!*this) {
          return 0;
        }
        return _p->setBufferSize(size);
      }

      inline void File::close() {
        if (_p) {
          _p->close();
          _p = nullptr;
        }
      }

      inline File::operator bool() const {
        return _p != nullptr && *_p != false;
      }

      inline const char *File::path() const {
        if (!*this) {
          return nullptr;
        }
        return _p->path();
      }

      inline const char *File::name() const {
        if (!*this) {
          return nullptr;
        }
        return _p->name();
      }

      //to implement
      inline boolean File::isDirectory(void) {
        if (!*this) {
          return false;
        }
        return _p->isDirectory();
      }

      inline File File::openNextFile(const char *mode) {
        if (!*this) {
          return File();
        }
        return _p->openNextFile(mode);
      }

      inline boolean File::seekDir(long position) {
        if (!_p) {
          return false;
        }
        return _p->seekDir(position);
      }

      inline String File::getNextFileName(void) {
        if (!_p) {
          return "";
        }
        return _p->getNextFileName();
      }

      inline String File::getNextFileName(bool *isDir) {
        if (!_p) {
          return "";
        }
        return _p->getNextFileName(isDir);
      }

      inline void File::rewindDirectory(void) {
        if (!*this) {
          return;
        }
        _p->rewindDirectory();
      }

      inline File FS::open(const String &path, const char *mode, const bool create) {
        return open(path.c_str(), mode, create);
      }

      inline File FS::open(const char *path, const char *mode, const bool create) {
        if (!_impl) {
          return File();
        }
        return File(_impl->open(path, mode, create));
      }

      inline bool FS::exists(const char *path) {
        if (!_impl) {
          return false;
        }
        return _impl->exists(path);
      }

      inline bool FS::exists(const String &path) {
        return exists(path.c_str());
      }

      inline bool FS::remove(const char *path) {
        if (!_impl) {
          return false;
        }
        return _impl->remove(path);
      }

      inline bool FS::remove(const String &path) {
        return remove(path.c_str());
      }

      inline bool FS::rename(const char *pathFrom, const char *pathTo) {
        if (!_impl) {
          return false;
        }
        return _impl->rename(pathFrom, pathTo);
      }

      inline bool FS::rename(const String &pathFrom, const String &pathTo) {
        return rename(pathFrom.c_str(), pathTo.c_str());
      }

      inline bool FS::mkdir(const char *path) {
        if (!_impl) {
          return false;
        }
        return _impl->mkdir(path);
      }

      inline bool FS::mkdir(const String &path) {
        return mkdir(path.c_str());
      }

      inline bool FS::rmdir(const char *path) {
        if (!_impl) {
          return false;
        }
        return _impl->rmdir(path);
      }

      inline bool FS::rmdir(const String &path) {
        return rmdir(path.c_str());
      }

      inline const char *FS::mountpoint() {
        if (!_impl) {
          return NULL;
        }
        return _impl->mountpoint();
      }

      inline void FSImpl::mountpoint(const char *mp) {
        _mountpoint = mp;
      }

      inline const char *FSImpl::mountpoint() {
        return _mountpoint;
      }


    }  // namespace fs


    #if __has_include(<SD.h>)

      #if !defined SDFAT_FILE_TYPE
        #define SDFAT_FILE_TYPE 3 // support all filesystem types (fat16/fat32/ExFat)
      #endif
      #if SDFAT_FILE_TYPE!=3
        #error "ESP32-targz only supports SdFs with SDFAT_FILE_TYPE 3"
      #endif

      #include <SD.h>

      // cfr https://en.cppreference.com/w/c/io/fopen + guesses
      inline int _convert_sdfs_access_mode_to_flag(const char* mode, const bool create = false)
      {
        int mode_chars = strlen(mode);
        if (mode_chars==0) return O_RDONLY;
        if (mode_chars==1) {
          if (mode[0]=='r') return O_RDONLY;
          if (mode[0]=='w') return O_WRONLY | create ? O_CREAT : 0;
          if (mode[0]=='a') return O_APPEND | create ? O_CREAT : 0;
        }
        if (mode_chars==2) {
          if (mode[1] ==  '+') {
            if (mode[0] == 'r') return O_RDWR;
            if (mode[0] == 'w') return O_RDWR | O_CREAT;
            if (mode[0] == 'a') return O_RDWR | O_APPEND | O_CREAT;
          }
        }
        return O_RDONLY;
      }

      class SdFsFileImpl : public fs::FileImpl
      {
        private:
          mutable FsFile _file;
        public:
          SdFsFileImpl(FsFile file) : _file(file) {}
          virtual ~SdFsFileImpl() { }

          virtual size_t write(const uint8_t *buf, size_t size) { return _file.write(buf, size); }
          virtual size_t read(uint8_t* buf, size_t size) { return _file.read(buf, size); }
          virtual void flush() { return _file.flush(); }
          virtual size_t position() const { return _file.curPosition(); }
          virtual size_t size() const { return _file.size(); }
          virtual void close() { _file.close(); }
          virtual operator bool() { return _file.operator bool(); }
          virtual boolean isDirectory(void) { return _file.isDirectory(); }
          virtual fs::FileImplPtr openNextFile(const char* mode) { return  std::make_shared<SdFsFileImpl>(_file.openNextFile(_convert_sdfs_access_mode_to_flag(mode))); }
          virtual boolean seekDir(long position) { return _file.seek(position); }
          virtual bool seek(uint32_t pos, SeekMode mode)
          {
            if (mode == SeekMode::SeekSet) {
              return _file.seek(pos);
            } else if (mode == SeekMode::SeekCur) {
              return _file.seek(position()+ pos);
            } else if (mode == SeekMode::SeekEnd) {
              return _file.seek(size()-pos);
            }
            return false;
          }
          virtual const char* name() const
          {
            // static, so if one asks the name of another file the same buffer will be used.
            // so we assume here the name ptr is not kept. (anyhow how would it be dereferenced and then cleaned...)
            static char _name[256];
            _file.getName(_name, sizeof(_name));
            return _name;
          }

          virtual String getNextFileName(void) { /* not implemented and not needed */ return String("Unimplemented"); }
          virtual String getNextFileName(bool*) { /* not implemented and not needed */ return String("Unimplemented"); }
          virtual time_t getLastWrite() { /* not implemented and not needed */  return 0; }
          virtual const char* path() const { /* not implemented and not needed */ return nullptr; }
          virtual bool setBufferSize(size_t size) { /* not implemented and not needed */ return false; }
          virtual void rewindDirectory(void) { /* not implemented and not needed */  }
      };


      class SdFsFSImpl : public fs::FSImpl
      {
        SdFs& sd;
        public:
          SdFsFSImpl(SdFs& sd) : sd(sd) { }
          virtual ~SdFsFSImpl() {}
          virtual fs::FileImplPtr open(const char* path, const char* mode, const bool create)
          {
              return std::make_shared<SdFsFileImpl>(sd.open(path, _convert_sdfs_access_mode_to_flag(mode, create)));
          }
          virtual bool exists(const char* path) { return sd.exists(path); }
          virtual bool rename(const char* pathFrom, const char* pathTo) { return sd.rename(pathFrom, pathTo); }
          virtual bool remove(const char* path) { return sd.remove(path); }
          virtual bool mkdir(const char *path) { return sd.mkdir(path); }
          virtual bool rmdir(const char *path) { return sd.rmdir(path); }
      };

    #endif



    #if __has_include(<LittleFS.h>)

      #include <LittleFS.h>

      // cfr https://en.cppreference.com/w/c/io/fopen + guesses
      inline int _convert_littlefs_access_mode_to_flag(const char* mode, const bool create = false)
      {
        int mode_chars = strlen(mode);
        if (mode_chars==0) return LFS_O_RDONLY;
        if (mode_chars==1) {
          if (mode[0]=='r') return LFS_O_RDONLY;
          if (mode[0]=='w') return LFS_O_WRONLY | create ? LFS_O_CREAT : 0;
          if (mode[0]=='a') return LFS_O_APPEND | create ? LFS_O_CREAT : 0;
        }
        if (mode_chars==2) {
          if (mode[1] ==  '+') {
            if (mode[0] == 'r') return LFS_O_RDWR;
            if (mode[0] == 'w') return LFS_O_RDWR | LFS_O_CREAT;
            if (mode[0] == 'a') return LFS_O_RDWR | LFS_O_APPEND | LFS_O_CREAT;
          }
        }
        return O_RDONLY;
      }


      class LittleFsFileImpl : public fs::FileImpl
      {
        private:
          mutable File _file;
        public:
          LittleFsFileImpl(File file) : _file(file) {}
          virtual ~LittleFsFileImpl() { }

          virtual size_t write(const uint8_t *buf, size_t size) { return _file.write(buf, size); }
          virtual size_t read(uint8_t* buf, size_t size) { return _file.read(buf, size); }
          virtual void flush() { return _file.flush(); }
          virtual size_t position() const { return _file.position(); }
          virtual size_t size() const { return _file.size(); }
          virtual void close() { _file.close(); }
          virtual operator bool() { return _file.operator bool(); }
          virtual boolean isDirectory(void) { return _file.isDirectory(); }
          virtual fs::FileImplPtr openNextFile(const char* mode) { return  openNextFile(mode); }
          virtual boolean seekDir(long position) { return _file.seek(position); }
          virtual bool seek(uint32_t pos, SeekMode mode) { return _file.seek(pos, mode); }
          virtual const char* name() const { return _file.name(); }
          virtual String getNextFileName(void) { /* not implemented and not needed */ return String("Unimplemented"); }
          virtual String getNextFileName(bool*) { /* not implemented and not needed */ return String("Unimplemented"); }
          virtual time_t getLastWrite()
          {
            DateTimeFields tm;
            if( _file.getModifyTime(tm) )
              return makeTime(tm);
            return 0;
          }
          virtual const char* path() const { /* not implemented and not needed */ return nullptr; }
          virtual bool setBufferSize(size_t size) { /* not implemented and not needed */ return false; }
          virtual void rewindDirectory(void) { /* not implemented and not needed */  }
      };


      template<typename LFS_Sibling>
      class LittleFsFSImpl : public fs::FSImpl
      {
        LFS_Sibling& lfs;
        public:
          LittleFsFSImpl(LFS_Sibling& lfs) : lfs(lfs) { }
          virtual ~LittleFsFSImpl() {}
          virtual fs::FileImplPtr open(const char* path, const char* mode, const bool create)
          {
              return std::make_shared<LittleFsFileImpl>(lfs.open(path, _convert_littlefs_access_mode_to_flag(mode, create)));
          }
          virtual bool exists(const char* path) { return lfs.exists(path); }
          virtual bool rename(const char* pathFrom, const char* pathTo) { return lfs.rename(pathFrom, pathTo); }
          virtual bool remove(const char* path) { return lfs.remove(path); }
          virtual bool mkdir(const char *path) { return lfs.mkdir(path); }
          virtual bool rmdir(const char *path) { return lfs.rmdir(path); }
      };

    #endif



    // filesystem wrapper -> to fs::FS
    template<typename inputFS>
    inline fs::FS unifyFS( inputFS &fs )
    {
      #if __has_include(<SD.h>)
        if ( std::is_same<inputFS, SdFs>::value ) {
          SdFs* fsPtr = (SdFs*)&fs;
          return fs::FS(fs::FSImplPtr(new SdFsFSImpl(*fsPtr)));
        }
      #endif
      #if __has_include(<LittleFS.h>)
        if (std::is_same<inputFS, LittleFS>::value) {
          LittleFS* fsPtr = (LittleFS*)&fs;
          return fs::FS(fs::FSImplPtr(new LittleFsFSImpl(*fsPtr)));
        }
        if (std::is_same<inputFS, LittleFS_Program>::value) {
          LittleFS_Program* fsPtr = (LittleFS_Program*)&fs;
          return fs::FS(fs::FSImplPtr(new LittleFsFSImpl(*fsPtr)));
        }
        if (std::is_same<inputFS, LittleFS_SPI>::value) {
          LittleFS_SPI* fsPtr = (LittleFS_SPI*)&fs;
          return fs::FS(fs::FSImplPtr(new LittleFsFSImpl(*fsPtr)));
        }
        if (std::is_same<inputFS, LittleFS_SPIFlash>::value) {
          LittleFS_SPIFlash* fsPtr = (LittleFS_SPIFlash*)&fs;
          return fs::FS(fs::FSImplPtr(new LittleFsFSImpl(*fsPtr)));
        }
        if (std::is_same<inputFS, LittleFS_SPINAND>::value) {
          LittleFS_SPINAND* fsPtr = (LittleFS_SPINAND*)&fs;
          return fs::FS(fs::FSImplPtr(new LittleFsFSImpl(*fsPtr)));
        }
        if (std::is_same<inputFS, LittleFS_SPIFram>::value) {
          LittleFS_SPIFram* fsPtr = (LittleFS_SPIFram*)&fs;
          return fs::FS(fs::FSImplPtr(new LittleFsFSImpl(*fsPtr)));
        }
        if (std::is_same<inputFS, LittleFS_RAM>::value) {
          LittleFS_RAM* fsPtr = (LittleFS_RAM*)&fs;
          return fs::FS(fs::FSImplPtr(new LittleFsFSImpl(*fsPtr)));
        }
        if (std::is_same<inputFS, LittleFS_QSPIFlash>::value) {
          LittleFS_QSPIFlash* fsPtr = (LittleFS_QSPIFlash*)&fs;
          return fs::FS(fs::FSImplPtr(new LittleFsFSImpl(*fsPtr)));
        }
        #if defined ARDUINO_TEENSY41 || defined ARDUINO_TEENSY40
          if (std::is_same<inputFS, LittleFS_QSPI>::value) {
            LittleFS_QSPI* fsPtr = (LittleFS_QSPI*)&fs;
            return fs::FS(fs::FSImplPtr(new LittleFsFSImpl(*fsPtr)));
          }
          if (std::is_same<inputFS, LittleFS_QPINAND>::value) {
            LittleFS_QPINAND* fsPtr = (LittleFS_QPINAND*)&fs;
            return fs::FS(fs::FSImplPtr(new LittleFsFSImpl(*fsPtr)));
          }
        #endif // defined ARDUINO_TEENSY41 || defined ARDUINO_TEENSY40
      #endif // __has_include(<LittleFS.h>)

      return fs::FS(nullptr);
    }


  #endif  //TGZ_FSIMPL_H

#endif
