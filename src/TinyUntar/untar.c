#include "untar.h"

char *empty_string = "";

int parse_header(const unsigned char buffer[512], header_t *header) {
  memcpy(header, buffer, sizeof(header_t));

  return 0;
}

static void log_error(const char *message) {
  printf("ERROR: %s\n", message);
}

static void log_debug(const char *message) {
  printf("DEBUG: %s\n", message);
}

unsigned long long decode_base256(const unsigned char *buffer) {
  return 0;
}

static void dump_hex(const char *ptr, int length) {
  int i = 0;
  printf("DUMP: ");
  while(i < length) {
    printf("%c", (ptr[i] >= 0x20 ? ptr[i] : '.'));
    i++;
  }
  printf("\n");
  i = 0;
  while(i < length) {
    printf("%X ", ptr[i]);

    i++;
  }
  printf("\n\n");
}

char *trim(char *raw, int length) {
  int i = 0;
  int j = length - 1;
  int is_empty = 0;
  // Determine left padding.
  while((raw[i] == 0 || raw[i] == ' ')) {
    i++;
    if(i >= length) {
      is_empty = 1;
      break;
    }
  }
  if(is_empty == 1)
    return empty_string;
  // Determine right padding.
  while((raw[j] == 0 || raw[j] == ' ')) {
    j--;
    if(j <= i)
      break;
  }
  // Place the terminator.
  raw[j + 1] = 0;
  // Return an offset pointer.
  return &raw[i];
}

int translate_header(header_t *raw_header, header_translated_t *parsed) {
  char buffer[101];
  char *buffer_ptr;
  const int R_OCTAL = 8;
  //
  memcpy(buffer, raw_header->filename, 100);
  buffer_ptr = trim(buffer, 100);
  strcpy(parsed->filename, buffer_ptr);
  parsed->filename[strlen(buffer_ptr)] = 0;
  //
  memcpy(buffer, raw_header->filemode, 8);
  buffer_ptr = trim(buffer, 8);

  if(IS_BASE256_ENCODED(buffer) != 0)
      parsed->filemode = decode_base256((const unsigned char *)buffer_ptr);
  else
      parsed->filemode = strtoull(buffer_ptr, NULL, R_OCTAL);
  //
  memcpy(buffer, raw_header->uid, 8);
  buffer_ptr = trim(buffer, 8);

  if(IS_BASE256_ENCODED(buffer) != 0)
      parsed->uid = decode_base256((const unsigned char *)buffer_ptr);
  else
      parsed->uid = strtoull(buffer_ptr, NULL, R_OCTAL);
  //
  memcpy(buffer, raw_header->gid, 8);
  buffer_ptr = trim(buffer, 8);

  if(IS_BASE256_ENCODED(buffer) != 0)
      parsed->gid = decode_base256((const unsigned char *)buffer_ptr);
  else
      parsed->gid = strtoull(buffer_ptr, NULL, R_OCTAL);
  //
  memcpy(buffer, raw_header->filesize, 12);
  buffer_ptr = trim(buffer, 12);

  if(IS_BASE256_ENCODED(buffer) != 0)
      parsed->filesize = decode_base256((const unsigned char *)buffer_ptr);
  else
      parsed->filesize = strtoull(buffer_ptr, NULL, R_OCTAL);
  //
  memcpy(buffer, raw_header->mtime, 12);
  buffer_ptr = trim(buffer, 12);

  if(IS_BASE256_ENCODED(buffer) != 0)
      parsed->mtime = decode_base256((const unsigned char *)buffer_ptr);
  else
      parsed->mtime = strtoull(buffer_ptr, NULL, R_OCTAL);
  //
  memcpy(buffer, raw_header->checksum, 8);
  buffer_ptr = trim(buffer, 8);

  if(IS_BASE256_ENCODED(buffer) != 0)
      parsed->checksum = decode_base256((const unsigned char *)buffer_ptr);
  else
      parsed->checksum = strtoull(buffer_ptr, NULL, R_OCTAL);
  //
  parsed->type = get_type_from_char(raw_header->type);

  memcpy(buffer, raw_header->link_target, 100);
  buffer_ptr = trim(buffer, 100);
  strcpy(parsed->link_target, buffer_ptr);
  parsed->link_target[strlen(buffer_ptr)] = 0;
  //
  memcpy(buffer, raw_header->ustar_indicator, 6);
  buffer_ptr = trim(buffer, 6);
  strcpy(parsed->ustar_indicator, buffer_ptr);
  parsed->ustar_indicator[strlen(buffer_ptr)] = 0;
  //
  memcpy(buffer, raw_header->ustar_version, 2);
  buffer_ptr = trim(buffer, 2);
  strcpy(parsed->ustar_version, buffer_ptr);
  parsed->ustar_version[strlen(buffer_ptr)] = 0;

  if(strcmp(parsed->ustar_indicator, "ustar") == 0) {
    //
    memcpy(buffer, raw_header->user_name, 32);
    buffer_ptr = trim(buffer, 32);
    strcpy(parsed->user_name, buffer_ptr);
    parsed->user_name[strlen(buffer_ptr)] = 0;
    //
    memcpy(buffer, raw_header->group_name, 32);
    buffer_ptr = trim(buffer, 32);
    strcpy(parsed->group_name, buffer_ptr);
    parsed->group_name[strlen(buffer_ptr)] = 0;
    //
    memcpy(buffer, raw_header->device_major, 8);
    buffer_ptr = trim(buffer, 8);

    if(IS_BASE256_ENCODED(buffer) != 0)
        parsed->device_major = decode_base256((const unsigned char *)buffer_ptr);
    else
        parsed->device_major = strtoull(buffer_ptr, NULL, R_OCTAL);
    //
    memcpy(buffer, raw_header->device_minor, 8);
    buffer_ptr = trim(buffer, 8);

    if(IS_BASE256_ENCODED(buffer) != 0) {
      parsed->device_minor = decode_base256((const unsigned char *)buffer_ptr);
    } else {
      parsed->device_minor = strtoull(buffer_ptr, NULL, R_OCTAL);
    }
  } else {
    strcpy(parsed->user_name, "");
    strcpy(parsed->group_name, "");

    parsed->device_major = 0;
    parsed->device_minor = 0;
  }
  return 0;
}

