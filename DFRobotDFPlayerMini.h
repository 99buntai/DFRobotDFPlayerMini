/*!
 * @file DFRobotDFPlayerMini.h
 * @brief Optimized DFPlayer Mini MP3 module driver (Replacement for DFRobotDFPlayerMini library)
 * 
 * This library provides a reliable interface to the DFPlayer Mini module, maintaining 
 * compatibility with the original DFRobotDFPlayerMini API. It supports Arduino, ESP8266, 
 * and ESP32 boards using hardware or software serial at 9600 baud.
 * 
 * Improvements:
 *  - Robust parsing of incoming serial data with frame validation (start byte, length, checksum, end byte).
 *  - Safe, non-blocking waits with timeouts to prevent watchdog resets (especially on ESP8266/ESP32).
 *  - Optional debug logging for sent/received data, enabled via DFPLAYER_DEBUG macro.
 *  - Thread-safe and reentrant design for multiple instances (each instance manages its own serial stream).
 * 
 * The class and constants remain identical to the original library for drop-in replacement.
 * 
 * © 2024 Original by DFRobot, Optimized version by 99buntai. Licensed under MIT License.
 */

#ifndef DFRobotDFPlayerMini_h
#define DFRobotDFPlayerMini_h

#include <Arduino.h>

// Equalizer presets
#define DFPLAYER_EQ_NORMAL 0
#define DFPLAYER_EQ_POP 1
#define DFPLAYER_EQ_ROCK 2
#define DFPLAYER_EQ_JAZZ 3
#define DFPLAYER_EQ_CLASSIC 4
#define DFPLAYER_EQ_BASS 5

// Playback devices
#define DFPLAYER_DEVICE_U_DISK 1
#define DFPLAYER_DEVICE_SD 2
#define DFPLAYER_DEVICE_AUX 3    // not used by DFPlayer Mini
#define DFPLAYER_DEVICE_SLEEP 4  // enters sleep mode
#define DFPLAYER_DEVICE_FLASH 5

// Frame lengths
#define DFPLAYER_RECEIVED_LENGTH 10   // length of incoming frame
#define DFPLAYER_SEND_LENGTH 10       // length of outgoing frame

// Enable debug logging by defining DFPLAYER_DEBUG (e.g., via build flags or before including this header)
//#define DFPLAYER_DEBUG

// Player return types (for readType())
#define TimeOut 0               // Timeout or no response
#define WrongStack 1            // Malformed or unexpected frame
#define DFPlayerCardInserted 2
#define DFPlayerCardRemoved 3
#define DFPlayerCardOnline 4
#define DFPlayerPlayFinished 5
#define DFPlayerError 6
#define DFPlayerUSBInserted 7
#define DFPlayerUSBRemoved 8
#define DFPlayerUSBOnline 9
#define DFPlayerCardUSBOnline 10
#define DFPlayerFeedBack 11     // Generic feedback for query commands

// Error codes (for DFPlayerError type's parameter via read())
#define Busy 1           // Module is busy (e.g., initializing or reading)
#define Sleeping 2       // Module is in sleep mode
#define SerialWrongStack 3  // Incorrect serial data received
#define CheckSumNotMatch 4  // Checksum validation failed
#define FileIndexOut 5    // File index out of bounds
#define FileMismatch 6    // File unable to play (mismatch or unsupported)
#define Advertise 7       // In advertising (ADVERT) mode, cannot execute command

#define Stack_Header      0
#define Stack_Version     1
#define Stack_Length      2
#define Stack_Command     3
#define Stack_ACK         4
#define Stack_Parameter   5
#define Stack_CheckSum    7
#define Stack_End         9

class DFRobotDFPlayerMini {
public:
    DFRobotDFPlayerMini() : 
        _serial(nullptr), _timeOutDuration(500), _receivedIndex(0),
        _isAvailable(false), _isSending(false),
        _handleType(0), _handleCommand(0), _handleParameter(0) {
        // Prepare the static part of the send buffer (start, version, length, end)
        _sending[0] = 0x7E;    // Start byte
        _sending[1] = 0xFF;    // Version (fixed)
        _sending[2] = 0x06;    // Length (fixed)
        _sending[3] = 0x00;    // Command will be filled in on send
        _sending[4] = 0x00;    // ACK flag (0 or 1) will be set in begin()
        _sending[5] = 0x00;    // Parameter high byte (filled as needed)
        _sending[6] = 0x00;    // Parameter low byte  (filled as needed)
        // [7] and [8] will hold checksum, [9] is end byte (0xEF)
        _sending[7] = 0x00;
        _sending[8] = 0x00;
        _sending[9] = 0xEF;    // End byte
    }

    // Initialize the DFPlayer module. 
    // stream: the serial stream (HardwareSerial or SoftwareSerial) already begun at 9600 baud.
    // isACK: true to enable command acknowledgements (default), false to disable ACK for faster/non-blocking operation.
    // doReset: true to send a reset command to DFPlayer on begin (recommended to initialize module).
    bool begin(Stream &stream, bool isACK = true, bool doReset = true);

    // Check if the DFPlayer has sent any message (track end, feedback, error, etc.).
    // Returns true if a new event is available to read via readType() and read().
    bool available();

    // Wait for an event to be available or until timeout (in milliseconds).
    // If duration is 0, uses the default _timeOutDuration. Returns true if an event arrived, false if timed out.
    bool waitAvailable(unsigned long duration = 0);

    // Get the type of the last received message (one of the DFPlayer... constants above, e.g., DFPlayerPlayFinished, DFPlayerError, etc.).
    uint8_t readType();

