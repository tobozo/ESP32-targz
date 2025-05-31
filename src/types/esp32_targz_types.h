/*\

  MIT License

  Copyright (c) 2020-now tobozo

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


  Tradeoffs :
    - speed: fast decompression needs 32Kb memory
    - memory: reducing memory use by dropping the gz_dictionary is VERY slow and prevents tar->gz->filesystem direct streaming
    - space: limited filesystems (<512KB spiffs) need tar->gz->filesystem direct streaming

\*/

#pragma once

#include <vector>

// Common types **************************************************************


// Callbacks for getting free/total space left on *destination* device.
// Optional but recommended to prevent SPIFFS/LittleFS/FFat partitions
// to explode during stream writes.
typedef uint64_t (*fsTotalBytesCb)();
typedef uint64_t (*fsFreeBytesCb)();

// setup filesystem helpers (totalBytes, freeBytes)
// must be done from outside the library since FS is an abstraction of an abstraction :(
typedef void (*fsSetupCallbacks)( fsTotalBytesCb cbt, fsFreeBytesCb cbf );

// Callbacks for progress and misc output messages, default is verbose
typedef void (*totalProgressCallback)(size_t progress, size_t total); // raw values
typedef void (*genericProgressCallback)(uint8_t progress); // percent (0...100)
typedef void (*genericLoggerCallback)( const char* format, ... ); // same behaviour as printf()






// LibUnpacker types **************************************************************

#define CC_UNUSED __attribute__((unused))

#define GZIP_DICT_SIZE 32768

#if defined ESP8266
  #define GZIP_BUFF_SIZE 1024
#else
  #define GZIP_BUFF_SIZE 4096
#endif

namespace TAR
{
  struct header_translated_s;
  typedef struct header_translated_s header_translated_t; // forward declaration
}

// overridable gz stream writer
typedef bool (*gzStreamWriter)( unsigned char* buff, size_t buffsize );
// overridable gz byte reader (used when no dictionary set)
typedef unsigned int (*gzDestByteReader)(int offset, unsigned char *out);

// tar doesn't have a real progress, so provide a status instead
typedef void (*tarStatusProgressCb)( const char* name, size_t size, size_t total_unpacked );

// tar has --exclude support, also provide --include
typedef bool (*tarExcludeFilter)( TAR::header_translated_t *header );
typedef bool (*tarIncludeFilter)( TAR::header_translated_t *header );

// This is only to centralize error codes and spare the
// hassle of looking up in three different library folders

