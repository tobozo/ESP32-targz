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
#endif


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
    fs::FS *srcFS;
    std::vector<dir_entity_t> dirEntities;
    fs::FS *dstFS;
    const char* output_file_path; // may be .tar or .tar.gz
    const char* tar_prefix;
    tar_callback_t *io;
    int ret;
  };


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
    //log_v("  DIR: %-16s", dirname);

    if( String(dirname) != "/" )
      dirEntities->push_back( { String(dirname), true, 0 } );

    File file = root.openNextFile();

    while (file) {
      const char* file_path =
        #if defined ESP32
          file.path()
        #elif defined ESP8266 || defined ARDUINO_ARCH_RP2040
          file.fullName()
        #else
          #error
        #endif
      ;
      if (file.isDirectory()) {
        if (levels) {
          collectDirEntities(dirEntities, fs, file_path, levels - 1);
        }
      } else {
        dirEntities->push_back( { String(file_path), false, file.size() } );
        //log_v("  FILE: %-16s\tSIZE: %6d", file.name(), file.size() );
      }
      file = root.openNextFile();
    }
  }


}
