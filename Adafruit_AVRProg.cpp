// Useful message printing definitions
#include "Adafruit_AVRProg.h"

static byte pageBuffer[128];		       /* One page of flash */

Adafruit_AVRProg::Adafruit_AVRProg() {
  progLED = errLED = -1;
  _sck = _miso = _mosi = _reset = -1;
  spi = NULL;
  programmode = false;
  spiBitDelay = 0;
}

void Adafruit_AVRProg::setSPI(int8_t reset_pin, SPIClass *theSPI) {
  _reset = reset_pin;
  spi = theSPI;
}

void Adafruit_AVRProg::setSPI(int8_t reset_pin, int8_t sck_pin, int8_t mosi_pin, int8_t miso_pin) {
  _reset = reset_pin;
  _mosi = mosi_pin;
  _miso = miso_pin;
  _sck = sck_pin;
}

void Adafruit_AVRProg::endProgramMode(void) {
  if (spi) {
    SPI.endTransaction();
    SPI.end();
  } else {
    pinMode(_miso, INPUT);
    pinMode(_mosi, INPUT);
    pinMode(_sck, INPUT);
  }
  digitalWrite(_reset, LOW);
  pinMode(_reset, INPUT);

  programmode = false;
}

bool Adafruit_AVRProg::targetPower(bool poweron) {
  if (poweron) {
    if (progLED > 0) {
      pinMode(progLED, OUTPUT);
      digitalWrite(progLED, HIGH);
    }
    pinMode(_reset, OUTPUT);
    digitalWrite(_reset, LOW);  // reset it right away.
    delay(100);
    Serial.print("Starting Program Mode...");
    if (startProgramMode()) {
      Serial.println(" [OK]");
      return true;
    } else {
      Serial.println(" [FAIL]");
      return false;
    }
  } else {
    endProgramMode();
    if (progLED > 0) {
      digitalWrite(progLED, LOW);
    }
    return true;
  }
}


bool Adafruit_AVRProg::startProgramMode(uint32_t clockspeed) {
  pinMode(_reset, OUTPUT);
  digitalWrite(_reset, LOW);

  if (spi) {
    debug("Using hardware SPI");
    SPI.begin();
    SPI.beginTransaction(SPISettings(clockspeed, MSBFIRST, SPI_MODE0)); 
  } else if (_sck > 0 && _mosi > 0 && _miso > 0) {
    debug("Using software SPI");
    pinMode(_sck, OUTPUT);
    digitalWrite(_sck, LOW);
    delay(50);
    digitalWrite(_reset, LOW);
    delay(50);
    pinMode(_miso, INPUT);
    pinMode(_mosi, OUTPUT);
    float _delay = 1000.0 * 1000.0 / (float)clockspeed;
    //Serial.println(_delay);
    spiBitDelay = _delay;
  } else {
    error(F("Neither hardware or software SPI modes selected"));
  }
  debug("...spi_init done");

  debug("...isp_transaction");
  if (isp_transaction(0xAC, 0x53, 0x00, 0x00) == 0x5300) {
    debug("...Done");
    programmode = true;
    return true;
  }
  return false;
}

/*******************************************************
 * ISP high level commands
 */

/*
 * readSignature
 * read the bottom two signature bytes (if possible) and return them
 * Note that the highest signature byte is the same over all AVRs so we skip it
 */
uint16_t Adafruit_AVRProg::readSignature (void)
{
  startProgramMode(100000); // start at 100KHz speed
    
  uint16_t target_type = 0;
  
  target_type = isp_transaction(0x30, 0x00, 0x01, 0x00);
  target_type <<= 8;
  target_type |= isp_transaction(0x30, 0x00, 0x02, 0x00);
  
  endProgramMode();

  return target_type;
}

// Send the erase command, then busy wait until the chip is erased
void Adafruit_AVRProg::eraseChip(void) {
  startProgramMode(100000); // start at 100KHz speed    
  if (isp_transaction(0xAC, 0x80, 0, 0) != 0x8000) {	// chip erase
    error(F("Error on chip erase command"));
  }
  busyWait();
  endProgramMode();
}


/*
 * programmingFuses
 * Program the fuse/lock bits
 */
