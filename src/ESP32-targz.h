#include "uzlib/uzlib.h"     // https://github.com/pfalcon/uzlib

extern "C" {
  #include "TinyUntar/untar.h" // https://github.com/dsoprea/TinyUntar
}

#include <FS.h>
#include <SPIFFS.h>
#include <Update.h>

fs::File untarredFile;
fs::FS *tarFS = NULL;

#define GZIP_DICT_SIZE 32768
#define GZIP_BUFF_SIZE 4096

// stores the gzip dictionnary, will eat 32KB ram and be freed afterwards
unsigned char *uzlib_gzip_dict = nullptr;

struct uzlib_uncomp uzLibDecompressor;

// checks if gzFile is a valid gzip file
bool uzLibFileIsGzip(fs::File &gzFile);
// uncompresses *gzipped* sourceFile to destFile, filesystems may differ
void uzFileExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFile );
// flashes the ESP with the content of a *gzipped* file
void uzUpdater( const char* gz_filename );

unsigned char __attribute__((aligned(4))) uzlib_read_cb_buff[GZIP_BUFF_SIZE];

struct TarGzStream {
  Stream *gz;
  Stream *tar;
  Stream *output;
  int32_t gz_size;
  int32_t output_size;
};

TarGzStream tarGzStream;


int8_t uzLibLastProgress = -1;

void uzLibProgressCallback( uint8_t progress ) {
  if( uzLibLastProgress != progress ) {
    uzLibLastProgress = progress;
    log_n("Progress: %d percent", progress );
  }
}


void (*uzLibWriteCallback)( unsigned char* buff, size_t buffsize );


static void uzLibUpdateWriteCallback( unsigned char* buff, size_t buffsize ) {
  Update.write( buff, buffsize );
}


static void uzLibStreamWriteCallback( unsigned char* buff, size_t buffsize ) {
  tarGzStream.output->write( buff, buffsize );
}


int uzLibStreamReadCallback( struct uzlib_uncomp *m ) {
  m->source = uzlib_read_cb_buff;
  m->source_limit = uzlib_read_cb_buff + GZIP_BUFF_SIZE;
  tarGzStream.gz->readBytes( uzlib_read_cb_buff, GZIP_BUFF_SIZE );
  return *( m->source++ );
}


uint8_t uzLibFileGetByte(fs::File &file, const uint32_t addr) {
  file.seek( addr );
  return file.read();
}

// check if a file has gzip headers, if so read its projected uncompressed size
bool uzLibFileIsGzip(fs::File &gzFile) {
  tarGzStream.output_size = 0;
  tarGzStream.gz_size = gzFile.size();
  bool ret = false;
  if ((uzLibFileGetByte(gzFile, 0) == 0x1f) && (uzLibFileGetByte(gzFile, 1) == 0x8b)) {
    // GZIP signature matched.  Find real size as encoded at the end
    tarGzStream.output_size =  uzLibFileGetByte(gzFile, tarGzStream.gz_size - 4);
    tarGzStream.output_size += uzLibFileGetByte(gzFile, tarGzStream.gz_size - 3)<<8;
    tarGzStream.output_size += uzLibFileGetByte(gzFile, tarGzStream.gz_size - 2)<<16;
    tarGzStream.output_size += uzLibFileGetByte(gzFile, tarGzStream.gz_size - 1)<<24;
    log_n("gzip file detected ! Left: %d, size:%d", tarGzStream.output_size, tarGzStream.gz_size);
    ret = true;
  }
  gzFile.seek(0);
  return ret;
}