static int read_block(unsigned char *buffer) {
  char message[200];
  int num_read;

  num_read = tinyUntarReadCallback(buffer, TAR_BLOCK_SIZE);

  if(num_read < TAR_BLOCK_SIZE) {
    snprintf(message,
      200,
      "Read has stopped short at (%d) count "
      "rather than (%d). Quitting under error.",
      num_read, TAR_BLOCK_SIZE
    );
    log_error(message);
    return -1;
  }
  return 0;
}


entry_callbacks_t *read_tar_callbacks = NULL;
unsigned char *read_buffer = NULL;
header_t header;
header_translated_t header_translated;
void *read_context_data = NULL;

int read_tar( entry_callbacks_t *callbacks, void *context_data) {
  if( read_tar_callbacks != NULL ) {
    read_tar_callbacks = NULL;
  }
  read_tar_callbacks = callbacks;
  read_context_data = context_data;
  read_buffer = (unsigned char*)malloc(TAR_BLOCK_SIZE + 1);

  //int header_checked = 0;
  int i;

  int num_blocks;
  int current_data_size;
  int entry_index = 0;
  int empty_count = 0;

  read_buffer[TAR_BLOCK_SIZE] = 0;

  // The end of the file is represented by two empty entries (which we
  // expediently identify by filename length).
  while(empty_count < 2) {
    if(read_block( read_buffer ) != 0)
        break;
    // If we haven't yet determined what format to support, read the
    // header of the next entry, now. This should be done only at the
    // top of the archive.
    if(parse_header(read_buffer, &header) != 0) {
        log_error("Could not understand the header of the first entry in the TAR.");
        free( read_buffer );
        read_tar_callbacks = NULL;
        return -3;
    } else if(strlen(header.filename) == 0) {
        empty_count++;
    } else {
      if(translate_header(&header, &header_translated) != 0) {
        log_error("Could not translate header.");
        free( read_buffer );
        read_tar_callbacks = NULL;
        return -4;
      }

      if(read_tar_callbacks->header_cb(&header_translated, entry_index, read_context_data) != 0) {
        log_error("Header callback failed.");
        free( read_buffer );
        read_tar_callbacks = NULL;
        return -5;
      }

      i = 0;
      int received_bytes = 0;
      num_blocks = GET_NUM_BLOCKS(header_translated.filesize);
      while(i < num_blocks) {
        if(read_block( read_buffer ) != 0) {
          log_error("Could not read block. File too short.");
          free( read_buffer );
          read_tar_callbacks = NULL;
          return -6;
        }

        if(i >= num_blocks - 1)
          current_data_size = get_last_block_portion_size(header_translated.filesize);
        else
          current_data_size = TAR_BLOCK_SIZE;

        read_buffer[current_data_size] = 0;

        if(read_tar_callbacks->data_cb(&header_translated, entry_index, read_context_data, read_buffer, current_data_size) != 0) {
          log_error("Data callback failed.");
          free( read_buffer );
          read_tar_callbacks = NULL;
          return -7;
        }
        i++;
        received_bytes += current_data_size;
      }

      if(read_tar_callbacks->end_cb(&header_translated, entry_index, read_context_data) != 0) {
        log_error("End callback failed.");
        free( read_buffer );
        read_tar_callbacks = NULL;
        return -5;
      }
    }

    entry_index++;
  }
  free( read_buffer );
  read_tar_callbacks = NULL;
  return 0;
}

