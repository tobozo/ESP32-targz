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
  Serial.println("LittleFS started");


  {
    const char *txtFileName = "/jargon.txt";
    const char *fzFileName = "/out.fz";

    unsigned int srcBufLen;
    unsigned char *srcBuf;


    {
      // load the uncompressed text file into memory
      File fin = LittleFS.open(txtFileName);
      if(!fin)
      {
        Serial.println("Unable to read input file, halting");
        while(1);
      }
      srcBufLen = fin.size();
      srcBuf = (unsigned char *)malloc(srcBufLen+1);
      if( srcBuf == NULL )
      {
        Serial.printf("Failed to malloc() %d bytes, halting.\nHint: use a smaller file.\n", srcBufLen);
        while(1);
      }
      size_t bytesRead = fin.readBytes( (char*)srcBuf, srcBufLen);
      printf("Loaded %d bytes from file into ram\n", bytesRead);
      fin.close();
    }


    {
      if( LittleFS.exists(fzFileName))
        LittleFS.remove(fzFileName); // delete artifacts from previous test

      // open the fz file for writing
      File fout = LittleFS.open(fzFileName, "w");
      if(!fout)
      {
        Serial.println("Unable to create output file, halting");
        while(1);
      }

      // deflate!
      size_t outputSize = LZPacker::compress( srcBuf, srcBufLen, &fout );
      Serial.printf("Compressed %d input bytes to %u raw bytes, file size is %d bytes\n", srcBufLen, outputSize, fout.position() );
      free(srcBuf); // free the buffer
      fout.close();
    }


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

  }


}

void loop()
{

}
