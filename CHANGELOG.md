# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] - 2024-04-04

### Added
- Cross-platform compatibility for Arduino, ESP8266, and ESP32
- Support for both HardwareSerial and SoftwareSerial interfaces
- Detailed debug logging capability (enabled via DFPLAYER_DEBUG)
- ESP-specific optimizations to prevent watchdog timer resets
- GitHub Actions CI workflow for automated testing

### Changed
- Complete rewrite of the serial communication handling
- Implemented robust state machine for packet parsing
- Eliminated blocking delays, using yield() and short waits
- Optimized checksum validation and error handling
- Improved API documentation

### Fixed
- Resolved frequent ESP8266/ESP32 crashes due to serial buffer overflow
- Fixed watchdog compatibility issues, preventing unexpected resets
- Eliminated buffer overflow issues for long-running applications

## [1.0.6] - Prior Version
- Original DFRobot implementation 