void dump_header(header_translated_t *header) {
  printf("===========================================\n");
  printf("      filename: %s\n", header->filename);
  printf("      filemode: 0%o (%llu)\n", (unsigned int)header->filemode, header->filemode);
  printf("           uid: 0%o (%llu)\n", (unsigned int)header->uid, header->uid);
  printf("           gid: 0%o (%llu)\n", (unsigned int)header->gid, header->gid);
  printf("      filesize: 0%o (%llu)\n", (unsigned int)header->filesize, header->filesize);
  printf("         mtime: 0%o (%llu)\n", (unsigned int)header->mtime, header->mtime);
  printf("      checksum: 0%o (%llu)\n", (unsigned int)header->checksum, header->checksum);
  printf("          type: %d\n", header->type);
  printf("   link_target: %s\n", header->link_target);
  printf("\n");

  printf("     ustar ind: %s\n", header->ustar_indicator);
  printf("     ustar ver: %s\n", header->ustar_version);
  printf("     user name: %s\n", header->user_name);
  printf("    group name: %s\n", header->group_name);
  printf("device (major): %llu\n", header->device_major);
  printf("device (minor): %llu\n", header->device_minor);
  printf("\n");

  printf("  data blocks = %d\n", GET_NUM_BLOCKS(header->filesize));
  printf("  last block portion = %d\n", get_last_block_portion_size(header->filesize));
  printf("===========================================\n");
  printf("\n");
}

enum entry_type_e get_type_from_char(char raw_type) {
  switch(raw_type) {
    case TAR_T_NORMAL1:
    case TAR_T_NORMAL2:
        return T_NORMAL;

    case TAR_T_HARD:
        return T_HARDLINK;

    case TAR_T_SYMBOLIC:
        return T_SYMBOLIC;

    case TAR_T_CHARSPECIAL:
        return T_CHARSPECIAL;

    case TAR_T_BLOCKSPECIAL:
        return T_CHARSPECIAL;

    case TAR_T_DIRECTORY:
        return T_DIRECTORY;

    case TAR_T_FIFO:
        return T_FIFO;

    case TAR_T_CONTIGUOUS:
        return T_CONTIGUOUS;

    case TAR_T_GLOBALEXTENDED:
        return T_GLOBALEXTENDED;

    case TAR_T_EXTENDED:
        return T_EXTENDED;
  }

  return T_OTHER;
}

int inline get_last_block_portion_size(int filesize) {
  const int partial = filesize % TAR_BLOCK_SIZE;
  return (partial > 0 ? partial : TAR_BLOCK_SIZE);
}
