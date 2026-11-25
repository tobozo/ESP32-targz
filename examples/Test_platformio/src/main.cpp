#include <ESP32-targz.h>

static TAR::entry_route_t routeToFilesystem(const TAR::entry_info_t&, void*)
{
  return TAR::ENTRY_ROUTE_FILESYSTEM;
}

static bool acceptEntryBlock(const TAR::entry_info_t&, const uint8_t*, size_t, void*)
{
  return true;
}

static bool completeEntry(const TAR::entry_info_t&, void*)
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
  TAR::entry_router_t router = {
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
