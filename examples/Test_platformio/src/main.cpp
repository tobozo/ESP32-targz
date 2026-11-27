#include <ESP32-targz.h>

static TAR::EntryRoute routeToFilesystem(const TAR::EntryInfo&, void*)
{
  return TAR::ENTRY_ROUTE_FILESYSTEM;
}

static bool acceptEntryBlock(const TAR::EntryInfo&, const uint8_t*, size_t, void*)
{
  return true;
}

static bool completeEntry(const TAR::EntryInfo&, void*)
{
  return true;
}

void setup()
{
  Serial.begin(115200);

  TarUnpacker *TARUnpacker     = new TarUnpacker();
  GzUnpacker *GZUnpacker       = new GzUnpacker();
  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

  // API smoke test: returning FILESYSTEM preserves legacy extraction.
  TAR::EntryRouter router = {
    routeToFilesystem,
    acceptEntryBlock,
    completeEntry,
    nullptr,
    nullptr
  };
  TARGZUnpacker->setTarEntryRouter(&router);

}


void loop()
{

}
