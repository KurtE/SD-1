/* SD library compatibility wrapper for use of SdFat on Teensy
 * Copyright (c) 2020, Paul Stoffregen, paul@pjrc.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <Arduino.h>
#include <SD.h>

SDClass SD;

// defines for SD card insertion, mostly specific to SD BUILTIN
enum {CPP_PRESENT=0x80, CPP_CD_ACTIVE=0x40, CPP_CD_DETECTED=0x20};
#if defined(ARDUINO_TEENSY41)
static const uint8_t _SD_DAT3=46;
#elif defined(ARDUINO_TEENSY40) || defined(ARDUINO_TEENSY_MICROMOD)
static const uint8_t _SD_DAT3=38;
#elif defined(ARDUINO_TEENSY35) || defined(ARDUINO_TEENSY36)
static const uint8_t _SD_DAT3=62;
#endif


#ifdef __arm__
void SDClass::dateTime(uint16_t *date, uint16_t *time)
{
	uint32_t now = Teensy3Clock.get();
	if (now < 315532800) { // before 1980
		*date = 0;
		*time = 0;
	} else {
		DateTimeFields datetime;
		breakTime(now, datetime);
		*date = FS_DATE(datetime.year + 1900, datetime.mon + 1, datetime.mday);
		*time = FS_TIME(datetime.hour, datetime.min, datetime.sec);
	}
}
#endif

bool SDClass::begin(uint8_t csPin) {
	csPin_ = csPin; // remember which one passed in. 
#ifdef __arm__
	FsDateTime::setCallback(dateTime);
#endif
#ifdef BUILTIN_SDCARD
	if (csPin == BUILTIN_SDCARD) {
		bool ret = sdfs.begin(SdioConfig(FIFO_SDIO));
		if (!ret) {
			pinMode(_SD_DAT3, INPUT_PULLDOWN);
			cardPreviouslyPresent = CPP_CD_ACTIVE;
		}
		else cardPreviouslyPresent = 0;
		return ret;
	}
#endif
	if (csPin < NUM_DIGITAL_PINS) {
		bool ret = sdfs.begin(SdSpiConfig(csPin, SHARED_SPI, SD_SCK_MHZ(25)));
		cardPreviouslyPresent = ret ? CPP_PRESENT : 0;
		return ret;
	}
	return false;
}


bool SDClass::format(int type, char progressChar, Print& pr)
{
	SdCard *card = sdfs.card();
	if (!card) return false; // no SD card
	uint32_t sectors = card->sectorCount();
	if (sectors <= 12288) return false; // card too small
	uint8_t *buf = (uint8_t *)malloc(512);
	if (!buf) return false; // unable to allocate memory
	bool ret;
	if (sectors > 67108864) {
#ifdef __arm__
		ExFatFormatter exFatFormatter;
		ret = exFatFormatter.format(card, buf, &pr);
#else
		ret = false;
#endif
	} else {
		FatFormatter fatFormatter;
		ret = fatFormatter.format(card, buf, &pr);
	}
	free(buf);
	if (ret) {
		// TODO: Is begin() really necessary?  Is a quicker way possible?
		sdfs.restart(); // TODO: is sdfs.volumeBegin() enough??
	}
	return ret;
}

bool SDClass::mediaPresent()
{
	#ifdef BUILTIN_SDCARD 
	// BUGBUG could/should be member variable
	static elapsedMillis em_cd_detected;
	#define MAX_CD_DETECTED_TEST 5000
	#endif
	//Serial.print("mediaPresent: ");
	bool ret;
	//elapsedMicros em = 0;
	SdCard *card = sdfs.card();
	//Serial.printf("sdfs.card() %u %u\n", (uint32_t)card, (uint32_t)em);
	if (card) {
		if (cardPreviouslyPresent == CPP_PRESENT) {
			#ifdef BUILTIN_SDCARD
			uint32_t s = card->status();
			#else
			const uint32_t s = 0xFFFFFFFF;
			#endif
			if (s == 0xFFFFFFFF) {
				// SPI doesn't have 32 bit status, read CID register
				cid_t cid;
				ret = card->readCID(&cid);
				//Serial.print(ret ? "CID=ok" : "CID=unreadable");
			} else if (s == 0) {
				// assume zero status means card removed
				// bits 12:9 are card state, which should
				// normally be 101 = data transfer mode
				//Serial.print("status=offline");
				cardPreviouslyPresent = 0;
				#ifdef BUILTIN_SDCARD
				if (csPin_ == BUILTIN_SDCARD) {
					// setup the 
					pinMode(_SD_DAT3, INPUT_PULLDOWN);
					cardPreviouslyPresent = CPP_CD_ACTIVE;
				} 
			    #endif
				ret = false;
			} else {
				//Serial.print("status=present");
				ret = true;
			}
		} else {
			#ifdef BUILTIN_SDCARD
			if (csPin_ == BUILTIN_SDCARD) {
				if (cardPreviouslyPresent == CPP_CD_DETECTED) {
					// if we previously detected DAT3 and not card ready then 
					// try for awhile to see if it will start
					ret = sdfs.restart();
					if (ret) cardPreviouslyPresent = CPP_PRESENT; 
					else {
						if (em_cd_detected >= MAX_CD_DETECTED_TEST) {
							pinMode(_SD_DAT3, INPUT_PULLDOWN);
							cardPreviouslyPresent = CPP_CD_ACTIVE;
						}
					}
				}

				else if ((cardPreviouslyPresent == CPP_CD_ACTIVE) && !digitalReadFast(_SD_DAT3)) 
					ret = false;
				else {
					ret = sdfs.restart();
					cardPreviouslyPresent = ret? CPP_PRESENT : CPP_CD_DETECTED;
				}
			} else 
			#endif
			{
			// TODO: need a quick test, only call begin if likely present
				ret = sdfs.restart();
				if (ret) cardPreviouslyPresent = CPP_PRESENT;
			//Serial.print(ret ? "begin ok" : "begin nope");
			}
		}
	} else {
		//Serial.print("no card");
		ret = false;
	}
	//Serial.println();
	//cardPreviouslyPresent = ret;
	return ret;
}