int uzLibUncompress() {
  if( !tarGzStream.gz->available() ) {
    log_e("gz resource doesn't exist!");
    return 1;
  }
  uzlib_gzip_dict = new unsigned char[GZIP_DICT_SIZE];
  int32_t left = tarGzStream.output_size;

  uint8_t buffer[SPI_FLASH_SEC_SIZE];
  //struct uzlib_uncomp uzLibDecompressor;
  uzlib_init();
  uzLibDecompressor.source = NULL;
  uzLibDecompressor.source_limit = NULL;
  uzLibDecompressor.source_read_cb = uzLibStreamReadCallback;
  uzlib_uncompress_init(&uzLibDecompressor, uzlib_gzip_dict, GZIP_DICT_SIZE);
  int res = uzlib_gzip_parse_header(&uzLibDecompressor);
  if (res != TINF_OK) {
    log_e("uzlib_gzip_parse_header failed!");
    free( uzlib_gzip_dict );
    return 5; // Error uncompress header read
  }
  uzLibProgressCallback( 0 );
  while( left>0 ) {
    uzLibDecompressor.dest_start = buffer;
    uzLibDecompressor.dest = buffer;
    int to_read = (left > SPI_FLASH_SEC_SIZE) ? SPI_FLASH_SEC_SIZE : left;
    uzLibDecompressor.dest_limit = buffer + to_read;
    int res = uzlib_uncompress(&uzLibDecompressor);
    if ((res != TINF_DONE) && (res != TINF_OK)) {
      log_e("Error uncompressing data");
      uzLibProgressCallback( 0 );
      free( uzlib_gzip_dict );
      return 6; // Error uncompress body
    } else {
      uzLibProgressCallback( 100*(tarGzStream.output_size-left)/tarGzStream.output_size );
    }
    // Fill any remaining with 0xff
    for (int i = to_read; i < SPI_FLASH_SEC_SIZE; i++) {
        buffer[i] = 0xff;
    }
    uzLibWriteCallback( buffer, SPI_FLASH_SEC_SIZE );
    left  -= SPI_FLASH_SEC_SIZE;
  }
  uzLibProgressCallback( 100 );
  free( uzlib_gzip_dict );
  uzlib_gzip_dict = nullptr;
  return 0;
}


void uzFileExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS, const char* destFile ) {
  log_n("uzLib expander start!");
  fs::File gz = sourceFS.open( sourceFile );
  if( !uzLibFileIsGzip( gz ) ) {
    log_e("Not a valid gzip file");
    gz.close();
    return;
  }
  fs::File outfile = destFS.open( destFile, FILE_WRITE );

  tarGzStream.gz = &gz;
  tarGzStream.output = &outfile;
  uzLibWriteCallback = &uzLibStreamWriteCallback; // for regular unzipping
  int ret = uzLibUncompress();
  if( ret!=0 ) log_n("uzLibUncompress returned error code %d", ret);
  outfile.close();
  gz.close();

  log_n("uzLib expander finished!");
}





void uzUpdater( const char* gz_filename ) {

  log_n("uzLib SPIFFS Updater start!");

  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  fs::File gz = SPIFFS.open( gz_filename );

  if( !uzLibFileIsGzip( gz ) ) {
    log_e("Not a valid gzip file");
    gz.close();
    return;
  }

  tarGzStream.gz = &gz;
  uzLibWriteCallback = &uzLibUpdateWriteCallback; // for unzipping direct to flash
  Update.begin( ( ( tarGzStream.output_size + SPI_FLASH_SEC_SIZE-1 ) & ~( SPI_FLASH_SEC_SIZE-1 ) ) );
  int ret = uzLibUncompress();
  if( ret!=0 ) log_n("uzLibUncompress returned error code %d", ret);
  gz.close();

  if ( Update.end() ) {
    Serial.println( "OTA done!" );
    if ( Update.isFinished() ) {
      // yay
      log_n("Update finished !");
      ESP.restart();
    } else {
      Serial.println( "Update not finished? Something went wrong!" );
    }
  } else {
    Serial.println( "Error Occurred. Error #: " + String( Update.getError() ) );
  }
  log_n("uzLib SPIFFS Updater finished!");
}











/*
static void targzLibStreamWriteCallback( unsigned char* buff, size_t buffsize ) {
  //tarGzStream.output->write( buff, buffsize );
}
*/



int unTarLibHeaderCb(header_translated_t *proper,  int entry_index,  void *context_data) {

  dump_header(proper);

  if(proper->type == T_NORMAL) {
    char file_path[256] = "";

    strcat(file_path, "/tmp/");
    strcat(file_path, proper->filename);
    untarredFile = tarFS->open(file_path, FILE_WRITE);
    if(!untarredFile) {
      printf("Could not open [%s] for write.\n", file_path);
      return -1;
    }
    log_n("Successfully created %s file descriptor", file_path);
    tarGzStream.output = &untarredFile;
  } else {
    printf("Not writing non-normal file.\n\n");
  }
  return 0;
}

