# DFPlayer Mini Serial Protocol Documentation

## 3.2 Serial Communication Format

- **Protocol**: Asynchronous serial communication
- **Baud rate**: 9600bps
- **Check digit**: none
- **Data bits**: 8
- **Stop bit**: 1
- **Flow control**: none

### Communication Format:
`$S VER Len CMD Feedback para1 para2 checksum $O`

| Field | Name | Value | Description |
|-------|------|-------|-------------|
| $S | Start bit | 0x7E | Each command feedback starts with 0x7E |
| VER | Version | 0xFF | Version information (currently defaults to 0xFF) |
| Len | Length | Number | Number of bytes from VER to para2 (6 bytes) |
| CMD | Command word | varies | Indicates specific operations (play/pause, etc.) |
| Feedback | Command feedback | 0 or 1 | 1: Need feedback, 0: No feedback |
| para1 | Parameter 1 | varies | High byte of data (e.g., song number) |
| para2 | Parameter 2 | varies | Low byte of data |
| checksum | Checksum | 2 bytes | Accumulation and verification (see below) |
| $O | End bit | 0xEF | End bit 0xEF |

**Example**: To play from SPI-Flash, send: `7E FF 06 09 01 00 04 FE ED EF`
- Data length is 6 bytes: `FF 06 09 01 00 04`
- Checksum calculation: sum of all data in length bits, then invert the result +1

## 3.3 Communication Commands

### 3.3.1 Instructions with Return Code

| Command | Function | Description |
|---------|----------|-------------|
| 0x01 | Next track | Play next track |
| 0x02 | Previous track | Play previous track |
| 0x03 | Play at specified index | Two bytes, max 65535 tracks |
| 0x04 | Volume up | Maximum volume level 30 |
| 0x05 | Volume down | Minimum volume level 0 |
| 0x06 | Specify volume | 0-30 level volume adjustment |
| 0x07 | Specify EQ | 0/1/2/3/4/5 (Normal/Pop/Rock/Jazz/Classic/Bass) |
| 0x08 | Specify track index to loop | Two bytes, max 65535 tracks |
| 0x09 | Specify playback device | 1: U-disk; 2: TF Card; 4: Flash |
| 0x0A | Shut down | After shutdown, can be woken up via pin 2 (ADK2) |
| 0x0C | Reset | Restart the device |
| 0x0D | Play | Resume playback |
| 0x0E | Pause | Pause playback |
| 0x0F | Play file in specified folder | Folder name: 01-99 (FF for root); Files: 001-999.mp3 |
| 0x11 | Full loop | 1: Loop; 0: Stop looping |
| 0x12 | Play file in "MP3" folder | Files < 10000: 0001-9999.mp3; Files ≥ 10000: 10000-65535.mp3 |
| 0x13 | Play file in "ADVERT" folder | Files < 10000: 0001-9999.mp3; Files ≥ 10000: 10000-65535.mp3 |
| 0x14 | Play file in specified folder | Folder name: 01-15; Files: 0001-4095.mp3 |
| 0x15 | Stop interruption | Return to background file playback |
| 0x16 | Stop play | Stop playback |
| 0x17 | Loop folder | Folder name: 01-99 (for 99 folders) |
| 0x18 | Shuffle all | Play all tracks randomly |
| 0x19 | Set single loop | 0: Single cycle; 1: Cancel single loop |
| 0x1A | Mute control | 0: Unmute; 1: Mute |
| 0x25 | ADVERT folder insertion | Specify ADVERT1-9 folders (001-255.mp3 files) |

### 3.3.2 System Reply Parameters

| Command | Function | Parameter (16-bit) |
|---------|----------|-------------------|
| 0x3A | Device online information | 01: U-disk insert, 02: TF/SD insert, 04: PC insert |
| 0x3B | Device offline information | 01: U-disk removed, 02: TF/SD removed, 04: PC removed |
| 0x3C | U-disk playback end | Returns current song index |
| 0x3D | TF card playback end | Returns current song index |
| 0x3E | Flash playback end | Returns current song index |
| 0x40 | Error return | Request resend |
| 0x41 | Command reception response | - |

### Query Commands

| Command | Function | Parameter (16-bit) |
|---------|----------|-------------------|
| 0x42 | Query current status | 01: play; 02: pause; 03: stop |
| 0x43 | Query current volume | 00-30 |
| 0x44 | Query current EQ | 0/1/2/3/4/5 (Normal/Pop/Rock/Jazz/Classic/Bass) |
| 0x45 | Query current cycle mode | 01: Full cycle; 02: Single cycle; 03: Folder loop; 04: Random loop; 05: Play once; 06: Single seamless loop |
| 0x46 | Query software version | Return value in ASCII |
| 0x47 | Query U-DISK total files | - |
| 0x48 | Query TF card total files | - |
| 0x49 | Query FLASH total files | - |
| 0x4B | Query U-DISK current track | - |
| 0x4C | Query TF card current track | - |
| 0x4D | Query FLASH current track | - |
| 0x4E | Query total files in current folder | - |
| 0x4F | Query total folders | - |

