// Set **destination** filesystem by uncommenting one of these:
//#define DEST_FS_USES_SPIFFS
#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only

#include <ESP32-targz.h>
#include "./GzipStaticHandler.h"

#define HOSTNAME "esp32targz"

static const char notFoundContent[] PROGMEM = R"==(
<html>
<head>
  <title>Resource not found</title>
</head>
<body>
  <p>The resource was not found.</p>
  <p><a href="/">Start again</a></p>
</body>
)==";


size_t compressFile( fs::FS &fs, const String &inputFilename, bool force=false )
{
  if(!fs.exists(inputFilename)) {
    Serial.printf("Filesystem is missing '%s' file, halting\n", inputFilename.c_str());
    while(1) yield();
  }

  String gzFilename = inputFilename+".gz";
  size_t ret = 0;
  size_t gzFileSize = 0;

  File gzFile;

  File inputFile = fs.open(inputFilename, "r");
  size_t uncompressedSize = inputFile.size();

  if( !inputFile || uncompressedSize==0 ) {
    Serial.printf("Unable to open '%s' file or file empty\n", inputFilename.c_str());
    goto _close_input_file;
  }

  if( !force && uncompressedSize < 512 ) { // compressed size will be higher than uncompressed size
    Serial.printf("File '%s' is not worth compressing (%d bytes only).\n", inputFilename.c_str(), uncompressedSize);
    goto _close_input_file;
  }

  if(force || !fs.exists(gzFilename)) { // the gz file does not exits, compress!

    gzFile = fs.open(gzFilename, "w");
    if( !gzFile ) {
      Serial.printf("Unable to open '%s' file for writing\n", gzFilename.c_str());
      goto _close_input_file;
    }

    gzFileSize = LZPacker::compress( &inputFile, inputFile.size(), &gzFile );
    gzFile.close();

    if( gzFileSize == 0 || gzFileSize > uncompressedSize ) { // uh-oh
      fs.remove(gzFilename);
      goto _close_input_file;
    }

    Serial.printf("Compressed %s (%d bytes) to %s (%d bytes)\n", inputFilename.c_str(), inputFile.size(), gzFilename.c_str(), gzFileSize );
    ret = gzFileSize;

  } else { // the gz file exists, open it to retrieve its size

    gzFile = fs.open(gzFilename);
    if( !gzFile ) {// uh-oh, fs.exists() returned true but fs.open() failed
      Serial.printf("Output file '%s' exists but cannot be opened\n", gzFilename.c_str() );
      goto _close_output_file;
    }
    if( gzFile.size() == 0) { // some previous compression didn't go well?
      fs.remove(gzFilename);
      goto _close_output_file;
    }

    ret = gzFile.size();

    _close_output_file:
    gzFile.close();
   }

  _close_input_file:
  inputFile.close();
  return ret;
}




size_t getFileSize(fs::FS &fs, const char *fName)
{
  assert(fName);
  File f = fs.open(fName);
  if(!f) {
    Serial.printf("Unable to open '%s' for STAT\n", fName);
    return 0;
  }
  size_t fileSize = f.size();
  f.close();
  return fileSize;
}



void gzipDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(" - not a directory");
    return;
  }

  String fName, gzPath;
  size_t compressedSize;

  File file = root.openNextFile();

  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        gzipDir(fs, file.path(), levels - 1);
      }
    } else {
      fName = file.name();
      if( !fName.endsWith("gz") ) {
        compressedSize = 0;

        gzPath = String(file.path())+".gz";

        if( !fs.exists(gzPath) ) {
          compressedSize = compressFile( fs, file.path() );
        } else {
          compressedSize = getFileSize(fs, gzPath.c_str());
        }

        if( compressedSize > 0 )
          Serial.printf("  FILE: %-16s\tSIZE: %6d\tCOMPRESSED: %6d\n", file.name(), file.size(), compressedSize );
        else
          Serial.printf("  FILE: %-16s\tSIZE: %6d\n", file.name(), file.size() );
      }
    }

    file = root.openNextFile();
  }
}


WebServer server(80);

