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

    auto gzStaticHandler = new GzStaticRequestHandler(tarGzFS, "/", "/", nullptr);
    server.addStaticHandler(gzStaticHandler);

    // optional: attach a cached compressor
    server.createGz = compressFile;

    // optional: attach as streamed compressor
    server.streamGz = [](Stream *input, size_t len, Stream* output)->size_t { return LZPacker::compress(input, len, output); };

    // optional: disable gz cache (also enables compression on the fly if server.streamGZ is set)
    // server.cacheGz = false;

    // optional: magic header to flush+regen gzip cache
    const char *headerkeys[] = {"x-gzip-recompress"};
    size_t headerkeyssize = sizeof(headerkeys) / sizeof(char *);
    server.collectHeaders(headerkeys, headerkeyssize);

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

using namespace mime;

class GzStaticRequestHandler;

static const char AUTHORIZATION_HEADER[] = "Authorization";
static const char ETAG_HEADER[] = "If-None-Match";
static const char ACCEPT_ENCODING_HEADER[] = "Accept-Encoding";

class GzWebServer : public WebServer {
public:
  GzWebServer(IPAddress addr, int port = 80)
  : WebServer(addr, port) {
    collectHeaders(0,0);
  };
  GzWebServer(int port = 80)
  : WebServer(port) {
    collectHeaders(0,0);
  }

  // optional: zlib/deflate "on the fly" compressor, returns gz data size
  size_t (*streamGz)(Stream *input, size_t len, Stream* output) = nullptr;
  // optional: zlib/deflate "cache" compressor creates gz if required, returns gzipped file size
  size_t (*createGz)(fs::FS&fs, const String &inputFilename, bool force) = nullptr;

  // Cache behaviour TL;DR: look for existing gz files when applicable.
  // When optional streamGz()/createGz() are set:
  //   - true  = compress to gz cache if not exists. ** needs createGz() **
  //   - false = compress on-the-fly, do not cache gz. ** needs streamGz() **
  // When optional streamGz()/createGz() are NOT set:
  //   - true  = serve gz if exists on filesystem, else serve uncompressed content
  //   - false = always serve uncompressed content
  bool cacheGz = true;

  // attach our custom request handler to be used with serveStatic()
  void addStaticHandler(GzStaticRequestHandler *handler) {
    _addRequestHandler((RequestHandler*)handler);
  }


  // copy of the original function with "Accept-Encoding" header made mandatory
  void collectHeaders(const char *headerKeys[], const size_t headerKeysCount) {
    _headerKeysCount = headerKeysCount + 3;
    if (_currentHeaders) {
      delete[] _currentHeaders;
    }
    _currentHeaders = new RequestArgument[_headerKeysCount];
    _currentHeaders[0].key = FPSTR(AUTHORIZATION_HEADER);
    _currentHeaders[1].key = FPSTR(ETAG_HEADER);
    _currentHeaders[2].key = FPSTR(ACCEPT_ENCODING_HEADER);
    for (int i = 3; i < _headerKeysCount; i++) {
      _currentHeaders[i].key = headerKeys[i - 3]; // TODO: check for duplicates, adjust _headerKeysCount if necessary
    }
  }


  bool clientAcceptsGz() {
    if( hasHeader(FPSTR(ACCEPT_ENCODING_HEADER)) ) {
      String acceptEncoding = header(FPSTR(ACCEPT_ENCODING_HEADER));
      if( acceptEncoding.indexOf("gzip") > 0 || acceptEncoding.indexOf("deflate") > 0 ) {
        // log_v("client can negociate gzip/deflate");
        return true;
      }
    }
    return false;
  }