## 3.4 Data Returned by the Chip

The chip returns data in key places for users to monitor status:
- Data indicating successful power-on initialization
- Data indicating the end of current track playback
- ACK data after successfully receiving a command
- Error data when receiving malformed commands
- Busy status when chip cannot accept commands
- Device insertion/removal notifications

### 3.4.1 Power-on Initialization Data

- Chip requires 1.5-3 seconds for initialization, depending on number of files
- Initialization data includes device status in bits 0-3 of the data byte:

| Data value | Bit3 (PC) | Bit2 (Flash) | Bit1 (SD) | Bit0 (U-disk) | Meaning |
|------------|-----------|--------------|-----------|---------------|---------|
| 0x01 | 0 | 0 | 0 | 1 | U-disk online |
| 0x02 | 0 | 0 | 1 | 0 | TF/SD Card online |
| 0x03 | 0 | 0 | 1 | 1 | U-disk, TF/SD Card online |
| 0x04 | 0 | 1 | 0 | 0 | Flash online |
| 0x05 | 0 | 1 | 0 | 1 | U-disk, Flash online |
| 0x06 | 0 | 1 | 1 | 0 | TF/SD Card, Flash online |
| 0x07 | 0 | 1 | 1 | 1 | U-disk, TF/SD Card, Flash online |

**Important**: You must wait for the chip initialization command before sending control commands.

### 3.4.2 End-of-Track Data

Examples:
- U-disk song 1 finished: `7E FF 06 3C 00 00 01 FE BE EF`
- U-disk song 2 finished: `7E FF 06 3C 00 00 02 FE BD EF`
- TF/SD song 3 finished: `7E FF 06 3D 00 00 03 FE BB EF`
- TF/SD song 4 finished: `7E FF 06 3D 00 00 04 FE BA EF`
- Flash song 5 finished: `7E FF 06 3E 00 00 05 FE B8 EF`
- Flash song 6 finished: `7E FF 06 3E 00 00 06 FE B7 EF`

Notes:
- Playback pause state outputs high level
- After specifying a device, wait 200ms before sending track commands

### 3.4.3 Error Return Data

| Message | Command | Meaning |
|---------|---------|---------|
| Return busy | `7E FF 06 40 00 00 01 xx xx EF` | Chip is initializing file system |
| Currently in sleep mode | `7E FF 06 40 00 00 02 xx xx EF` | Device is in sleep mode |
| Serial port receiving error | `7E FF 06 40 00 00 03 xx xx EF` | Incomplete data frame |
| Verification error | `7E FF 06 40 00 00 04 xx xx EF` | Checksum error |
| File out of range | `7E FF 06 40 00 00 05 xx xx EF` | Specified file exceeds range |
| File not found | `7E FF 06 40 00 00 06 xx xx EF` | Specified file not found |
| Insertion command error | `7E FF 06 40 00 00 07 xx xx EF` | Interruptions not accepted |

### 3.4.5 Device Insertion/Removal Messages

- U-Disk inserted: `7E FF 06 3A 00 00 01 xx xx EF`
- TF card inserted: `7E FF 06 3A 00 00 02 xx xx EF`
- TF card removed: `7E FF 06 3B 00 00 01 xx xx EF`
- PC removed: `7E FF 06 3B 00 00 02 xx xx EF`

## 3.5 Detailed Command Examples

### 3.5.1 Play Specific Track

Example: Play track 1
`7E FF 06 03 00 00 01 FF E6 EF`

- Start: 7E
- Version: FF
- Length: 06
- Command: 03 (Play track)
- Response needed: 00 (No)
- Track high byte: 00
- Track low byte: 01 (Track #1)
- Checksum high: FF
- Checksum low: E6
- End: EF

For track 100: DH=0x00, DL=0x64 (hex value of 100)
For track 1000: DH=0x03, DL=0xE8 (hex value of 1000)

### 3.5.2 Set Volume Command (0x06)

Example: Set volume to level 15
`7E FF 06 06 00 00 0F FF D5 EF`

### 3.5.3 Specify Playback Device (0x09)

- Specify U-disk: `7E FF 06 09 00 00 01 xx xx EF`
- Specify SD card: `7E FF 06 09 00 00 02 xx xx EF`

Note: Wait 200ms after this command before sending track commands. 