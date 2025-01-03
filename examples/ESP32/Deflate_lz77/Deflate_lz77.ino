#ifndef ESP32
  #error "this sketch is only available on ESP32 architecture"
#endif

// Set **destination** filesystem by uncommenting one of these:
//#define DEST_FS_USES_SPIFFS
#define DEST_FS_USES_LITTLEFS
//#define DEST_FS_USES_SD
//#define DEST_FS_USES_FFAT   // ESP32 only
//#define DEST_FS_USES_SD_MMC // ESP32 only
#include <ESP32-targz.h>


void unpackFile(const char* fzFileName)
{
  // now open the fz file for reading
  File flz = LittleFS.open(fzFileName);
  if(!flz)
  {
    Serial.println("Unable to open lz file, halting");
    while(1);
  }

  // create GzUnpacker instance
  GzUnpacker *GZUnpacker = new GzUnpacker();
  // attach callback to write the uncompressed data
  GZUnpacker->setStreamWriter(
    [](unsigned char* buff, size_t buffsize)->bool
    {
      Serial.write(buff, buffsize); return true;
    }
  );
  // inflate!
  GZUnpacker->gzStreamExpander(&flz, flz.size());
}


void loadFileToBuffer( const char* fileName, unsigned char** bufPtr, size_t* bufLen )
{
  File fin = LittleFS.open(fileName);
  if(!fin)
  {
    Serial.println("Unable to read input file, halting");
    while(1);
  }
  *bufLen = fin.size();
  *bufPtr = (unsigned char *)malloc(*bufLen+1);
  if( *bufPtr == NULL )
  {
    Serial.printf("Failed to malloc() %d bytes, halting.\nHint: use a smaller file.\n", *bufLen);
    while(1);
  }
  size_t bytesRead = fin.readBytes( (char*)*bufPtr, *bufLen);
  printf("Loaded %d bytes from file into ram\n", bytesRead);
  fin.close();
}


void saveBufferToFile( const uint8_t* buf, size_t bufSize, const char* dstFileName )
{
  assert(buf);
  assert(bufSize>0);
  File out = LittleFS.open(dstFileName, "w");
  if(!out) {
    Serial.printf("Unable to open %s for writing, aborting\n", dstFileName);
    return;
  }
  size_t written_bytes = out.write(buf, bufSize);
  Serial.printf("Wrote %d bytes to %s\n", written_bytes, dstFileName);
}


void testBufferToBuffer()
{
  const char *txtFileName = "/jargon.txt";
  const char *fzFileName = "/out.fz";

  size_t srcBufLen;
  uint8_t* srcBuf = NULL;
  uint8_t* dstBuf = NULL;

  // load the uncompressed text file into memory
  loadFileToBuffer( txtFileName, &srcBuf, &srcBufLen );

  LZPacker::setProgressCallBack( lzProgressCallback );
  // perform buffer to buffer decompression (will be saved as file for verification)
  size_t dstBufLen = LZPacker::compress( srcBuf, srcBufLen, &dstBuf );
  if( dstBufLen==0 ) {
    Serial.printf("Failed to compress %d bytes, halting\n", srcBufLen);
    while(1);
  }
  if( LittleFS.exists(fzFileName))
    LittleFS.remove(fzFileName); // delete artifacts from previous test

  saveBufferToFile( dstBuf, dstBufLen, fzFileName ); // save to file for verification

  free(srcBuf); // free the input buffer
  free(dstBuf); //free the output buffer

  unpackFile(fzFileName); // verify
}



void testBufferToStream()
{
  const char *txtFileName = "/jargon.txt";
  const char *fzFileName = "/out.fz";

  size_t srcBufLen;
  uint8_t* srcBuf = NULL;

  // load the uncompressed text file into memory
  loadFileToBuffer( txtFileName, &srcBuf, &srcBufLen );
  Serial.printf("srcBufLen = %d\n", srcBufLen);
  if(srcBuf==NULL)
  {
    Serial.printf("Buffer is empty ?");
    while(1);
  }

  if( LittleFS.exists(fzFileName))
    LittleFS.remove(fzFileName); // delete artifacts from previous test

  // create writable file stream
  File stream = LittleFS.open(fzFileName, "w");
  if(!stream)
  {
    Serial.println("Unable to create output file, halting");
    while(1);
  }

  // deflate!
  LZPacker::setProgressCallBack( lzProgressCallback );
  size_t outputSize = LZPacker::compress( srcBuf, srcBufLen, &stream );
  Serial.printf("Compressed %d input bytes to %u total bytes, file size is %d bytes\n", srcBufLen, outputSize, stream.position() );
  stream.close();

  free(srcBuf); // free the input buffer
}


void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("Hello world");

  if(!LittleFS.begin())
  {
      Serial.println("LittleFS Failed, halting");
      while(1);
  }
  Serial.println("LittleFS started, now testing LZ77 compression");

  testBufferToBuffer(); // tested OK
  // testBufferToStream(); // tested OK

}

void loop()
{

}