/*
int unTarLibStreamReadGzCallback( unsigned char* buff, size_t buffsize ) {
  //uzLibWriteCallback = &uzLibStreamWriteCallback; // for regular unzipping
}*/


int unTarLibStreamReadCallback( unsigned char* buff, size_t buffsize ) {
  return tarGzStream.tar->readBytes( buff, buffsize );
}

static uint32_t untarredBytesCount = 0;

int unTarLibStreamWriteCallback(header_translated_t *proper, int entry_index, void *context_data, unsigned char *block, int length) {
  if( tarGzStream.output ) {
    tarGzStream.output->write(block, length);
    untarredBytesCount+=length;
    //log_n("Wrote %d bytes", length);
    if( untarredBytesCount%(length*80) == 0 ) {
      Serial.println();
    } else {
      Serial.print(".");
    }
  }
  return 0;
}


int unTarLibEndCb(header_translated_t *proper, int entry_index, void *context_data) {
  if(untarredFile) {
    log_n("Final size: %d", untarredFile.size() );
    untarredFile.close();
  }
  return 0;
}


int untarFile(fs::FS &fs, const char* fileName) {

  tarFS = &fs;

  if( !fs.exists( fileName ) ) {
    log_n("Error: file %s does not exist or is not reachable", fileName);
    return 1;
  }

  if( !fs.exists("/tmp") ) {
    fs.mkdir("/tmp");
  }

  untarredBytesCount = 0;
  entry_callbacks_t entry_callbacks = {
    unTarLibHeaderCb,
    unTarLibStreamWriteCallback,
    unTarLibEndCb
  };

  fs::File tarFile = fs.open( fileName );
  tarGzStream.tar = &tarFile;
  tinyUntarReadCallback = &unTarLibStreamReadCallback; // unTarLibStreamReadCallback reads n bytes from tar file

  if(read_tar( &entry_callbacks, NULL ) != 0) {
    printf("Read failed.\n\n");
    return -2;
  }

  return 0;
}








int tarGzExpander( fs::FS sourceFS, const char* sourceFile, fs::FS destFS ) {
  log_n("targz expander start!");
  fs::File gz = sourceFS.open( sourceFile );
  if( !uzLibFileIsGzip( gz ) ) {
    log_e("Not a valid gzip file");
    gz.close();
    return 1;
  }
/*
  unsigned char *uzlib_gzip_dict = new unsigned char[GZIP_DICT_SIZE];
  int32_t left = tarGzStream.output_size;

  uint8_t buffer[SPI_FLASH_SEC_SIZE];
  //struct uzlib_uncomp uzLibDecompressor;
  uzlib_init();
  uzLibDecompressor.source = NULL;
  uzLibDecompressor.source_limit = NULL;
  uzLibDecompressor.source_read_cb = uzLibStreamReadCallback; //TODO: pipe this to untar
  uzlib_uncompress_init(&uzLibDecompressor, uzlib_gzip_dict, GZIP_DICT_SIZE);
  int res = uzlib_gzip_parse_header(&uzLibDecompressor);
  if (res != TINF_OK) {
    log_e("uzlib_gzip_parse_header failed!");
    free( uzlib_gzip_dict );
    return 5; // Error uncompress header read
  }

  if( !destFS.exists("/tmp") ) {
    destFS.mkdir("/tmp");
  }
  tarFS = &destFS;

  entry_callbacks_t entry_callbacks = {
    unTarLibHeaderCb,
    unTarLibStreamWriteCallback,
    unTarLibEndCb
  };
*/

  //fs::File tarFile = fs.open( "/tmp/tmp.tar" );
  //tarGzStream.tar = &tarFile;
  //tinyUntarReadCallback = &uzLibStreamWriteCallback;
  //if(read_tar( &entry_callbacks, NULL ) != 0) {
  //  printf("Read failed.\n\n");
  //  return -2;
  //}
  //tarGzStream.gz = &gz;
  //tarGzStream.output = &outfile;
  //uzLibWriteCallback = &targzLibStreamWriteCallback; // for regular unzipping
  //int ret = uzLibUncompress();
  //if( ret!=0 ) log_n("uzLibUncompress returned error code %d", ret);
  //outfile.close();
  gz.close();

  log_n("uzLib expander finished!");
  return 0;
}