bool Adafruit_AVRProg::programFuses (const byte *fuses)
{
  startProgramMode(100000); // start at 100KHz speed
    
  byte f;
  Serial.println(F("\nSetting fuses"));

  f = pgm_read_byte(&fuses[FUSE_PROT]);
  if (f) {
    Serial.print(F("\tSet Lock Fuse to: ")); Serial.println(f, HEX);
    if (isp_transaction(0xAC, 0xE0, 0x00, f) != 0xE000) {
      return false;
    }
  }
  f = pgm_read_byte(&fuses[FUSE_LOW]);
  if (f) {
    Serial.print(F("\tSet Low Fuse to: ")); Serial.println(f, HEX);
    if (isp_transaction(0xAC, 0xA0, 0x00, f) != 0xA000) {
      return false;
    }
  }
  f = pgm_read_byte(&fuses[FUSE_HIGH]);
  if (f) {
    Serial.print(F("\tSet High Fuse to: "));Serial.println(f, HEX);
    if (isp_transaction(0xAC, 0xA8, 0x00, f) != 0xA800) {
      return false;
    }
  }
  f = pgm_read_byte(&fuses[FUSE_EXT]);
  if (f) {
    Serial.print(F("\tSet Ext Fuse to: ")); Serial.println(f, HEX);
    if (isp_transaction(0xAC, 0xA4, 0x00, f) != 0xA400) {
      return false;
    }
  }
  Serial.println();
  endProgramMode();
  return true;
}



/*
 * verifyFuses
 * Verifies a fuse set
 */
boolean Adafruit_AVRProg::verifyFuses (const byte *fuses, const byte *fusemask)
{
  startProgramMode(100000); // start at 100KHz speed

  byte f;
  Serial.println(F("Verifying fuses..."));
  f = pgm_read_byte(&fuses[FUSE_PROT]);
  if (f) {
    uint8_t readfuse = isp_transaction(0x58, 0x00, 0x00, 0x00);  // lock fuse
    readfuse &= pgm_read_byte(&fusemask[FUSE_PROT]);
    Serial.print(F("\tLock Fuse = 0x")); Serial.println(readfuse, HEX);
    if (readfuse != f) 
      return false;
  }
  f = pgm_read_byte(&fuses[FUSE_LOW]);
  if (f) {
    uint8_t readfuse = isp_transaction(0x50, 0x00, 0x00, 0x00);  // low fuse
    Serial.print(F("\tLow Fuse = 0x")); Serial.println(readfuse, HEX);
    readfuse &= pgm_read_byte(&fusemask[FUSE_LOW]);
    if (readfuse != f) 
      return false;
  }
  f = pgm_read_byte(&fuses[FUSE_HIGH]);
  if (f) {
    uint8_t readfuse = isp_transaction(0x58, 0x08, 0x00, 0x00);  // high fuse
    readfuse &= pgm_read_byte(&fusemask[FUSE_HIGH]);
    Serial.print(F("\tHigh Fuse = 0x")); Serial.println(readfuse, HEX);
    if (readfuse != f) 
      return false;
  }
  f = pgm_read_byte(&fuses[FUSE_EXT]);
  if (f) {
    uint8_t readfuse = isp_transaction(0x50, 0x08, 0x00, 0x00);  // ext fuse
    readfuse &= pgm_read_byte(&fusemask[FUSE_EXT]);
    Serial.print(F("\tExt Fuse = 0x")); Serial.println(readfuse, HEX);
    if (readfuse != f) 
      return false;
  }
  Serial.println();
  endProgramMode();
  return true;
}


bool Adafruit_AVRProg::writeImage(const byte *hextext, uint8_t pagesize, uint32_t chipsize) {
  uint16_t pageaddr = 0;

  Serial.print("Chip size: "); Serial.println(chipsize, DEC);
  Serial.print("Page size: "); Serial.println(pagesize, DEC);
  while (pageaddr < chipsize) {
     const byte *hextextpos = readImagePage(hextext, pageaddr, pagesize, pageBuffer);
          
     boolean blankpage = true;
     for (uint8_t i=0; i<pagesize; i++) {
       if (pageBuffer[i] != 0xFF) blankpage = false;
     }          
     if (! blankpage) {
       if (! flashPage(pageBuffer, pageaddr, pagesize))	
	 return false;
     }
     hextext = hextextpos;
     pageaddr += pagesize;
  }
  return true;
}



