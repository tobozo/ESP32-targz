#pragma once

#include "../ESP32-targz-lib.hpp"

namespace TAR
{
  #include "../tar/libtar.h"
}

namespace GZ
{
  #include "../uzlib/uzlib.h"
}


#if defined ARDUINO_ARCH_RP2040

  #include <FreeRTOS.h>
  #include <task.h>
  #include <queue.h>
  // RP2040 loads stats.h twice and panics on ambiguity, let's hint
  #define struct_stat_t struct TAR::stat
  // not sure what version of FreeRTOS is required, but xPortGetCoreID() appears to be missing
  #define xPortGetCoreID get_core_num

#else
  #define struct_stat_t struct stat
#endif


namespace LZPacker
{
  typedef size_t (*gzStreamReader_t)( uint8_t* buf, size_t bufsize );
  [[maybe_unused]] static gzStreamReader_t gzStreamReader = NULL;
}



namespace TAR
{

  [[maybe_unused]] inline static int max_path_len=255;

  struct dir_entity_t
  {
    String path{""};
    bool is_dir{false};
    size_t size{0};
  };


  struct tar_entity_t
  {
    String realpath{""}; // source path on filesystem
    String savepath{""}; // dst path in tar archive
    bool is_dir{false};
    size_t size{0};
  };


  struct tar_params_t
  {
    fs::FS *srcFS{nullptr};
    std::vector<dir_entity_t> dirEntities;
    fs::FS *dstFS{nullptr};
    const char* output_file_path{nullptr}; // may be .tar or .tar.gz
    const char* tar_prefix{nullptr};
    tar_callback_t *io{nullptr};
    int ret{-1};
  };


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



  // helper function to create dirEntities from a given folder
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


}
