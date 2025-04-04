/*!
 * @file DFRobotDFPlayerMini.cpp
 * @brief Implementation of the DFRobotDFPlayerMini replacement library.
 *
 * This file contains the definitions for all functions declared in DFRobotDFPlayerMini.h.
 * The implementation ensures reliable serial communication with the DFPlayer Mini module.
 */

#include "DFRobotDFPlayerMini.h"

// Helper: split a 16-bit value into two bytes (big-endian order)
void DFRobotDFPlayerMini::uint16ToArray(uint16_t value, uint8_t *array) {
    array[0] = (uint8_t)(value >> 8);   // high byte
    array[1] = (uint8_t)(value & 0xFF); // low byte
}

// Helper: calculate checksum for the current frame (two-byte checksum = - (VER+LEN+CMD+FEEDBACK+PARAM_HIGH+PARAM_LOW))
uint16_t DFRobotDFPlayerMini::calculateCheckSum(const uint8_t *buffer) {
    uint16_t sum = 0;
    // Sum bytes from version (index 1) to the last parameter byte (index 6)
    for (int i = Stack_Version; i <= Stack_Parameter + 1; ++i) {
        sum += buffer[i];
    }
    // Checksum is two bytes: take two's complement of the sum
    return 0 - sum;
}

// Internal: Enable ACK request in outgoing command frames
void DFRobotDFPlayerMini::enableACK() {
    _sending[4] = 0x01;  // Stack_ACK index (4) set to 1 to request ACK from module
}

// Internal: Disable ACK request in outgoing command frames
void DFRobotDFPlayerMini::disableACK() {
    _sending[4] = 0x00;  // Stack_ACK index (4) set to 0 to not request ACK
}

// Begin communication with DFPlayer
bool DFRobotDFPlayerMini::begin(Stream &stream, bool isACK, bool doReset) {
    _serial = &stream;
    _receivedIndex = 0;
    _isAvailable = false;
    _isSending = false;
    // Configure ACK mode based on parameter
    if (isACK) enableACK();
    else       disableACK();

    if (doReset) {
        // Issue a reset command to initialize DFPlayer
        reset();
        // Wait up to 2 seconds for the DFPlayer to send an "online" message (e.g., card inserted)
        waitAvailable(2000);
        // Give an extra brief delay to allow DFPlayer initialization events to finish
        delay(200);
    } else {
        // Without reset, assume the device is already online (simulate CardOnline event)
        _handleType = DFPlayerCardOnline;
        _isAvailable = false;
    }
    // The begin is successful if either we detected a card/USB online event, or if ACK is disabled (cannot verify)
    uint8_t type = readType();
    return (type == DFPlayerCardOnline || type == DFPlayerUSBOnline || !isACK);
}

// Send the current frame in _sending buffer over serial.
void DFRobotDFPlayerMini::sendStack() {
    if (_sending[4] == 0x01) {  // ACK requested
        // If waiting for a previous ACK, ensure it's done before sending a new command
        // (This prevents buffer overflow by not piling up commands.)
        while (_isSending) {
            yield();            // yield to avoid blocking (feeds watchdog on ESP)
            waitAvailable();    // process incoming data (which might include the awaited ACK)
        }
    }
#ifdef DFPLAYER_DEBUG
    // Print out the bytes being sent for debugging
    Serial.print(F("[DFPlayer Debug] Sending: "));
    for (int i = 0; i < DFPLAYER_SEND_LENGTH; ++i) {
        Serial.print(_sending[i], HEX);
        Serial.print(' ');
    }
    Serial.println();
#endif
    // Write all 10 bytes of the command frame to the serial port
    _serial->write(_sending, DFPLAYER_SEND_LENGTH);

    _timeOutTimer = millis();
    // If ACK is requested, mark as waiting for ACK; if not, we are not in a waiting state.
    _isSending = (_sending[4] == 0x01);
    if (!_isSending) {
        // If not expecting an ACK, insert a small delay to avoid sending commands too rapidly
        delay(10);
    }
}