/*
 * readImagePage
 *
 * Read a page of intel hex image from a string in pgm memory. Returns a pointer to where we ended
*/
const byte *Adafruit_AVRProg::readImagePage (const byte *hextext, uint16_t pageaddr, uint8_t pagesize, byte *page)
{
  uint16_t len;
  uint8_t page_idx = 0;
  const byte *beginning = hextext;
  
  byte b, cksum = 0;

  //Serial.print("page size = "); Serial.println(pagesize, DEC);

  // 'empty' the page by filling it with 0xFF's
  for (uint8_t i=0; i<pagesize; i++)
    page[i] = 0xFF;

  while (1) {
    uint16_t lineaddr;
    char c;

    // read one line!
    c = pgm_read_byte(hextext++);
    if (c == '\n' || c == '\r') {
      continue;
    }
    if (c != ':') {
      Serial.print(c);
      error(F(" No colon?"));
      break;
    }
    // Read the byte count into 'len'
    len = hexToByte(pgm_read_byte(hextext++));
    len = (len<<4) + hexToByte(pgm_read_byte(hextext++));
    cksum = len;
    
    // read high address byte
    b = hexToByte(pgm_read_byte(hextext++));  
    b = (b<<4) + hexToByte(pgm_read_byte(hextext++));
    cksum += b;
    lineaddr = b;
    
    // read low address byte
    b = hexToByte(pgm_read_byte(hextext++)); 
    b = (b<<4) + hexToByte(pgm_read_byte(hextext++));
    cksum += b;
    lineaddr = (lineaddr << 8) + b;
    
    if (lineaddr >= (pageaddr + pagesize)) {
      return beginning;
    }

    b = hexToByte(pgm_read_byte(hextext++)); // record type 
    b = (b<<4) + hexToByte(pgm_read_byte(hextext++));
    cksum += b;
    //Serial.print("Record type "); Serial.println(b, HEX);
    if (b == 0x1) { 
     // end record!
     break;
    } 
#if VERBOSE
    Serial.print(F("\nLine address =  0x")); Serial.print(lineaddr, HEX);      
    Serial.print(F(", Page address =  0x")); Serial.println(pageaddr, HEX);   
    Serial.print(F("HEX data: "));
#endif
    for (byte i=0; i < len; i++) {
      // read 'n' bytes
      b = hexToByte(pgm_read_byte(hextext++));
      b = (b<<4) + hexToByte(pgm_read_byte(hextext++));
      
      cksum += b;
#if VERBOSE
      Serial.print(b, HEX);
      Serial.write(' ');
#endif

      page[page_idx] = b;
      page_idx++;

      if (page_idx > pagesize) {
          error("Too much code!");
	  break;
      }
    }
    b = hexToByte(pgm_read_byte(hextext++));  // chxsum
    b = (b<<4) + hexToByte(pgm_read_byte(hextext++));
    cksum += b;
    if (cksum != 0) {
      error(F("Bad checksum: "));
      Serial.print(cksum, HEX);
    }
    if (pgm_read_byte(hextext++) != '\n') {
      error(F("No end of line"));
      break;
    }
#if VERBOSE
    Serial.println();
    Serial.print(F("Page index: ")); Serial.println(page_idx, DEC);
#endif
    if (page_idx == pagesize) 
      break;
  }
#if VERBOSE
  Serial.print(F("\n  Total bytes read: "));
  Serial.println(page_idx, DEC);
#endif
  return hextext;
}

