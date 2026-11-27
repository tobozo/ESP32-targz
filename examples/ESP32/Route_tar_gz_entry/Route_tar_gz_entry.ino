/*
 * Route_tar_gz_entry.ino
 *
 * Demonstrates routing one archive member to an application sink while the
 * rest of the .tar.gz archive is extracted normally to LittleFS.
 *
 * Upload examples/Test_tar_gz_tgz/data/targz_example.tar.gz to LittleFS as
 * /targz_example.tar.gz before running this sketch. The selected member is
 * zombo.com/index.html from that archive.
 */

#ifndef ESP32
  #error "tarGzStreamExpander is only available on ESP32 architecture"
#endif

#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>

struct RoutedEntryState {
  uint64_t received = 0;
};

static TAR::EntryRoute selectEntry(const TAR::EntryInfo& entry, void*)
{
  if( entry.type == TAR::T_NORMAL && strcmp(entry.path, "zombo.com/index.html") == 0 ) {
    Serial.printf("Routing %s (%llu bytes) to the custom sink\n", entry.path, entry.size);
    return TAR::ENTRY_ROUTE_CUSTOM;
  }
  return TAR::ENTRY_ROUTE_FILESYSTEM;
}

static bool consumeEntryBlock(const TAR::EntryInfo&, const uint8_t*, size_t size, void* context)
{
  // Replace this counter with an application writer, e.g. an OTA Update.write.
  static_cast<RoutedEntryState*>(context)->received += size;
  return true;
}

static bool completeEntry(const TAR::EntryInfo& entry, void* context)
{
  const auto* state = static_cast<RoutedEntryState*>(context);
  Serial.printf("Custom sink received %llu bytes for %s\n", state->received, entry.path);
  return state->received == entry.size;
}

static void abortEntry(const TAR::EntryInfo& entry, void*)
{
  Serial.printf("Custom sink aborted for %s\n", entry.path);
}

void setup()
{
  Serial.begin(115200);
  if( !tarGzFS.begin(true) ) {
    Serial.println("Could not start LittleFS");
    return;
  }

  File archive = tarGzFS.open("/targz_example.tar.gz", "r");
  if( !archive ) {
    Serial.println("Upload /targz_example.tar.gz to LittleFS first");
    return;
  }

  RoutedEntryState state;
  TAR::EntryRouter router = {
    selectEntry,
    consumeEntryBlock,
    completeEntry,
    abortEntry,
    &state
  };

  TarGzUnpacker unpacker;
  unpacker.setLoggerCallback(BaseUnpacker::targzPrintLoggerCallback);
  unpacker.setTarEntryRouter(&router);

  if( !unpacker.tarGzStreamExpander(&archive, tarGzFS, "/", archive.size()) ) {
    Serial.printf("Expansion failed with error %d\n", unpacker.tarGzGetError());
  }
  archive.close();
}

void loop()
{
}