// Overloads for sendStack with different parameter types:
void DFRobotDFPlayerMini::sendStack(uint8_t command) {
    sendStack(command, (uint16_t)0);
}
void DFRobotDFPlayerMini::sendStack(uint8_t command, uint16_t argument) {
    _sending[3] = command;                     // Stack_Command index (3)
    uint16ToArray(argument, _sending + 5);     // place argument into Stack_Parameter (index 5-6)
    uint16ToArray(calculateCheckSum(_sending), _sending + 7);  // compute checksum into index 7-8
    _sending[9] = 0xEF;                        // end byte
    sendStack();
}
void DFRobotDFPlayerMini::sendStack(uint8_t command, uint8_t argHigh, uint8_t argLow) {
    uint16_t arg = ((uint16_t)argHigh << 8) | argLow;
    sendStack(command, arg);
}

// Check if data is available from DFPlayer, and parse frames. 
// Returns true if a complete message (event) was received.
bool DFRobotDFPlayerMini::available() {
    // Read as many bytes as available, one at a time, to assemble frames.
    while (_serial->available()) {
        uint8_t byteIn = _serial->read();
#ifdef DFPLAYER_DEBUG
        Serial.print(F("[DFPlayer Debug] Received byte: 0x"));
        Serial.print(byteIn, HEX);
        Serial.print(' ');
#endif
        if (_receivedIndex == 0) {
            // Looking for the start byte 0x7E
            if (byteIn == 0x7E) {
                _received[_receivedIndex++] = byteIn;
#ifdef DFPLAYER_DEBUG
                Serial.println(F("<= Start of frame"));
#endif
            }
            // If byte is not 0x7E, ignore it (out-of-sync or noise)
            continue;
        }

        // Store the byte and advance index
        _received[_receivedIndex++] = byteIn;

        // If we just stored the second byte, verify version is correct (should be 0xFF per protocol)
        if (_receivedIndex == 2) { // index 1 just filled (version byte)
            if (_received[1] != 0xFF) {
                // Version mismatch – frame is invalid
#ifdef DFPLAYER_DEBUG
                Serial.println(F("<< Invalid version byte, discarding frame"));
#endif
                return handleError(WrongStack);  // handle error (sets _isSending=false, etc.)
            }
        }
        // If we just stored the third byte, verify length (should be 0x06 for all standard frames)
        else if (_receivedIndex == 3) { // index 2 just filled (length byte)
            if (_received[2] != 0x06) {
#ifdef DFPLAYER_DEBUG
                Serial.println(F("<< Invalid length byte, discarding frame"));
#endif
                return handleError(WrongStack);
            }
        }
        // If we have read the full frame length (10 bytes):
        if (_receivedIndex >= DFPLAYER_RECEIVED_LENGTH) {
            // We have a complete frame in _received[0..9]
#ifdef DFPLAYER_DEBUG
            Serial.println(F("<= End of frame received"));
#endif
            _receivedIndex = 0;  // reset index for next frame assembly
            // Verify start and end bytes, and checksum
            if (_received[0] != 0x7E || _received[9] != 0xEF) {
                // Invalid frame markers
#ifdef DFPLAYER_DEBUG
                Serial.println(F("<< Frame start/end byte error"));
#endif
                return handleError(WrongStack);
            }
            if (!validateStack()) {
                // Checksum mismatch
#ifdef DFPLAYER_DEBUG
                Serial.println(F("<< Checksum error"));
#endif
                return handleError(WrongStack);
            }
            // Frame is valid – parse the content
            parseStack();
            // If the parsed frame was an ACK (0x41) it won't set _isAvailable, just clears _isSending.
            // Continue reading any further bytes in buffer (do not return true yet in that case).
            if (_isAvailable) {
                // A non-ACK event is ready for user consumption
                return true;
            }
            // If it was only an ACK, loop continues to check next bytes (if any)
        }
    }
    // If we exit the loop, either no more data or only partial frame collected.
    return _isAvailable;
}

// Wait for data to be available for up to the specified duration.
// This will repeatedly call available() until an event is ready or timeout occurs.
bool DFRobotDFPlayerMini::waitAvailable(unsigned long duration) {
    if (duration == 0) duration = _timeOutDuration;
    unsigned long startTime = millis();
    // Loop until available() returns true (event ready) or timeout elapses
    while (!available()) {
        if (millis() - startTime >= duration) {
            // Timeout occurred, register a TimeOut error
            handleError(TimeOut);
            // Stop waiting if timed out
            return false;
        }
        // Yield to prevent watchdog reset (especially on ESP8266/ESP32)
        yield();
    }
    return true;
}