// verifyImage does a byte-by-byte verify of the flash hex against the chip
// Thankfully this does not have to be done by pages!
// returns true if the image is the same as the hextext, returns false on any error
boolean Adafruit_AVRProg::verifyImage(const byte *hextext)  {
  startProgramMode(1000000); // start at 1MHz speed

  uint16_t len;
  byte b, cksum = 0;

  while (1) {
    uint16_t lineaddr;
    
    // read one line!
    char c = pgm_read_byte(hextext++);
    if (c == '\n' || c == '\r') {
      continue;
    }
    if (c != ':') {
      Serial.print(c);
      error(F(" No colon?"));
      break;
    }
    len = hexToByte(pgm_read_byte(hextext++));
    len = (len<<4) + hexToByte(pgm_read_byte(hextext++));
    cksum = len;

    b = hexToByte(pgm_read_byte(hextext++)); // record type 
    b = (b<<4) + hexToByte(pgm_read_byte(hextext++));
    cksum += b;
    lineaddr = b;
    b = hexToByte(pgm_read_byte(hextext++)); // record type
    b = (b<<4) + hexToByte(pgm_read_byte(hextext++));
    cksum += b;
    lineaddr = (lineaddr << 8) + b;
    
    b = hexToByte(pgm_read_byte(hextext++)); // record type 
    b = (b<<4) + hexToByte(pgm_read_byte(hextext++));
    cksum += b;

    //Serial.print("Record type "); Serial.println(b, HEX);
    if (b == 0x1) { 
     // end record!
     break;
    } 
    
    for (byte i=0; i < len; i++) {
      // read 'n' bytes
      b = hexToByte(pgm_read_byte(hextext++));
      b = (b<<4) + hexToByte(pgm_read_byte(hextext++));
      cksum += b;
      
#if VERBOSE
      Serial.print("$"); Serial.print(lineaddr, HEX);
      Serial.print(":0x"); Serial.print(b, HEX);
      Serial.write(" ? ");
#endif

      // verify this byte!
      byte reply;
      if (lineaddr % 2) { // for 'high' bytes
	reply = isp_transaction(0x28, lineaddr >> 9, lineaddr / 2, 0) & 0xFF;
      } else {            // for 'low' bytes
	reply = isp_transaction(0x20, lineaddr >> 9, lineaddr / 2, 0) & 0xFF;
      }
      if (b != reply) {
	Serial.print(F("Verification error at address 0x")); Serial.print(lineaddr, HEX);
	Serial.print(F(" Should be 0x")); Serial.print(b, HEX); Serial.print(F(" not 0x"));
	Serial.println(reply, HEX);
	return false;
      }
      lineaddr++;  
    }
    
    b = hexToByte(pgm_read_byte(hextext++));  // chxsum
    b = (b<<4) + hexToByte(pgm_read_byte(hextext++));
    cksum += b;
    if (cksum != 0) {
      Serial.print(cksum, HEX);
      error(F(" - bad checksum"));
    }
    if (pgm_read_byte(hextext++) != '\n') {
      error("No end of line");
    }
  }

  endProgramMode();

  return true;
}




/*******************************************************
 * Functions specific to ISP programming of an AVR
 */


// Send one byte to the page buffer on the chip
bool Adafruit_AVRProg::flashWord (uint8_t hilo, uint16_t addr, uint8_t data) {
#if VERBOSE
  Serial.print(data, HEX);
#endif
  if (isp_transaction(0x40+8*hilo, addr>>8 & 0xFF, addr & 0xFF, data) != addr) {
    return false;
  }
#if VERBOSE
  Serial.print(" ");
#endif
  return true;
}

// Basically, write the pagebuff (with pagesize bytes in it) into page $pageaddr
bool Adafruit_AVRProg::flashPage (byte *pagebuff, uint16_t pageaddr, uint8_t pagesize) {  
  Serial.print(F("Flashing page ")); Serial.println(pageaddr, HEX);

  startProgramMode(1000000); // start at 1MHz speed

  for (uint16_t i=0; i < pagesize/2; i++) {  
#if VERBOSE
    Serial.print(pagebuff[2*i], HEX); Serial.print(' ');
    Serial.print(pagebuff[2*i+1], HEX); Serial.print(' ');
    if ( i % 16 == 15) Serial.println();
#endif

    if (! flashWord(LOW, i, pagebuff[2*i])) return false;
    if (! flashWord(HIGH, i, pagebuff[2*i+1])) return false;
  }
  
  // page addr is in bytes, but we need to convert to words (/2)
  //Serial.print("page addr "); Serial.println(pageaddr);
  pageaddr /= 2;

  uint16_t commitreply = isp_transaction(0x4C, (pageaddr >> 8) & 0xFF, pageaddr & 0xFF, 0);
  
  Serial.print("  Commit Page: 0x");  Serial.print(pageaddr, HEX);
  Serial.print(" -> 0x"); Serial.println(commitreply, HEX);
  if (commitreply != pageaddr) 
    return false;

  busyWait();
  
  endProgramMode();

  return true;
}




