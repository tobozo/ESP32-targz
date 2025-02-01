// Set **destination** filesystem by uncommenting one of these:
//#define DEST_FS_USES_SPIFFS
#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only
#include <ESP32-targz.h>

#include "./test_utils.h"


// const char *inputFilename = "/ESP32-targz.bmp"; // 450 KB of binary data (some tests will fail if free heap is below that)
// const char *inputFilename = "/tiny.json"; // 32 bytes of JSON (tiny file, deflated output should be bigger than the original)
const char *inputFilename = "/big.json"; // 52 KB of non-minified JSON (deflated output should be 50~80% smaller)
const char *fzFileName = "/out.gz";

File src;
File dst;

void testStreamToStream()
{
  Serial.println();
  Serial.println("### Stream to Stream ###");

  // open the uncompressed text file for streaming
  src = tarGzFS.open(inputFilename, "r");
  if(!src)
  {
    Serial.println("[testStreamToStream] Unable to read input file, halting");
    while(1) yield();
  }

  // open the fz file for writing
  dst = tarGzFS.open(fzFileName, "w");
  if(!dst)
  {
    Serial.println("[testStreamToStream] Unable to create output file, halting");
    while(1) yield();
  }

  LZPacker::setProgressCallBack( LZPacker::defaultProgressCallback );
  size_t dstLen = LZPacker::compress( &src, src.size(), &dst );
  size_t srcLen = src.size();

  float done = float(dstLen)/float(srcLen);
  float left = -(1.0f - done);

  Serial.printf("[testStreamToStream] Deflated %d bytes to %d bytes (%s%.1f%s)\n", srcLen, dstLen, left>0?"+":"", left*100.0, "%" );

  src.close();
  dst.close();

  verify(fzFileName, inputFilename);

  tarGzFS.remove(fzFileName);

}



void testBufferToBuffer()
{
  Serial.println();
  Serial.println("### Buffer to Buffer ###");


  size_t srcBufLen;
  uint8_t* srcBuf = NULL;
  uint8_t* dstBuf = NULL;

  // load the uncompressed text file into memory
  loadFileToBuffer( inputFilename, &srcBuf, &srcBufLen );

  LZPacker::setProgressCallBack( LZPacker::defaultProgressCallback );
  // perform buffer to buffer decompression (will be saved as file for verification)
  size_t dstBufLen = LZPacker::compress( srcBuf, srcBufLen, &dstBuf );

  if( dstBufLen==0 ) {
    Serial.printf("[testBufferToBuffer] Failed to compress %d bytes, halting\n", srcBufLen);
    while(1) yield();
  }

  float done = float(dstBufLen)/float(srcBufLen);
  float left = -(1.0f - done);

  Serial.printf("[testBufferToBuffer] Deflated %d bytes to %d bytes (%s%.1f%s)\n", srcBufLen, dstBufLen, left>0?"+":"", left*100.0, "%" );

  saveBufferToFile( dstBuf, dstBufLen, fzFileName ); // save to file for verification

  free(srcBuf); // free the input buffer
  free(dstBuf); //free the output buffer

  verify(fzFileName, inputFilename);

  tarGzFS.remove(fzFileName);

}



void testStreamToBuffer()
{
  Serial.println();
  Serial.println("### Stream to Buffer ###");


  // open the uncompressed text file for streaming
  src = tarGzFS.open(inputFilename, "r");
  if(!src)
  {
    Serial.println("[testStreamToBuffer] Unable to read input file, halting");
    while(1) yield();
  }

  size_t srcLen = src.size();

  uint8_t* dstBuf;

  size_t dstBufLen = LZPacker::compress( &src, srcLen, &dstBuf );

  if( dstBufLen==0 ) {
    Serial.printf("[testStreamToBuffer] Failed to compress %d bytes, halting\n", srcLen);
    while(1) yield();
  }

  float done = float(dstBufLen)/float(srcLen);
  float left = -(1.0f - done);

  Serial.printf("[testStreamToBuffer] Deflated %d bytes to %d bytes (%s%.1f%s)\n", srcLen, dstBufLen, left>0?"+":"", left*100.0, "%" );

  saveBufferToFile( dstBuf, dstBufLen, fzFileName ); // save to file for verification

  free(dstBuf); //free the output buffer

  src.close();

  verify(fzFileName, inputFilename);

  tarGzFS.remove(fzFileName);

}



void testBufferToStream()
{
  Serial.println();
  Serial.println("### Buffer to Stream ###");

  size_t srcBufLen;
  uint8_t* srcBuf = NULL;

  // load the uncompressed text file into memory
  loadFileToBuffer( inputFilename, &srcBuf, &srcBufLen );
  if(srcBufLen==0 || srcBuf==NULL)
  {
    Serial.printf("[testBufferToStream] Source buffer is empty, halkting");
    while(1) yield();
  }

  // create writable file stream
  src = tarGzFS.open(fzFileName, "w");
  if(!src)
  {
    Serial.println("[testBufferToStream] Unable to create output file, halting");
    while(1) yield();
  }

  // deflate!
  LZPacker::setProgressCallBack( LZPacker::defaultProgressCallback );
  size_t dstLen = LZPacker::compress( srcBuf, srcBufLen, &src );

  float done = float(dstLen)/float(srcBufLen);
  float left = -(1.0f - done);

  Serial.printf("[testBufferToStream] Deflated %d bytes to %d bytes (%s%.1f%s)\n", srcBufLen, dstLen, left>0?"+":"", left*100.0, "%" );
  src.close();

  free(srcBuf); // free the input buffer
  srcBuf = NULL;

  verify(fzFileName, inputFilename);

  tarGzFS.remove(fzFileName);

}


void setup()
{
  Serial.begin(115200);
  delay(5000);

  if(!tarGzFS.begin())
  {
      Serial.println("tarGzFS Failed, halting");
      while(1) yield();
  }

  Serial.println("ESP32-targz: LZ77 compression example");
  printMem();

  const bool IF_TEST_ENDLESSLY = false; // set to true to test memory leaks

  do
  {
    testStreamToStream(); // tested OK on ESP32/RP2040/ESP8266 (any file size)
    printMem();
    // testBufferToBuffer(); // tested OK on ESP32/RP2040/ESP8266 (small file size)
    // printMem();
    // testBufferToStream(); // tested OK on ESP32/RP2040/ESP8266 (small file size)
    // printMem();
    // testStreamToBuffer(); // tested OK on ESP32/RP2040/ESP8266 (small file size)
    // printMem();

    Serial.println();
    Serial.println("All tests completed");
    Serial.println();

  } while( IF_TEST_ENDLESSLY );
}

void loop()
{
}
