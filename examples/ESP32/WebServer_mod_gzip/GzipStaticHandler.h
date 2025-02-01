/*\

  MIT License

  Copyright (c) tobozo

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

  GzStaticRequestHandler is a derivate of ESP32 WebServer library, with added
  support for external content-encoders such as gzip or deflate..

  Usage:

    WebServer server(80);
    // client can send a header to regenerate gz cache i.e. after modifying the uncompressed source
    const char* gzip_flush_header = "x-gzip-cache-flush";
    GzCacheMiddleware mod_gzip( server, new GzStaticRequestHandler(LittleFS, "/", "/", nullptr), gzip_flush_header );

    // add mod_gzip as a middleware from setup()
    server.addMiddleware( &mod_gzip );

    // optional: attach a file compressor
    mod_gzip.addFileCompressor( [](fs::FS&fs, const String &inputFilename, bool flush_cache)->size_t { return myFileCompressor(fs, inputFilename, flush_cache); } );

    // optional: attach a stream compressor
    mod_gzip.addStreamCompressor( [](Stream *input, size_t len, Stream* output)->size_t { return myStreamCompressor(input, len, output); } );

    // affects cache behaviour: use gz files if exist, or create gz cache if addFileCompressor() was used
    mod_gzip.enableCache();

    // affects cache behaviour: ignore existing gz files, and compress on the fly if addStreamCompressor() was used
    mod_gzip.disableCache();

    server.begin();

\*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include "detail/RequestHandler.h" // grab from WebServer
#include "detail/mimetable.h"
#include "WString.h"
#include "Uri.h"
#include <MD5Builder.h>
#include <base64.h>


#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 1, 1)
  #error "Arduino-esp32 core 3.1.1 minimum required (depends on WebServer+Middleware)"
#endif


static const char ACCEPT_ENCODING_HEADER[] = "Accept-Encoding";
using namespace mime;

class GzStaticRequestHandler;
class GzCacheMiddleware;

using gzStreamCompressor = std::function<size_t(Stream *input, size_t len, Stream* output)>;
using gzFileCompressor = std::function<size_t(fs::FS&fs, const String &inputFilename, bool force)>;


class GzCacheMiddleware : public Middleware {
public:

  GzCacheMiddleware(WebServer &server, GzStaticRequestHandler *handler, const char*flush_header=nullptr);
  // optional: zlib/deflate "on the fly" compressor, returns gz data size
  static gzStreamCompressor streamGz;
  // optional: zlib/deflate "cache" compressor creates gz if required, returns gzipped file size
  static gzFileCompressor createGz;
  // Cache behaviour TL;DR: look for existing gz files when applicable.
  // When optional streamGz()/createGz() are set:
  //   - true  = compress to gz cache if not exists. ** needs createGz() **
  //   - false = compress on-the-fly, do not cache gz. ** needs streamGz() **
  // When optional streamGz()/createGz() are NOT set:
  //   - true  = serve gz if exists on filesystem, else serve uncompressed content
  //   - false = always serve uncompressed content
  static bool cacheGz;

  // Cache flush headers: assuming cacheGz is set to true, the gz cache is flushed when these headers are sent
  static const char *cache_flush_header;

  bool run(WebServer &server, Middleware::Callback next) override;

  void disableCache();
  void enableCache();
  void addStreamCompressor( gzStreamCompressor fn );
  void addFileCompressor( gzFileCompressor fn );
  void setCacheFlushHeader(const char*header);

private:
  WebServer* serverPtr = nullptr;
  GzStaticRequestHandler* gzHandler = nullptr;

};


class GzStaticRequestHandler : public RequestHandler {
public:
  GzStaticRequestHandler(FS &fs, const char *path, const char *uri, const char *cache_header);
  bool canHandle(HTTPMethod requestMethod, const String &requestUri) override;
  bool canHandle(WebServer &server, HTTPMethod requestMethod, const String &requestUri) override;
  bool handle(WebServer &_server, HTTPMethod requestMethod, const String &requestUri) override;
  static String getContentType(const String &path);
  static String calcETag(FS &fs, const String &path);
  GzStaticRequestHandler &setFilter(WebServer::FilterFunction filter);
protected:
  // _filter should return 'true' when the request should be handled
  // and 'false' when the request should be ignored
  WebServer::FilterFunction _filter;
  FS _fs;
  String _uri;
  String _path;
  String _cache_header;
  bool _isFile;
  size_t _baseUriLength;
};




namespace GzUtils
{


  // gzip/deflate negociator
  bool canHandleGz(WebServer &server)   {
    if( server.hasHeader(FPSTR(ACCEPT_ENCODING_HEADER)) ) {
      String acceptEncoding = server.header(FPSTR(ACCEPT_ENCODING_HEADER));
      if( acceptEncoding.indexOf("gzip") > 0 || acceptEncoding.indexOf("deflate") > 0 ) {
        return true;
      }
    }
    return false;
  }


  // gzip+cache stream function
  template<typename T> size_t stream(WebServer&server, fs::FS &fs, T &srcFile, const String &contentType, const int code = 200) {
    String HTTPResponse;
    // NOTE: this 'magic' header must be added manually using collectHeaders()
    bool overwrite = GzCacheMiddleware::cache_flush_header!=nullptr && server.hasHeader(String(GzCacheMiddleware::cache_flush_header));
    size_t compressedSized = 0;
    String path = String(srcFile.path());
    String pathWithGz;
    size_t i, headersCount;

    size_t len = srcFile.size();

    if( len<512 || path.endsWith(FPSTR(mimeTable[gz].endsWith)) ) {
      log_d("gz: file %s (%d bytes) does not need to be compressed", path.c_str(), len);
      goto _stream_file;
    }

    if( !GzUtils::canHandleGz(server) ) {
      log_d("gz: client didn't negociate gzip/deflate");
      goto _stream_file;
    }

    if( GzCacheMiddleware::cacheGz ) { // server has gz cache enabled
      if( GzCacheMiddleware::createGz ) { // a callback is attached, server will compress file to gz if necessary
        compressedSized = GzCacheMiddleware::createGz(fs, path, overwrite);
        if( compressedSized == 0 ) {
          log_e("gz cache miss: compression failed for %s", path);
          goto _stream_file;
        }
      }
      // check if gz cache exists
      pathWithGz = path + FPSTR(mimeTable[gz].endsWith);
      if( fs.exists(pathWithGz) ) {
        log_d("gz cache hit");
        srcFile.close(); // close the uncompressed file
        srcFile = fs.open(pathWithGz); // open gz cached file
        server.setContentLength( srcFile.size() ); // update content length
        goto _stream_file;
      }
    }

    if( !GzCacheMiddleware::streamGz ) { // streamed compression is unavailable, serve static
      log_d("no compressor attached");
      goto _stream_file;
    }

    log_d("on-the-fly gzip/deflate compression selected");

    // compress on the fly, do not sent "Content-Length" header
    server.sendHeader(String(F("Content-Type")), contentType, true); // TODO: verify content-type for emptiness (should default to text/html)
    server.sendHeader(F("Content-Encoding"), F("gzip")); // TODO: allow single/combined values of {gzip,compress,deflate,br,zstd}
    server.sendHeader(String(F("Connection")), String(F("close")));

    // Content-Length is unknown when gzipping on the fly, the HTTP response will be sent without Content-Length header.

    // WebServer::_prepareHeader() selects Chunked transfer encoding when the content length is unknown.
    // However Chunked transfer encoding is only available since HTTP 1.1 (with HTTP 1.0 the Content-Length header is optional).

    // Content-Length header cannot be sent because the gz size isn't known yet (compressing on the fly).
    // Chunked encoding cannot be used because WebServer::sendContent() function needs a non-zero contentLength argument.
    // Building a custom HTTP response is necessary.

    HTTPResponse = String(F("HTTP/1.1"))+' '+String(code)+' '+server.responseCodeToString(code)+"\r\n";
    headersCount = server.responseHeaders();
    for(i=0;i<headersCount;i++)
      HTTPResponse.concat(server.responseHeaderName(i) + F(": ") + server.responseHeader(i) + F("\r\n"));
    HTTPResponse.concat(F("\r\n"));
    log_d("Response headers: %s", HTTPResponse.c_str() );

    // stream gz data
    server.client().write(HTTPResponse.c_str(), HTTPResponse.length());
    return GzCacheMiddleware::streamGz(&srcFile, len, &server.client());

    _stream_file:
    return server.streamFile(srcFile, contentType, code);
  }

}; // end namespace



// ############## GzCacheMiddleware #################


// init static members
bool GzCacheMiddleware::cacheGz = true;
gzStreamCompressor GzCacheMiddleware::streamGz = nullptr;
gzFileCompressor GzCacheMiddleware::createGz = nullptr;
const char *GzCacheMiddleware::cache_flush_header = nullptr;

void setCacheFlushHeader(const char* header) {
  GzCacheMiddleware::cache_flush_header = header;
}

void GzCacheMiddleware::disableCache() {
  GzCacheMiddleware::cacheGz = false;
}


void GzCacheMiddleware::enableCache() {
  GzCacheMiddleware::cacheGz = true;
}


void GzCacheMiddleware::addStreamCompressor( gzStreamCompressor fn ) {
  GzCacheMiddleware::streamGz = fn;
}


void GzCacheMiddleware::addFileCompressor( gzFileCompressor fn ) {
  GzCacheMiddleware::createGz = fn;
}


GzCacheMiddleware::GzCacheMiddleware(WebServer &server, GzStaticRequestHandler *handler, const char *flush_header) {
  server.collectAllHeaders();
  server.addHandler((RequestHandler*)gzHandler);
  serverPtr = &server;
  gzHandler = handler;
  GzCacheMiddleware::cache_flush_header = flush_header;
}


bool GzCacheMiddleware::run(WebServer &server, Middleware::Callback next) {
  if (server.method() != HTTP_GET && server.method() != HTTP_HEAD )
    return next();

  assert(gzHandler);
  if( gzHandler->handle( server, server.method(), server.uri() ) )
    return true;
  return next();
}



// ############## GzStaticRequestHandler #################


GzStaticRequestHandler::GzStaticRequestHandler(FS &fs, const char *path, const char *uri, const char *cache_header) : _fs(fs), _uri(uri), _path(path), _cache_header(cache_header) {
  File f = fs.open(path);
  _isFile = (f && (!f.isDirectory()));
  log_w(
    "GzStaticRequestHandler: path=%s uri=%s isFile=%d, cache_header=%s\r\n", path, uri, _isFile, cache_header ? cache_header : ""
  );  // issue 5506 - cache_header can be nullptr
  _baseUriLength = _uri.length();
}


bool GzStaticRequestHandler::canHandle(HTTPMethod requestMethod, const String &requestUri) {
  switch(requestMethod) {
    case HTTP_GET:
    case HTTP_HEAD:
      // let it slide
    break;
    default: return false;
  }

  if ((_isFile && requestUri != _uri) || !requestUri.startsWith(_uri)) {
    return false;
  }

  return true;
}


bool GzStaticRequestHandler::canHandle(WebServer &server, HTTPMethod requestMethod, const String &requestUri) {
  switch(requestMethod) {
    case HTTP_GET:
    case HTTP_HEAD:
      // let it slide
    break;
    default: return false;
  }

  if ((_isFile && requestUri != _uri) || !requestUri.startsWith(_uri)) {
    return false;
  }

  if (_filter != NULL ? _filter(server) == false : false) {
    return false;
  }

  return true;
}


bool GzStaticRequestHandler::handle(WebServer &server, HTTPMethod requestMethod, const String &requestUri) {
  if (!canHandle(server, requestMethod, requestUri)) {
    return false;
  }

  String path(_path);

  if (!_isFile) {
    // Base URI doesn't point to a file.
    // If a directory is requested, look for index file.
    if (requestUri.endsWith("/")) {
      return handle(server, requestMethod, String(requestUri + "index.htm"));
    }

    // Append whatever follows this URI in request to get the file path.
    path += requestUri.substring(_baseUriLength);
  }

  String contentType = getContentType(path);

  // look for gz file, only if the requested file is not .gz **and** the client supports gzip/deflate
  if ( GzCacheMiddleware::cacheGz && GzUtils::canHandleGz(server) && !path.endsWith(FPSTR(mimeTable[gz].endsWith)) ) {
    String pathWithGz = path + FPSTR(mimeTable[gz].endsWith);
    if (_fs.exists(pathWithGz)) {
      path += FPSTR(mimeTable[gz].endsWith);
    }
  }

  File f = _fs.open(path, "r");
  if (!f || !f.available()) {
    return false;
  }

  String eTagCode;

  if (server._eTagEnabled) {
    if (server._eTagFunction) {
      eTagCode = (server._eTagFunction)(_fs, path);
    } else {
      eTagCode = calcETag(_fs, path);
    }

    if (server.header("If-None-Match") == eTagCode) {
      server.send(304);
      return true;
    }
  }

  if (_cache_header.length() != 0) {
    server.sendHeader("Cache-Control", _cache_header);
  }

  if ((server._eTagEnabled) && (eTagCode.length() > 0)) {
    server.sendHeader("ETag", eTagCode);
  }

  if( requestMethod == HTTP_GET )
    GzUtils::stream(server, _fs, f, contentType);
  else { // HTTP_HEAD
    server.setContentLength(f.size());
    server.send(200, contentType, "");
  }

  f.close();
  return true;
}


String GzStaticRequestHandler::getContentType(const String &path) {
  char buff[sizeof(mimeTable[0].mimeType)];
  // Check all entries but last one for match, return if found
  for (size_t i = 0; i < sizeof(mimeTable) / sizeof(mimeTable[0]) - 1; i++) {
    strcpy_P(buff, mimeTable[i].endsWith);
    if (path.endsWith(buff)) {
      strcpy_P(buff, mimeTable[i].mimeType);
      return String(buff);
    }
  }
  // Fall-through and just return default type
  strcpy_P(buff, mimeTable[sizeof(mimeTable) / sizeof(mimeTable[0]) - 1].mimeType);
  return String(buff);
}


// calculate an ETag for a file in filesystem based on md5 checksum
// that can be used in the http headers - include quotes.
String GzStaticRequestHandler::calcETag(FS &fs, const String &path) {
  String result;

  // calculate eTag using md5 checksum
  uint8_t md5_buf[16];
  File f = fs.open(path, "r");
  MD5Builder calcMD5;
  calcMD5.begin();
  calcMD5.addStream(f, f.size());
  calcMD5.calculate();
  calcMD5.getBytes(md5_buf);
  f.close();
  // create a minimal-length eTag using base64 byte[]->text encoding.
  result = "\"" + base64::encode(md5_buf, 16) + "\"";
  return (result);
}  // calcETag


GzStaticRequestHandler &GzStaticRequestHandler::setFilter(WebServer::FilterFunction filter) {
  _filter = filter;
  return *this;
}