// Once a full frame is validated, this function interprets the command and parameters.
void DFRobotDFPlayerMini::parseStack() {
    uint8_t cmd = _received[3];  // Command byte from frame
    // Special case: ACK response (0x41) – this just indicates the module received a command.
    if (cmd == 0x41) {
        // Received ACK, no user-facing event. Just mark the sending as completed.
        _isSending = false;
        return;
    }
    // Store the command and parameter for user retrieval
    _handleCommand = cmd;
    _handleParameter = arrayToUint16(_received + 5);
    // Determine the type of event based on command code
    switch (cmd) {
        case 0x3C: // U-disk finished playing current track
        case 0x3D: // TF card finished playing current track
        case 0x3E: // Flash finished playing current track
            // All of these indicate a track finished playing on some device
            handleMessage(DFPlayerPlayFinished, _handleParameter);
            break;
        case 0x3A: // Card or USB inserted
            if (_handleParameter & 0x01) {
                handleMessage(DFPlayerUSBInserted, _handleParameter);
            } else if (_handleParameter & 0x02) {
                handleMessage(DFPlayerCardInserted, _handleParameter);
            }
            break;
        case 0x3B: // Card or USB removed
            if (_handleParameter & 0x01) {
                handleMessage(DFPlayerUSBRemoved, _handleParameter);
            } else if (_handleParameter & 0x02) {
                handleMessage(DFPlayerCardRemoved, _handleParameter);
            }
            break;
        case 0x3F: // Device online (initialization result after reset)
            // 0x3F returns a parameter indicating which devices are online (bitmask)
            // 0x01 = USB, 0x02 = SD, 0x04 = PC (not used here). Combine bits means both.
            if (_handleParameter & 0x01 && _handleParameter & 0x02) {
                handleMessage(DFPlayerCardUSBOnline, _handleParameter);
            } else if (_handleParameter & 0x01) {
                handleMessage(DFPlayerUSBOnline, _handleParameter);
            } else if (_handleParameter & 0x02) {
                handleMessage(DFPlayerCardOnline, _handleParameter);
            }
            break;
        case 0x40: // Error report from DFPlayer
            // The _handleParameter contains an error code (1-7) corresponding to Busy, Sleeping, etc.
            handleMessage(DFPlayerError, _handleParameter);
            break;
        case 0x42: // Query current status (response to 0x42 command)
        case 0x43: // Query current volume
        case 0x44: // Query current EQ
        case 0x45: // Query current playback mode
        case 0x46: // Query software version
        case 0x47: // Query U-disk total files
        case 0x48: // Query TF card total files
        case 0x49: // Query flash total files
        case 0x4B: // Query U-disk current file
        case 0x4C: // Query TF card current file
        case 0x4D: // Query flash current file
        case 0x4E: // Query folder file count
        case 0x4F: // Query folder count
            // All these queries respond with a value, which we store as generic feedback
            handleMessage(DFPlayerFeedBack, _handleParameter);
            break;
        default:
            // Unknown or unexpected command
            handleError(WrongStack);
            break;
    }
}

// Combine two bytes from array into a single 16-bit value (big-endian)
uint16_t DFRobotDFPlayerMini::arrayToUint16(const uint8_t *array) {
    uint16_t value = ((uint16_t)array[0] << 8) | array[1];
    return value;
}

// Validate the checksum of the received frame
bool DFRobotDFPlayerMini::validateStack() {
    // Calculate checksum from the received data and compare with the two checksum bytes in the frame
    uint16_t sum = 0;
    for (int i = 1; i <= 6; ++i) { // sum from version (1) to param low (6)
        sum += _received[i];
    }
    uint16_t expectedCheckSum = 0 - sum;
    uint16_t receivedCheckSum = arrayToUint16(_received + 7);
    return (expectedCheckSum == receivedCheckSum);
}

// Handle a normal message event: set state for readType() and read()
bool DFRobotDFPlayerMini::handleMessage(uint8_t type, uint16_t parameter) {
    _handleType = type;
    _handleParameter = parameter;
    _isAvailable = true;   // mark that an event is ready to be read
    _isSending = false;    // in case we were waiting for an ACK, it's done now
    // Reset index to start looking for next frame (in case not already reset)
    _receivedIndex = 0;
#ifdef DFPLAYER_DEBUG
    Serial.print(F("[DFPlayer Debug] Event: type="));
    Serial.print(type);
    Serial.print(F(", parameter="));
    Serial.println(parameter);
#endif
    return true;
}

