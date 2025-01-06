// Set **destination** filesystem by uncommenting one of these:
//#define DEST_FS_USES_SPIFFS
#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only
#include <ESP32-targz.h>


#include "./test_utils.h"


// const char *inputFilename = "/ESP32-targz.bmp"; // 450 KB of binary data (messes up the progress meter)
// const char *inputFilename = "/tiny.json"; // 32 bytes of JSON (tiny file, deflated output should be bigger than the original)
const char *inputFilename = "/big.json"; // 52 KB of non-minified JSON (output should be much smaller)
const char *fzFileName = "/out.gz";



void testStreamToStream()
{
  Serial.println();
  Serial.println("### Stream to Stream ###");

  // open the uncompressed text file for streaming
  File src = LittleFS.open(inputFilename);
  if(!src)
  {
    Serial.println("[testStreamToStream] Unable to read input file, halting");
    while(1);
  }

  // open the fz file for writing
  File dst = LittleFS.open(fzFileName, "w");
  if(!dst)
  {
    Serial.println("[testStreamToStream] Unable to create output file, halting");
    while(1);
  }

  LZPacker::setProgressCallBack( LZ77::defaultProgressCallback );
  size_t dstLen = LZPacker::compress( &src, src.size(), &dst );
  size_t srcLen = src.size();

  float done = float(dstLen)/float(srcLen);
  float left = -(1.0f - done);

  Serial.printf("[testStreamToStream] Deflated %d bytes to %d bytes (%s%.1f%s)\n", srcLen, dstLen, left>0?"+":"", left*100.0, "%" );

  src.close();
  dst.close();

  verify(fzFileName, inputFilename);
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

  LZPacker::setProgressCallBack( LZ77::defaultProgressCallback );
  // perform buffer to buffer decompression (will be saved as file for verification)
  size_t dstBufLen = LZPacker::compress( srcBuf, srcBufLen, &dstBuf );

  if( dstBufLen==0 ) {
    Serial.printf("[testBufferToBuffer] Failed to compress %d bytes, halting\n", srcBufLen);
    while(1);
  }

  float done = float(dstBufLen)/float(srcBufLen);
  float left = -(1.0f - done);

  Serial.printf("[testBufferToBuffer] Deflated %d bytes to %d bytes (%s%.1f%s)\n", srcBufLen, dstBufLen, left>0?"+":"", left*100.0, "%" );

  saveBufferToFile( dstBuf, dstBufLen, fzFileName ); // save to file for verification

  free(srcBuf); // free the input buffer
  free(dstBuf); //free the output buffer

  verify(fzFileName, inputFilename);
}



void testStreamToBuffer()
{
  Serial.println();
  Serial.println("### Stream to Buffer ###");

  // open the uncompressed text file for streaming
  File src = LittleFS.open(inputFilename);
  if(!src)
  {
    Serial.println("[testStreamToBuffer] Unable to read input file, halting");
    while(1);
  }

  size_t srcLen = src.size();

  uint8_t* dstBuf;

  size_t dstBufLen = LZPacker::compress( &src, srcLen, &dstBuf );

  if( dstBufLen==0 ) {
    Serial.printf("[testStreamToBuffer] Failed to compress %d bytes, halting\n", srcLen);
    while(1);
  }

  float done = float(dstBufLen)/float(srcLen);
  float left = -(1.0f - done);

  Serial.printf("[testStreamToBuffer] Deflated %d bytes to %d bytes (%s%.1f%s)\n", srcLen, dstBufLen, left>0?"+":"", left*100.0, "%" );

  saveBufferToFile( dstBuf, dstBufLen, fzFileName ); // save to file for verification

  free(dstBuf); //free the output buffer

  verify(fzFileName, inputFilename);
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
    while(1);
  }

  // create writable file stream
  File src = LittleFS.open(fzFileName, "w");
  if(!src)
  {
    Serial.println("[testBufferToStream] Unable to create output file, halting");
    while(1);
  }

  // deflate!
  LZPacker::setProgressCallBack( LZ77::defaultProgressCallback );
  size_t dstLen = LZPacker::compress( srcBuf, srcBufLen, &src );

  float done = float(dstLen)/float(srcBufLen);
  float left = -(1.0f - done);

  Serial.printf("[testBufferToStream] Deflated %d bytes to %d bytes (%s%.1f%s)\n", srcBufLen, dstLen, left>0?"+":"", left*100.0, "%" );
  src.close();

  free(srcBuf); // free the input buffer

  verify(fzFileName, inputFilename);
}


void setup()
{
  Serial.begin(115200);

  if(!LittleFS.begin())
  {
      Serial.println("LittleFS Failed, halting");
      while(1);
  }
  Serial.println("ESP32-targz: LZ77 compression example");

  testBufferToBuffer();
  testBufferToStream();
  testStreamToBuffer();
  testStreamToStream();

  Serial.println();
  Serial.println("All tests completed");
}

void loop()
{

}