    // Get the parameter of the last received message (e.g., track number for DFPlayerPlayFinished, error code for DFPlayerError, etc.).
    uint16_t read();

    // Get the last received command byte from the DFPlayer (for low-level use or debugging).
    uint8_t readCommand();

    // Set the serial communication timeout duration (milliseconds). Default is 500ms.
    void setTimeOut(unsigned long timeOutDuration);

    // Playback control methods (same as original library):
    void next();                      // Play next track
    void previous();                  // Play previous track
    void play(int fileNumber);        // Play track by index (1...65535)
    void volumeUp();                  // Increase volume by 1
    void volumeDown();                // Decrease volume by 1
    void volume(uint8_t volume);      // Set volume (0-30)
    void EQ(uint8_t eq);              // Set EQ (0-5, use DFPLAYER_EQ_* constants)
    void loop(int fileNumber);        // Play track by index on repeat
    void outputDevice(uint8_t device);// Set output device (see DFPLAYER_DEVICE_* constants)
    void sleep();                     // Enter sleep mode (low power)
    void reset();                     // Reset the DFPlayer module
    void start();                     // Start or resume playback
    void pause();                     // Pause playback
    void playFolder(uint8_t folderNumber, uint8_t fileNumber); // Play file in specific folder (folders 01-99, files 001-255)
    void outputSetting(bool enable, uint8_t gain);  // Set output settings (enable/disable and gain 0-31)
    void enableLoopAll();             // Enable loop for all tracks
    void disableLoopAll();            // Disable loop for all tracks
    void playMp3Folder(int fileNumber);   // Play file in root "MP3" folder by index (supports 4-digit and 5-digit filenames)
    void advertise(int fileNumber);       // Play advertisement track (in "ADVERT" folder), interrupts current track
    void playLargeFolder(uint8_t folderNumber, uint16_t fileNumber); // Play file in folder>99 or file index >255 (combined command)
    void stopAdvertise();                 // Stop advertisement playback and resume background track
    void stop();                          // Stop playback
    void loopFolder(int folderNumber);    // Loop all tracks in a folder
    void randomAll();                     // Play all tracks randomly
    void enableLoop();                    // Enable single track loop (repeat current track)
    void disableLoop();                   // Disable single track loop (play track once)
    void enableDAC();                     // Enable DAC (disable mute) - 0x1A command with param 0
    void disableDAC();                    // Disable DAC (enable mute) - 0x1A command with param 1

    // Query methods (send a query command and wait for the response):
    int readState();                      // Read current play status (1: playing, 2: paused, 3: stopped)
    int readVolume();                     // Read current volume (0-30)
    int readEQ();                         // Read current EQ setting (0-5)
    int readFileCounts(uint8_t device);   // Read number of files on specified device (U disk/SD/Flash)
    int readCurrentFileNumber(uint8_t device); // Read current file number on specified device
    int readFileCountsInFolder(int folderNumber); // Read number of files in a folder
    int readFolderCounts();               // Read total number of folders on the SD card
    int readFileCounts();                 // [Alias] Read number of files on SD card (DFPLAYER_DEVICE_SD)
    int readCurrentFileNumber();          // [Alias] Read current file number on SD card (DFPLAYER_DEVICE_SD)

private:
    Stream* _serial;                 // Serial stream used for communication (HardwareSerial or SoftwareSerial)
    unsigned long _timeOutTimer;     // Timer to track for timeouts
    unsigned long _timeOutDuration;  // Duration (ms) to wait for incoming data (ACK or response)
    uint8_t _received[DFPLAYER_RECEIVED_LENGTH]; // Buffer for incoming data frame
    uint8_t _sending[DFPLAYER_SEND_LENGTH];      // Buffer for outgoing data frame
    uint8_t _receivedIndex;          // Current index in _received buffer when assembling a frame
    bool _isAvailable;              // Flag indicating a complete message (event) is ready to be read
    bool _isSending;                // Flag indicating a command has been sent and is awaiting ACK

    uint8_t _handleType;            // Last message type (for readType)
    uint8_t _handleCommand;         // Last command byte received from DFPlayer
    uint16_t _handleParameter;      // Last 16-bit parameter received from DFPlayer

    // Internal methods for building and sending command frames
    void sendStack();                                      // Send the prepared _sending buffer over serial
    void sendStack(uint8_t command);                       // Send command with no parameters (uses default param=0)
    void sendStack(uint8_t command, uint16_t argument);    // Send command with 16-bit parameter
    void sendStack(uint8_t command, uint8_t argHigh, uint8_t argLow); // Send command with two 8-bit parameters

    // Internal utilities for frame handling
    void enableACK();                // Turn on ACK request in outgoing commands
    void disableACK();               // Turn off ACK request in outgoing commands
    void uint16ToArray(uint16_t value, uint8_t *array); // Helper to split uint16 into two bytes
    uint16_t arrayToUint16(const uint8_t *array);       // Helper to combine two bytes into uint16
    uint16_t calculateCheckSum(const uint8_t *buffer);  // Compute checksum for a 10-byte frame

    void parseStack();               // Interpret a fully received frame in _received buffer
    bool validateStack();            // Validate checksum (and any other needed checks) for received frame

    // Handle an incoming message or error, setting internal state
    bool handleMessage(uint8_t type, uint16_t parameter = 0);
    bool handleError(uint8_t type, uint16_t parameter = 0);
};

#endif  // DFRobotDFPlayerMini_h