// Handle an error event: treat it as a message but return false (for internal use)
bool DFRobotDFPlayerMini::handleError(uint8_t type, uint16_t parameter) {
    handleMessage(type, parameter);
    _isSending = false;
    // We do not set _isAvailable to false here; the error can be retrieved via readType/read if needed.
    return false;
}

// Retrieve the last message type and reset availability flag
uint8_t DFRobotDFPlayerMini::readType() {
    uint8_t t = _handleType;
    _isAvailable = false;  // clear the flag after reading
    return t;
}

// Retrieve the last message parameter (16-bit) and reset availability flag
uint16_t DFRobotDFPlayerMini::read() {
    uint16_t param = _handleParameter;
    _isAvailable = false;
    return param;
}

// Retrieve the last command byte received (for low-level debugging)
uint8_t DFRobotDFPlayerMini::readCommand() {
    return _handleCommand;
}

// Set a custom timeout duration (in milliseconds) for waiting on responses/ACKs
void DFRobotDFPlayerMini::setTimeOut(unsigned long timeOutDuration) {
    _timeOutDuration = timeOutDuration;
}

/** 
 * Below are the implementations of all control and query methods.
 * Each sends the corresponding command to the DFPlayer. 
 * Query functions will wait for a response and return the result, or -1 on error.
 */
void DFRobotDFPlayerMini::next() {
    sendStack(0x01);
}
void DFRobotDFPlayerMini::previous() {
    sendStack(0x02);
}
void DFRobotDFPlayerMini::play(int fileNumber) {
    sendStack(0x03, (uint16_t)fileNumber);
}
void DFRobotDFPlayerMini::volumeUp() {
    sendStack(0x04);
}
void DFRobotDFPlayerMini::volumeDown() {
    sendStack(0x05);
}
void DFRobotDFPlayerMini::volume(uint8_t volume) {
    sendStack(0x06, (uint16_t)volume);
}
void DFRobotDFPlayerMini::EQ(uint8_t eq) {
    sendStack(0x07, (uint16_t)eq);
}
void DFRobotDFPlayerMini::loop(int fileNumber) {
    sendStack(0x08, (uint16_t)fileNumber);
}
void DFRobotDFPlayerMini::outputDevice(uint8_t device) {
    sendStack(0x09, (uint16_t)device);
    // Changing the device (U disk, SD, Flash) triggers a reinitialization on the DFPlayer.
    // A short delay helps to ensure the module switches device properly before sending other commands.
    delay(200);
}
void DFRobotDFPlayerMini::sleep() {
    sendStack(0x0A);
}
void DFRobotDFPlayerMini::reset() {
    sendStack(0x0C);
}
void DFRobotDFPlayerMini::start() {
    sendStack(0x0D);
}
void DFRobotDFPlayerMini::pause() {
    sendStack(0x0E);
}
void DFRobotDFPlayerMini::playFolder(uint8_t folderNumber, uint8_t fileNumber) {
    // folderNumber (1-99), fileNumber (1-255)
    sendStack(0x0F, folderNumber, fileNumber);
}
void DFRobotDFPlayerMini::outputSetting(bool enable, uint8_t gain) {
    // enable: true (on) or false (off), gain: 0-31 (volume gain)
    sendStack(0x10, (uint8_t)enable, gain);
}
void DFRobotDFPlayerMini::enableLoopAll() {
    // 0x11 command, parameter 1 = loop all
    sendStack(0x11, (uint16_t)0x01);
}
void DFRobotDFPlayerMini::disableLoopAll() {
    // 0x11 command, parameter 0 = stop loop all
    sendStack(0x11, (uint16_t)0x00);
}
void DFRobotDFPlayerMini::playMp3Folder(int fileNumber) {
    // Play file in "MP3" root folder by index (1-65535)
    sendStack(0x12, (uint16_t)fileNumber);
}
void DFRobotDFPlayerMini::advertise(int fileNumber) {
    // Play advertisement track from "ADVERT" folder (1-65535)
    sendStack(0x13, (uint16_t)fileNumber);
}
void DFRobotDFPlayerMini::playLargeFolder(uint8_t folderNumber, uint16_t fileNumber) {
    // 0x14 command: folder (1-15) and file (1-4095) combined into 16-bit (folder in high 4 bits, file in low 12 bits)
    uint16_t combined = ((uint16_t)folderNumber << 12) | (fileNumber & 0x0FFF);
    sendStack(0x14, combined);
}
void DFRobotDFPlayerMini::stopAdvertise() {
    sendStack(0x15);
}
void DFRobotDFPlayerMini::stop() {
    sendStack(0x16);
}
void DFRobotDFPlayerMini::loopFolder(int folderNumber) {
    // Loop all tracks in specified folder (folderNumber 1-99)
    sendStack(0x17, (uint16_t)folderNumber);
}
void DFRobotDFPlayerMini::randomAll() {
    sendStack(0x18);
}
void DFRobotDFPlayerMini::enableLoop() {
    // Enable single track loop: send 0x19 with parameter 0
    sendStack(0x19, (uint16_t)0x00);
}
void DFRobotDFPlayerMini::disableLoop() {
    // Disable single track loop: send 0x19 with parameter 1
    sendStack(0x19, (uint16_t)0x01);
}
void DFRobotDFPlayerMini::enableDAC() {
    // 0x1A with 0: enable DAC (un-mute audio output)
    sendStack(0x1A, (uint16_t)0x00);
}
void DFRobotDFPlayerMini::disableDAC() {
    // 0x1A with 1: disable DAC (mute audio output)
    sendStack(0x1A, (uint16_t)0x01);
}