  template<typename T> size_t gzStream(fs::FS &fs, T &srcFile, const String &contentType, const int code = 200) {

    String HTTPResponse;
    // NOTE: this 'magic' header must be added manually using collectHeaders()
    bool overwrite = hasHeader(F("x-gzip-recompress"));
    size_t compressedSized = 0;
    String path = String(srcFile.path());
    String pathWithGz;

    size_t len = srcFile.size();

    if( len<512 || path.endsWith(FPSTR(mimeTable[gz].endsWith)) ) {
      log_d("file %s (%d bytes) does not need to be compressed", path.c_str(), len);
      goto _stream_uncompressed;
    }

    if( !clientAcceptsGz() ) {
      log_w("Client didn't negociate gzip/deflate encoded content");
      goto _stream_uncompressed;
    }

    if( cacheGz ) {
      // log_w("server can cache gz");
      if( createGz ) { // callback is attached, server can create gz
        compressedSized = createGz(fs, path, overwrite);
        if( compressedSized == 0 ) {
          log_e("gz compression failed for %s", path);
          goto _stream_uncompressed;
        }
      }
      pathWithGz = path + FPSTR(mimeTable[gz].endsWith);
      if( fs.exists(pathWithGz) ) {
        log_d("gz cache found");
        srcFile.close();
        srcFile = fs.open(pathWithGz);
        _contentLength = srcFile.size();
        goto _stream_uncompressed;
      }
    }

    if( !streamGz ) {
      log_d("no gz cache found and server can't compress on the fly");
      goto _stream_uncompressed;
    }

    log_d("server will compress on the fly");

    // compress on the fly, do not sent "Content-Length" header
    sendHeader(String(F("Content-Type")), contentType, true); // TODO: verify content-type for emptiness (should default to text/html)
    sendHeader(F("Content-Encoding"), F("gzip")); // TODO: allow single/combined values of {gzip,compress,deflate,br,zstd}
    sendHeader(String(F("Connection")), String(F("close")));

    // BUG: WebServer library (3.1.0) sends "Content-Length: 0"
    // But we don't want to send that header since the size isn't known yet.
    // So we build our own HTTP response
    HTTPResponse = String(F("HTTP/1.")) + String(_currentVersion) + ' ';
    HTTPResponse += String(code);
    HTTPResponse += ' ';
    HTTPResponse += _responseCodeToString(code);
    HTTPResponse += "\r\n";
    HTTPResponse += _responseHeaders;
    HTTPResponse += "\r\n";
    _responseHeaders = "";
    _currentClientWrite(HTTPResponse.c_str(), HTTPResponse.length());
    return streamGz(&srcFile, len, &_currentClient);

    _stream_uncompressed:
    return streamFile(srcFile, contentType, code); // no need to compress small files
  }

};


class GzStaticRequestHandler : public RequestHandler {
public:
  GzStaticRequestHandler(FS &fs, const char *path, const char *uri, const char *cache_header) : _fs(fs), _uri(uri), _path(path), _cache_header(cache_header) {
    File f = fs.open(path);
    _isFile = (f && (!f.isDirectory()));
    log_w(
      "GzStaticRequestHandler: path=%s uri=%s isFile=%d, cache_header=%s\r\n", path, uri, _isFile, cache_header ? cache_header : ""
    );  // issue 5506 - cache_header can be nullptr
    _baseUriLength = _uri.length();
  }

  bool canHandle(HTTPMethod requestMethod, const String &requestUri) override {
    if (requestMethod != HTTP_GET) {
      return false;
    }

    if ((_isFile && requestUri != _uri) || !requestUri.startsWith(_uri)) {
      return false;
    }

    return true;
  }

  bool canHandle(WebServer &server, HTTPMethod requestMethod, const String &requestUri) override {
    if (requestMethod != HTTP_GET) {
      return false;
    }

    if ((_isFile && requestUri != _uri) || !requestUri.startsWith(_uri)) {
      return false;
    }

    if (_filter != NULL ? _filter(server) == false : false) {
      return false;
    }

    return true;
  }

  bool handle(WebServer &_server, HTTPMethod requestMethod, const String &requestUri) override {
    if (!canHandle(_server, requestMethod, requestUri)) {
      return false;
    }

    auto &server = (GzWebServer&)_server;

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
    if ( server.cacheGz && server.clientAcceptsGz() && !path.endsWith(FPSTR(mimeTable[gz].endsWith)) ) {
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

    server.gzStream(_fs, f, contentType);
    f.close();
    return true;
  }

  static String getContentType(const String &path) {
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
  static String calcETag(FS &fs, const String &path) {
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

  GzStaticRequestHandler &setFilter(WebServer::FilterFunction filter) {
    _filter = filter;
    return *this;
  }

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
