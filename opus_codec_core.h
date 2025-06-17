#ifndef OPUS_CODEC_CORE_H
#define OPUS_CODEC_CORE_H

#include <opus.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

// Configuration constants
#define OPUS_DEFAULT_SAMPLE_RATE 48000
#define OPUS_FRAME_SIZE_MS 20.0  // 20ms frames for standard quality
#define OPUS_MAX_FRAME_SIZE (48000 * 60 / 1000)  // 60ms max at 48kHz for buffer allocation
#define OPUS_MAX_PACKET_SIZE 4000
#define OPUS_CHANNELS 2

// Error codes
#define OPUS_CODEC_OK 0
#define OPUS_CODEC_ERROR -1

// Opus codec state structure
typedef struct _opus_codec {
    OpusEncoder *encoder;
    OpusDecoder *decoder;
    
    // Configuration parameters
    int sample_rate;        // Sample rate (8000, 12000, 16000, 24000, 48000)
    int bitrate;            // Target bitrate (6000 to 510000)
    int complexity;         // Complexity (0-10)
    int vbr_mode;          // 0=CBR, 1=VBR, 2=CVBR
    int signal_type;       // OPUS_SIGNAL_VOICE or OPUS_SIGNAL_MUSIC
    int application;       // OPUS_APPLICATION_AUDIO, _VOIP, or _RESTRICTED_LOWDELAY
    int packet_loss_perc;  // Expected packet loss percentage
    int use_dtx;           // Discontinuous transmission
    int use_fec;           // Forward error correction
    
    // Buffers
    float *input_buffer_left;
    float *input_buffer_right;
    float *output_buffer_left;
    float *output_buffer_right;
    float *interleaved_input;
    float *interleaved_output;
    unsigned char *opus_packet;
    
    // Frame management
    int buffer_pos;
    int frame_size;
    int output_pos;        // Position in output buffer for sample delivery
    int output_available;  // Number of samples available in output buffer
    
    // Silence detection
    float silence_threshold;
    int silent_frames_count;
    
    // Ring buffer for smooth output delivery (like MP3 codec)
    float *output_ring_left;
    float *output_ring_right;
    int ring_write_pos;
    int ring_read_pos;
    int ring_size;
    
} t_opus_codec;

// Function prototypes
t_opus_codec* opus_codec_create(int sample_rate);
void opus_codec_destroy(t_opus_codec *codec);
int opus_codec_process_sample(t_opus_codec *codec, float in_left, float in_right, 
                              float *out_left, float *out_right);
int opus_codec_set_bitrate(t_opus_codec *codec, int bitrate);
int opus_codec_set_complexity(t_opus_codec *codec, int complexity);
int opus_codec_set_vbr_mode(t_opus_codec *codec, int mode);
int opus_codec_set_signal_type(t_opus_codec *codec, int type);
int opus_codec_set_packet_loss(t_opus_codec *codec, int percentage);
int opus_codec_set_dtx(t_opus_codec *codec, int enable);
int opus_codec_set_fec(t_opus_codec *codec, int enable);
int opus_codec_reset(t_opus_codec *codec);
int opus_codec_set_frame_size_ms(t_opus_codec *codec, float ms);
int opus_codec_get_latency(t_opus_codec *codec);

#endif