// client can send a header to regenerate gz cache i.e. after modifying the uncompressed source
const char* gzip_flush_header = "x-gzip-cache-flush";
GzCacheMiddleware mod_gzip( server, new GzStaticRequestHandler(tarGzFS, "/", "/", nullptr), gzip_flush_header );



void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-targz WebServer deflate test");

  if(!tarGzFS.begin()) {
    Serial.println("Failed to start filesystem, halting");
    while(1) yield();
  }

  // optional: pre-compress all files up to 3 folders deep from the root
  // gzipDir(tarGzFS, "/", 3);

  // allow to address the device by the given name e.g. http://webserver
  WiFi.setHostname(HOSTNAME);

  // start WiFI
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  printf("Connect to WiFi...\n");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    printf(".");
  }
  printf("connected.\n");

  server.onNotFound([]() { server.send(404, "text/html", FPSTR(notFoundContent)); });

  // attach stream compressor
  mod_gzip.addStreamCompressor( [](Stream *input, size_t len, Stream* output)->size_t { return LZPacker::compress(input, len, output); } );
  // attache file compressor
  mod_gzip.addFileCompressor( compressFile );
  // mod_gzip.enableCache(); // cache gz files
  mod_gzip.disableCache(); // ignore existing gz files, compress on the fly


  server.on("/json", []() { // send gz compressed JSON
      // building HTTP response without "Content-Length" header isn't 100% standard, so we have to do this
      int responseCode = 200;
      const char* myJsonData = "{\"ceci\":\"cela\",\"couci\":\"couça\",\"patati\":\"patata\"}";
      server.sendHeader(String(F("Content-Type")), String(F("application/json")), true);
      server.sendHeader(String(F("Content-Encoding")), String(F("gzip")));
      server.sendHeader(String(F("Connection")), String(F("close")));
      String HTTPResponse = String(F("HTTP/1.1"))+' '+String(responseCode)+' '+server.responseCodeToString(responseCode)+"\r\n";
      size_t headersCount = server.responseHeaders();
      for(size_t i=0;i<headersCount;i++)
        HTTPResponse.concat(server.responseHeaderName(i) + F(": ") + server.responseHeader(i) + F("\r\n"));
      HTTPResponse.concat(F("\r\n"));
      // sent HTTP response
      server.client().write(HTTPResponse.c_str(), HTTPResponse.length());

      // stream compressed json
      size_t compressed_size = LZPacker::compress( (uint8_t*)myJsonData, strlen(myJsonData), &server.client() );
      log_i("Sent %d compressed bytes", compressed_size);
  });


  server.on("/spiffs.tar.gz", []() { // compress all filesystem files/folders on the fly
    // building HTTP response without "Content-Length" header isn't 100% standard, so we have to do this
    int responseCode = 200;
    server.sendHeader(String(F("Content-Type")), String(F("application/tar+gzip")), true);
    server.sendHeader(String(F("Connection")), String(F("close")));
    String HTTPResponse = String(F("HTTP/1.1"))+' '+String(responseCode)+' '+server.responseCodeToString(responseCode)+"\r\n";
    size_t headersCount = server.responseHeaders();
    for(size_t i=0;i<headersCount;i++)
      HTTPResponse.concat(server.responseHeaderName(i) + F(": ") + server.responseHeader(i) + F("\r\n"));
    HTTPResponse.concat(F("\r\n"));
    // sent HTTP response
    server.client().write(HTTPResponse.c_str(), HTTPResponse.length());

    // stream tar.gz data
    std::vector<TAR::dir_entity_t> dirEntities; // storage for scanned dir entities
    TarPacker::collectDirEntities(&dirEntities, &tarGzFS, "/"); // collect dir and files
    size_t compressed_size = TarGzPacker::compress(&tarGzFS, dirEntities, &server.client());
    log_i("Sent %d compressed bytes", compressed_size);
  });


  server.addMiddleware( &mod_gzip );

  server.begin();

  printf("open <http://%s> or <http://%s>\n", WiFi.getHostname(), WiFi.localIP().toString().c_str());

}

void loop()
{
  server.handleClient();
}

