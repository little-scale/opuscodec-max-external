# OpusCodec~ - Real-time Opus Audio Codec for Max/MSP

A high-quality, low-latency Opus audio codec external for Max/MSP, providing real-time encode/decode processing for stereo audio streams.

## Features

- **Real-time Processing**: Sample-by-sample Opus encoding and decoding
- **Low Latency**: 20ms frame size with ring buffer for smooth output
- **High Quality**: Opus codec with configurable quality settings
- **Click-free**: Advanced ring buffer system eliminates frame boundary artifacts
- **Message-based Control**: Reliable real-time parameter adjustment
- **Stereo Processing**: Native stereo input/output support
- **Dynamic Sample Rate**: Auto-adapts to host sample rate (8/12/16/24/48kHz)

## Quick Start

```max
// Basic usage with default settings (32kbps, music mode)
opuscodec~

// Custom bitrate and complexity via arguments
opuscodec~ 64000 8

// Real-time control via messages
opuscodec~
|
bitrate 128000
complexity 5
mode voice
```

## Parameters

### Audio Quality
- **bitrate** (6000-510000): Target bitrate in bits per second
- **complexity** (0-10): Encoding complexity (0=fast, 10=best quality)
- **vbr** (0-2): Variable bitrate mode (0=CBR, 1=VBR, 2=CVBR)
- **mode** (voice/music): Signal type optimization

### Network/Transmission
- **loss** (0-100): Expected packet loss percentage
- **dtx** (0/1): Discontinuous transmission
- **fec** (0/1): Forward error correction

### Performance
- **framesize** (2.5,5,10,20,40,60): Frame size in milliseconds
- **bypass** (0/1): Bypass codec processing

## Default Settings (Production Ready)

- **Bitrate**: 32 kbps (good quality/compression balance)
- **Complexity**: 5 (balanced performance)
- **Mode**: Music (preserves frequency content)
- **Frame Size**: 20ms (standard quality)
- **VBR**: CBR (constant bitrate)
- **DTX**: Disabled (for reliability)

## Technical Details

### Architecture
- **Sample Rate**: Auto-adapts to host (supports 8, 12, 16, 24, 48 kHz)
- **Channels**: Stereo (2 channels)
- **Frame Processing**: 20ms frames (samples vary by rate)
- **Ring Buffer**: 4-frame circular buffer for smooth output
- **Latency**: ~20ms (one frame buffer delay)

### Ring Buffer System
Implements the same proven ring buffer architecture as the MP3 codec:
- Continuous frame processing in background
- Smooth sample delivery without gaps
- Eliminates clicking artifacts at frame boundaries
- Handles codec latency transparently

## Usage Examples

### Voice/Speech Processing
```max
opuscodec~ @bitrate 16000 @mode voice @complexity 0 @framesize 20
```

### High-Quality Music
```max
opuscodec~ @bitrate 128000 @mode music @complexity 8 @vbr 1
```

### Network Streaming (Resilient)
```max
opuscodec~ @bitrate 64000 @fec 1 @loss 5 @framesize 20
```

### Low-Latency Application
```max
opuscodec~ @bitrate 64000 @framesize 10 @complexity 3
```

## Message Commands

### Real-time Control
```max
bitrate 128000      // Change bitrate
complexity 8        // Change encoding complexity
mode music          // Switch to music mode
framesize 10        // Change frame size
bypass 1            // Enable bypass mode
reset               // Reset codec state
```

### Quality Presets
```max
// Telephone quality
bitrate 16000, mode voice, complexity 0

// Broadcast quality  
bitrate 64000, mode music, complexity 5

// Studio quality
bitrate 128000, mode music, complexity 8, vbr 1
```

## Development Notes

### Problem Solved: Frame Boundary Clicking
The original implementation suffered from clicking artifacts caused by silence gaps during frame collection. This was solved by implementing a ring buffer system inspired by the mp3codec~ external:

**Before (Clicking):**
```
Frame Collection: SILENCE → Frame Processing → AUDIO BURST → SILENCE
```

**After (Smooth):**
```
Frame Collection: Continuous → Ring Buffer → Continuous Output
```

### Key Technical Decisions
1. **Ring Buffer**: 4-frame circular buffer eliminates clicking
2. **20ms Frames**: Balance between latency and stability
3. **Music Signal**: Better default for general audio content
4. **CBR Mode**: More predictable than VBR for real-time use
5. **Attributes**: Full parameter control via Max attribute system

### Build Requirements
- **libopus 1.5.2**: Opus codec library (ARM64)
- **Max SDK**: Max/MSP external development kit
- **CMake**: Universal binary build system
- **macOS**: Code signing for security

## File Structure

```
opuscodec~/
├── README.md                 // This file
├── opuscodec~.c             // Main Max external
├── opus_codec_core.h        // Opus wrapper interface
├── opus_codec_core.c        // Opus codec implementation
├── CMakeLists.txt           // Build configuration
└── build/                   // Build directory
```

## Building

```bash
cd source/audio/opuscodec~
mkdir build && cd build
cmake -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" ..
cmake --build .
codesign --force --deep -s - ../../../externals/opuscodec~.mxo
```

## Performance

- **CPU Usage**: Low (optimized Opus implementation)
- **Memory**: ~50KB per instance (ring buffers)
- **Latency**: ~20ms (one frame + ring buffer)
- **Quality**: Transparent at 64kbps+ for music

## Troubleshooting

### No Audio Output
- Verify input signal levels
- Try `bypass 1` to test audio routing
- Check console for codec creation messages

### Clicking/Artifacts
- Increase frame size: `framesize 40`
- Lower complexity: `complexity 3`
- Check for buffer underruns

### Poor Quality
- Increase bitrate: `bitrate 128000`
- Use music mode: `mode music`
- Enable VBR: `vbr 1`

## License

This external uses the Opus codec library and follows Max SDK licensing terms.

## Credits

Developed with Claude Code assistance, incorporating proven techniques from the mp3codec~ external for robust real-time audio processing.