// Query functions: send query command and wait for response, returning the result or -1 on error.
int DFRobotDFPlayerMini::readState() {
    sendStack(0x42);
    if (waitAvailable()) {
        // Check if the response type is DFPlayerFeedBack (which holds the data for queries)
        if (readType() == DFPlayerFeedBack) {
            return (int)read();
        }
    }
    return -1; // on timeout or wrong type
}
int DFRobotDFPlayerMini::readVolume() {
    sendStack(0x43);
    if (waitAvailable()) {
        if (readType() == DFPlayerFeedBack) {
            return (int)read();
        }
    }
    return -1;
}
int DFRobotDFPlayerMini::readEQ() {
    sendStack(0x44);
    if (waitAvailable()) {
        if (readType() == DFPlayerFeedBack) {
            return (int)read();
        }
    }
    return -1;
}
int DFRobotDFPlayerMini::readFileCounts(uint8_t device) {
    // Send appropriate query based on device code
    if (device == DFPLAYER_DEVICE_U_DISK) {
        sendStack(0x47);
    } else if (device == DFPLAYER_DEVICE_SD) {
        sendStack(0x48);
    } else if (device == DFPLAYER_DEVICE_FLASH) {
        sendStack(0x49);
    } else {
        return -1; // invalid device
    }
    if (waitAvailable()) {
        if (readType() == DFPlayerFeedBack) {
            return (int)read();
        }
    }
    return -1;
}
int DFRobotDFPlayerMini::readCurrentFileNumber(uint8_t device) {
    if (device == DFPLAYER_DEVICE_U_DISK) {
        sendStack(0x4B);
    } else if (device == DFPLAYER_DEVICE_SD) {
        sendStack(0x4C);
    } else if (device == DFPLAYER_DEVICE_FLASH) {
        sendStack(0x4D);
    } else {
        return -1;
    }
    if (waitAvailable()) {
        if (readType() == DFPlayerFeedBack) {
            return (int)read();
        }
    }
    return -1;
}
int DFRobotDFPlayerMini::readFileCountsInFolder(int folderNumber) {
    sendStack(0x4E, (uint16_t)folderNumber);
    if (waitAvailable()) {
        if (readType() == DFPlayerFeedBack) {
            return (int)read();
        }
    }
    return -1;
}
int DFRobotDFPlayerMini::readFolderCounts() {
    sendStack(0x4F);
    if (waitAvailable()) {
        if (readType() == DFPlayerFeedBack) {
            return (int)read();
        }
    }
    return -1;
}
int DFRobotDFPlayerMini::readFileCounts() {
    // Default to SD card
    return readFileCounts(DFPLAYER_DEVICE_SD);
}
int DFRobotDFPlayerMini::readCurrentFileNumber() {
    // Default to SD card
    return readCurrentFileNumber(DFPLAYER_DEVICE_SD);
}