// Simply polls the chip until it is not busy any more - for erasing and programming
void Adafruit_AVRProg::busyWait(void)  {
  byte busybit;
  do {
    busybit = isp_transaction(0xF0, 0x0, 0x0, 0x0);
    //Serial.print(busybit, HEX);
  } while (busybit & 0x01);
}


void Adafruit_AVRProg::generateClock() {
#ifdef __AVR__
  Serial.println(F("Setting up 8MHz clock on pin 9"));
  pinMode(9, OUTPUT);
  // setup high freq PWM on pin 9 (timer 1)
  OCR1A = 0;
  ICR1 = 1;  // 50% duty cycle -> 8 MHz
  // OC1A output, fast PWM
  TCCR1A = _BV(WGM11) | _BV(COM1A1);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10); // no clock prescale
#else
  error(F("Clock generation only supported on AVRs"));
#endif
}


uint16_t Adafruit_AVRProg::isp_transaction(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  uint8_t n, m;
  transfer(a); 
  n = transfer(b);
  //if (n != a) error = -1;
  m = transfer(c);
  return 0xFFFFFF & ((n<<16)+(m<<8) + transfer(d));
}

uint8_t Adafruit_AVRProg::transfer(uint8_t out) {
  if (spi) {
    return spi->transfer(out);
  } else if (_sck > 0 && _mosi > 0 && _miso > 0) {
    uint8_t in = 0;
    
    //Serial.print(" -> 0x"); Serial.print(out, HEX);
    for (int16_t i=0x80; i>0; i>>=1) {
      digitalWrite(_sck, LOW);
      delayMicroseconds(spiBitDelay);
      //Serial.print("out: "); Serial.println(out, HEX);
      //Serial.print("i: "); Serial.println(i, HEX);
      if (out & i) {
	digitalWrite(_mosi, HIGH);
      } else {
	digitalWrite(_mosi, LOW);
      }
      
      in <<= 1;
      if (digitalRead(_miso)) 
	in |= 1;
      
      digitalWrite(_sck, HIGH);
      delayMicroseconds(spiBitDelay);
    }
    digitalWrite(_sck, LOW);
    // Make sure clock ends low
    
    //Serial.print(" <= 0x"); Serial.println(in, HEX);
    
    return in;
  }
  error(F("Neither hardware or software SPI modes selected"));
  return -1;
}

/*******************************************************
 * Low level support functions
 */


/*
 * hexToByte
 * Turn a Hex digit (0..9, A..F) into the equivalent binary value (0-16)
 */
byte Adafruit_AVRProg::hexToByte (byte h)
{
  if (h >= '0' && h <= '9')
    return(h - '0');
  if (h >= 'A' && h <= 'F')
    return((h - 'A') + 10);
  error(F("Bad hex digit!"));
  return -1;
}

/*
 * pulse
 * turn a pin on and off a few times; indicates life via LED
 */
void Adafruit_AVRProg::pulseLED (int pin, int times) {
  uint8_t PTIME = 30;
  pinMode(pin, OUTPUT);
  do {
    digitalWrite(pin, HIGH);
    delay(PTIME);
    digitalWrite(pin, LOW);
    delay(PTIME);
  } 
  while (times--);
}


void Adafruit_AVRProg::error(const char *string) { 
  Serial.println(string); 
  if (errLED > 0) {
    pinMode(errLED, OUTPUT);
    digitalWrite(errLED, HIGH); 
  } 
  while(1) {
  }
}


void Adafruit_AVRProg::error(const __FlashStringHelper *string) { 
  Serial.println(string); 
  if (errLED > 0) {
    pinMode(errLED, OUTPUT);
    digitalWrite(errLED, HIGH); 
  } 
  while(1) {
  }
}