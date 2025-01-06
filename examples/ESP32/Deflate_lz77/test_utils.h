


void verify(const char* fzFileName, const char* srcFilename)
{
  // now open the fz file for reading
  File flz = LittleFS.open(fzFileName);
  if(!flz)
  {
    Serial.println("[verify] Unable to open lz file, halting");
    while(1);
  }

  // keep it static for the lambda function
  static struct verify_progress_t
  {
    void verified_progress(size_t last_added_bytes) {
      if( verified == 0 ) {
        Serial.print("Verifying 0% ");
      } else if( verified+last_added_bytes == src.size() ) {
        Serial.println(" 100%");
        return;
      }
      static size_t lastprogress = -1;
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
  } pg;

  // reset values because it's a static struct
  pg.verified = 0;
  pg.src = LittleFS.open(srcFilename);

  if(!pg.src)
  {
    Serial.printf("[verify] Unable to read input file %s, halting\n", srcFilename);
    while(1);
  }

  // create GzUnpacker instance
  GzUnpacker *GZUnpacker = new GzUnpacker();

  // attach callback to verify the uncompressed data
  GZUnpacker->setStreamWriter(
    [](unsigned char* buff, size_t buffsize)->bool
    {
      for(int i=0;i<buffsize;i++) {
        uint8_t byte = pg.src.read();
        if( byte != buff[i] ) {
          Serial.printf("\n[VERIFY ERROR] Source and destination differ at offset %d (expected 0x%02x, got 0x%02x), halting\n", pg.src.position(), byte, buff[i] );
          while(1);
        }
      }
      pg.verified_progress(buffsize);
      return true;
    }
  );

  // inflate+verify!
  GZUnpacker->gzStreamExpander(&flz, flz.size());

  flz.close();
  pg.src.close();

}


void loadFileToBuffer( const char* fileName, unsigned char** bufPtr, size_t* bufLen )
{
  File fin = LittleFS.open(fileName);
  if(!fin)
  {
    Serial.println("[loadFileToBuffer] Unable to read input file, halting");
    while(1);
  }
  *bufLen = fin.size();
  *bufPtr = (unsigned char *)malloc(*bufLen+1);
  if( *bufPtr == NULL )
  {
    Serial.printf("[loadFileToBuffer] Failed to malloc() %d bytes, halting.\nHint: use a smaller file.\n", *bufLen);
    while(1);
  }
  size_t bytesRead = fin.readBytes( (char*)*bufPtr, *bufLen);
  Serial.printf("[loadFileToBuffer] Loaded %d bytes from file into ram\n", bytesRead);
  fin.close();
}


void saveBufferToFile( const uint8_t* buf, size_t bufSize, const char* dstFileName )
{
  assert(buf);
  assert(bufSize>0);
  File out = LittleFS.open(dstFileName, "w");
  if(!out) {
    Serial.printf("[saveBufferToFile] Unable to open %s for writing, aborting\n", dstFileName);
    return;
  }
  size_t written_bytes = out.write(buf, bufSize);
  Serial.printf("[saveBufferToFile] Wrote %d bytes to %s\n", written_bytes, dstFileName);
}
