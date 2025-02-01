
File flz;
File fin;
File fout;

void printMem()
{
  static size_t lastFreeHEap = 0;
  #if defined ESP32 || defined ESP8266
    size_t freeHeap = ESP.getFreeHeap();
  #elif defined ARDUINO_ARCH_RP2040
    size_t freeHeap = rp2040.getFreeHeap();
  #endif

  int diff = freeHeap - lastFreeHEap;

  Serial.printf("Free heap: %d (%s%d bytes since last measure)\n", freeHeap, diff>0?"+":"", diff );
  lastFreeHEap = freeHeap;
}


// keep it static for the lambda function
struct verify_progress_t
{
  void verified_progress(size_t last_added_bytes) {
    if( verified == 0 ) {
      Serial.print("Verifying 0% ");
    } else if( verified+last_added_bytes == src.size() ) {
      verified += last_added_bytes;
      Serial.println(" 100%");
      return;
    }
    verified += last_added_bytes;
    size_t pg = 100.0*(float(verified)/float(src.size()));
    if( pg != lastprogress ) {
      //Serial.printf("Verified %3d%s\n", pg, "%");
      Serial.print(".");
      lastprogress = pg;
    }
  }
  File src;
  size_t verified = 0;
  size_t lastprogress = -1;
} vprogress;



void verify(const char* fzFileName, const char* srcFilename)
{
  // open the fz file for reading
  flz = tarGzFS.open(fzFileName, "r");
  if(!flz)
  {
    Serial.println("[verify] Unable to open lz file, halting");
    while(1) yield();
  }

  // reset values because it's a static struct
  vprogress.verified = 0;
  vprogress.lastprogress = -1;
  vprogress.src = tarGzFS.open(srcFilename, "r");

  if(!vprogress.src || vprogress.src.size()==0)
  {
    Serial.printf("[verify] Unable to read input file %s, halting\n", srcFilename);
    while(1) yield();
  }

  // create GzUnpacker instance
  GzUnpacker *GZUnpacker = new GzUnpacker();

  // attach callback to verify the uncompressed data
  GZUnpacker->setStreamWriter(
    [](unsigned char* buff, size_t buffsize)->bool
    {
      for(size_t i=0;i<buffsize;i++) {
        uint8_t byte = vprogress.src.read();
        if( byte != buff[i] ) {
          Serial.printf("\n[VERIFY ERROR] Source and destination differ at offset %d (expected 0x%02x, got 0x%02x), halting\n", vprogress.src.position(), byte, buff[i] );
          while(1) yield();
        }
      }
      //Serial.write(buff, buffsize);
      vprogress.verified_progress(buffsize);
      return true;
    }
  );

  // inflate+verify!
  if( !GZUnpacker->gzStreamExpander(&flz, flz.size()) || vprogress.verified != vprogress.src.size() || vprogress.src.position() != vprogress.src.size()  ) {
    const char* diffStr = vprogress.src.size()>vprogress.verified?"bigger":"smaller";
    int diffSize = vprogress.src.size()-vprogress.verified;
    Serial.printf("[VERIFY ERROR] Source (%d bytes) is %s (offset=%d, readpos=%d) than Deflated (%d bytes).\n",
      vprogress.src.size(),
      diffStr, diffSize,
      vprogress.src.position(),
      vprogress.verified
    );
    if(diffSize>0) {
      Serial.println("Remaining bytes:");
      for(int i=0;i<diffSize;i++)
          Serial.write(vprogress.src.read());
      Serial.println();
    }
  }

  flz.close();
  vprogress.src.close();
  delete GZUnpacker;
}


void loadFileToBuffer( const char* fileName, unsigned char** bufPtr, size_t* bufLen )
{
  fin = tarGzFS.open(fileName, "r");
  if(!fin)
  {
    Serial.println("[loadFileToBuffer] Unable to read input file, halting");
    while(1) yield();
  }
  *bufLen = fin.size();
  *bufPtr = (unsigned char *)malloc(*bufLen+1);
  if( *bufPtr == NULL )
  {
    Serial.printf("[loadFileToBuffer] Failed to malloc() %d bytes, halting.\nHint: use a smaller file.\n", *bufLen);
    while(1) yield();
  }
  size_t bytesRead = fin.readBytes( (char*)*bufPtr, *bufLen);
  Serial.printf("[loadFileToBuffer] Loaded %d bytes from file into ram\n", bytesRead);
  fin.close();
}


void saveBufferToFile( const uint8_t* buf, size_t bufSize, const char* dstFileName )
{
  assert(buf);
  assert(bufSize>0);
  fout = tarGzFS.open(dstFileName, "w");
  if(!fout) {
    Serial.printf("[saveBufferToFile] Unable to open %s for writing, aborting\n", dstFileName);
    return;
  }
  size_t written_bytes = fout.write(buf, bufSize);
  Serial.printf("[saveBufferToFile] Wrote %d bytes to %s\n", written_bytes, dstFileName);
  fout.close();
}
