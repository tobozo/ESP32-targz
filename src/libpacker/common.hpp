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


namespace TAR
{

  [[maybe_unused]] inline static int max_path_len=255;


  struct tar_files_packer_t; // forward declaration
  struct dir_iterator_t;     // forward declaration

  struct dir_entity_t
  {
    String path{""};
    bool is_dir{false};
    size_t size{0};
  };


  enum tar_files_packer_step_t
  {
    PACK_FILES_SETUP,
    PACK_FILES_STEP,
    PACK_FILES_END
  };

  struct tar_files_packer_t
  {
    TAR *tar{nullptr};
    fs::FS*fs{nullptr};
    const char*tar_path{nullptr};
    const char* tgz_path{nullptr};
    const char*dst_path{nullptr};
    ssize_t *written_bytes{nullptr};
    tar_callback_t *io{nullptr};
    bool status{false};
    tar_files_packer_step_t step{PACK_FILES_SETUP};
    tar_files_packer_step_t entity_step{PACK_FILES_SETUP};
    dir_iterator_t *dirIterator{nullptr};
  };


  struct dir_iterator_t
  {

    tar_files_packer_t *p{nullptr};;
    std::vector<dir_entity_t> *dirEntities{nullptr};
    int iterator{0};
    dir_entity_t dirEntity{"",false,0}; // last parsed entity
    entity_t entity{ nullptr, nullptr, nullptr, nullptr, ENTITY_STAT, 0 };
    String SavePath{""};

    inline bool available() {
      if( !p || !dirEntities ) {
        log_e("No packer!");
        return false;
      }

      if( !dirEntities || dirEntities->size() == 0 ) {
        log_e("No root entity!");
        return false;
      }
      dirEntity = dirEntities->at(0);
      return dirEntity.is_dir;
    }

    inline bool complete() {
      assert(p);
      assert( dirEntities );
      return iterator >= dirEntities->size();
    }

    inline bool stepComplete() {
      return entity.step == ENTITY_END;
    }

    inline bool next() {
      assert(p);
      assert( dirEntities );
      dirEntity = dirEntities->at(iterator);
      SavePath = p->dst_path ? String(p->dst_path)+dirEntity.path : "";
      if(iterator==0) // root dir
        entity = { p->tar, dirEntity.path.c_str(), SavePath.c_str(), p->written_bytes, ENTITY_STAT, -1 };
      else // child entity (dir or file)
        entity = { p->tar, dirEntity.path.c_str(), (p->dst_path ? SavePath.c_str() : NULL), p->written_bytes, ENTITY_STAT, -1 };
      iterator++;
      // recursion prevention: don't include target output file in the archive ;)
      return ! (p->tgz_path != NULL && dirEntity.path==String(p->tgz_path)); // true = process, false = filter
    }

  };



  // helper function to create dirEntities from a given folder
  inline void collectDirEntities(std::vector<dir_entity_t> *dirEntities, fs::FS *fs, const char *dirname, uint8_t levels)
  {
    assert(fs);
    assert(dirname);

    File root = fs->open(dirname);
    if (!root) {
      log_e("Failed to open directory %s", dirname);
      return;
    }
    if (!root.isDirectory()) {
      log_e("Not a directory %s", dirname);
      return;
    }
    log_v("  DIR: %-16s", dirname);

    dirEntities->push_back( { String(dirname), true, 0 } );

    File file = root.openNextFile();

    while (file) {
      if (file.isDirectory()) {
        if (levels) {
          collectDirEntities(dirEntities, fs, file.path(), levels - 1);
        }
      } else {
        dirEntities->push_back( { String(file.path()), false, file.size() } );
        log_v("  FILE: %-16s\tSIZE: %6d", file.name(), file.size() );
      }
      file = root.openNextFile();
    }
  }


}