typedef enum tarGzErrorCode /* int8_t */
{

  ESP32_TARGZ_OK                         =   0,   // yay
  // general library errors
  ESP32_TARGZ_FS_ERROR                   =  -1,   // Filesystem error
  ESP32_TARGZ_STREAM_ERROR               =  -6,   // same as Filesystem error
  ESP32_TARGZ_UPDATE_INCOMPLETE          =  -7,   // Update not finished? Something went wrong
  ESP32_TARGZ_TAR_ERR_GZDEFL_FAIL        =  -38,  // Library error during deflating
  ESP32_TARGZ_TAR_ERR_GZREAD_FAIL        =  -39,  // Library error during gzip read
  ESP32_TARGZ_TAR_ERR_FILENAME_TOOLONG   =  -40,  // Library error during file creation
  ESP32_TARGZ_FS_FULL_ERROR              =  -100, // no space left on device
  ESP32_TARGZ_FS_WRITE_ERROR             =  -101, // no space left on device
  ESP32_TARGZ_FS_READSIZE_ERROR          =  -102, // no space left on device
  ESP32_TARGZ_HEAP_TOO_LOW               =  -103, // not enough heap
  ESP32_TARGZ_NEEDS_DICT                 =  -104, // gzip dictionnary needs to be enabled
  ESP32_TARGZ_UZLIB_PARSE_HEADER_FAILED  =  -105, // Gz Error when parsing header
  ESP32_TARGZ_UZLIB_MALLOC_FAIL          =  -106, // Gz Error when allocating memory
  ESP32_TARGZ_INTEGRITY_FAIL             =  -107, // General error, file integrity check fail

  // UZLIB: keeping error values from uzlib.h as is (no offset)
  ESP32_TARGZ_UZLIB_INVALID_FILE         =  -2,   // Not a valid gzip file
  ESP32_TARGZ_UZLIB_DATA_ERROR           =  -3,   // Gz Error TINF_DATA_ERROR
  ESP32_TARGZ_UZLIB_CHKSUM_ERROR         =  -4,   // Gz Error TINF_CHKSUM_ERROR
  ESP32_TARGZ_UZLIB_DICT_ERROR           =  -5,   // Gz Error TINF_DICT_ERROR
  ESP32_TARGZ_TAR_ERR_FILENAME_NOT_GZ    =  -41,  // Gz error, can't guess filename

  // UPDATE: adding -20 offset to actual error values from Update.h
  ESP32_TARGZ_UPDATE_ERROR_ABORT         =  -8,   // Updater Error UPDATE_ERROR_ABORT        -20   // (12-20) = -8
  ESP32_TARGZ_UPDATE_ERROR_BAD_ARGUMENT  =  -9,   // Updater Error UPDATE_ERROR_BAD_ARGUMENT -20   // (11-20) = -9
  ESP32_TARGZ_UPDATE_ERROR_NO_PARTITION  =  -10,  // Updater Error UPDATE_ERROR_NO_PARTITION -20   // (10-20) = -10
  ESP32_TARGZ_UPDATE_ERROR_ACTIVATE      =  -11,  // Updater Error UPDATE_ERROR_ACTIVATE     -20   // (9-20)  = -11
  ESP32_TARGZ_UPDATE_ERROR_MAGIC_BYTE    =  -12,  // Updater Error UPDATE_ERROR_MAGIC_BYTE   -20   // (8-20)  = -12
  ESP32_TARGZ_UPDATE_ERROR_MD5           =  -13,  // Updater Error UPDATE_ERROR_MD5          -20   // (7-20)  = -13
  ESP32_TARGZ_UPDATE_ERROR_STREAM        =  -14,  // Updater Error UPDATE_ERROR_STREAM       -20   // (6-20)  = -14
  ESP32_TARGZ_UPDATE_ERROR_SIZE          =  -15,  // Updater Error UPDATE_ERROR_SIZE         -20   // (5-20)  = -15
  ESP32_TARGZ_UPDATE_ERROR_SPACE         =  -16,  // Updater Error UPDATE_ERROR_SPACE        -20   // (4-20)  = -16
  ESP32_TARGZ_UPDATE_ERROR_READ          =  -17,  // Updater Error UPDATE_ERROR_READ         -20   // (3-20)  = -17
  ESP32_TARGZ_UPDATE_ERROR_ERASE         =  -18,  // Updater Error UPDATE_ERROR_ERASE        -20   // (2-20)  = -18
  ESP32_TARGZ_UPDATE_ERROR_WRITE         =  -19,  // Updater Error UPDATE_ERROR_WRITE        -20   // (1-20)  = -19

  // TAR: adding -30 offset to actual error values from untar.h
  ESP32_TARGZ_TAR_ERR_DATACB_FAIL        =  -32,  // Tar Error TAR_ERR_DATACB_FAIL       -30   // (-2-30) = -32
  ESP32_TARGZ_TAR_ERR_HEADERCB_FAIL      =  -33,  // Tar Error TAR_ERR_HEADERCB_FAIL     -30   // (-3-30) = -33
  ESP32_TARGZ_TAR_ERR_FOOTERCB_FAIL      =  -34,  // Tar Error TAR_ERR_FOOTERCB_FAIL     -30   // (-4-30) = -34
  ESP32_TARGZ_TAR_ERR_READBLOCK_FAIL     =  -35,  // Tar Error TAR_ERR_READBLOCK_FAIL    -30   // (-5-30) = -35
  ESP32_TARGZ_TAR_ERR_HEADERTRANS_FAIL   =  -36,  // Tar Error TAR_ERR_HEADERTRANS_FAIL  -30   // (-6-30) = -36
  ESP32_TARGZ_TAR_ERR_HEADERPARSE_FAIL   =  -37,  // Tar Error TAR_ERR_HEADERPARSE_FAIL  -30   // (-7-30) = -37
  ESP32_TARGZ_TAR_ERR_HEAP_TOO_LOW       =  -38,  // Tar Error TAR_ERROR_HEAP            -30   // (-8-30) = -38

} ErrorCodes ;






// LibPacker types **************************************************************

namespace LZPacker
{
  typedef size_t (*gzStreamReader_t)( uint8_t* buf, size_t bufsize );
  // default stream reader for LZPacker::compress(srcStream, srcLen, dstStream), can be overriden when source stream is null
  [[maybe_unused]] static gzStreamReader_t gzStreamReader = NULL;
}



namespace TAR
{

  // user list of entitites to add to a tar archive
  struct dir_entity_t
  {
    String path{""};
    bool is_dir{false};
    size_t size{0};
  };


  // same as dir entity but with save path
  struct tar_entity_t
  {
    String realpath{""}; // source path on filesystem
    String savepath{""}; // saved path in tar archive
    bool is_dir{false};
    size_t size{0};      // 0 if is_dir = true
  };

  typedef std::vector<dir_entity_t> dir_entities_t; // shorthand for users, not used in the library but still valid

  struct _tar_callback_t;
  typedef struct _tar_callback_t tar_callback_t; // forward declaration

  // settings for tar packer implementation
  struct tar_params_t
  {
    fs::FS *srcFS{nullptr};                // source filesystem
    std::vector<dir_entity_t> dirEntities; // entities to add, output_file_path will be ignored if present in the list
    fs::FS *dstFS{nullptr};                // destination filesystem
    const char* output_file_path{nullptr}; // destination archive path, may be .tar or .tar.gz
    const char* tar_prefix{nullptr};       // root directory in the tar archive (all paths in will be prefixed with this)
    tar_callback_t *io{nullptr};           // i/o functions for tar r/w operations
    int ret{-1};                           // return status, size of the processed tar bytes if successful
  };
}


#if defined ESP8266
  #define struct_stat_t struct stat
#elif defined ARDUINO_ARCH_RP2040
  // RP2040 loads stats.h twice and panics on ambiguity, let's hint
  #define struct_stat_t struct TAR::stat
#else
  #if __has_include(<sys/stat.h>)
    #define struct_stat_t struct stat
    #include <sys/stat.h>
  #else
    // struct stat is namespaced from libtar.h
    #define struct_stat_t struct TAR::stat
  #endif
#endif

