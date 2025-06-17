#include "opus_codec_core.h"

// Helper function to get closest supported Opus sample rate
static int get_opus_sample_rate(int host_rate) {
    // Opus supports: 8000, 12000, 16000, 24000, 48000 Hz
    if (host_rate <= 8000) return 8000;
    if (host_rate <= 12000) return 12000;
    if (host_rate <= 16000) return 16000;
    if (host_rate <= 24000) return 24000;
    return 48000;  // Default to 48kHz for rates above 24kHz
}

t_opus_codec* opus_codec_create(int host_sample_rate) {
    t_opus_codec *codec = (t_opus_codec*)calloc(1, sizeof(t_opus_codec));
    if (!codec) return NULL;
    
    int error;
    
    // Set sample rate to closest supported Opus rate
    codec->sample_rate = get_opus_sample_rate(host_sample_rate);
    
    // Initialize encoder with determined sample rate
    codec->encoder = opus_encoder_create(codec->sample_rate, OPUS_CHANNELS, 
                                         OPUS_APPLICATION_AUDIO, &error);
    if (error != OPUS_OK || !codec->encoder) {
        free(codec);
        return NULL;
    }
    
    // Initialize decoder
    codec->decoder = opus_decoder_create(codec->sample_rate, OPUS_CHANNELS, &error);
    if (error != OPUS_OK || !codec->decoder) {
        opus_encoder_destroy(codec->encoder);
        free(codec);
        return NULL;
    }
    
    // Set default parameters
    codec->bitrate = 6000;   // 6 kbps - minimum for maximum compression
    codec->complexity = 0;   // Lowest complexity for fastest/lowest quality
    codec->vbr_mode = 0;     // CBR for most predictable compression
    codec->signal_type = OPUS_SIGNAL_MUSIC;
    codec->application = OPUS_APPLICATION_AUDIO;
    codec->packet_loss_perc = 0;
    codec->use_dtx = 0;      // Disable DTX by default
    codec->use_fec = 0;
    codec->frame_size = (int)(codec->sample_rate * OPUS_FRAME_SIZE_MS / 1000.0); // 20ms default
    
    // Apply default settings to encoder
    opus_encoder_ctl(codec->encoder, OPUS_SET_BITRATE(codec->bitrate));
    opus_encoder_ctl(codec->encoder, OPUS_SET_COMPLEXITY(codec->complexity));
    opus_encoder_ctl(codec->encoder, OPUS_SET_VBR(codec->vbr_mode));
    opus_encoder_ctl(codec->encoder, OPUS_SET_SIGNAL(codec->signal_type));
    opus_encoder_ctl(codec->encoder, OPUS_SET_DTX(codec->use_dtx));
    
    // Initialize silence detection
    codec->silence_threshold = 0.001f; // -60dB threshold
    codec->silent_frames_count = 0;
    
    // Initialize ring buffer (4 frames worth, like MP3 codec)
    codec->ring_size = codec->frame_size * 4;  // 4 frames for smooth output
    codec->ring_write_pos = 0;
    codec->ring_read_pos = 0;
    
    // Allocate buffers (use max frame size for dynamic frame size support)
    codec->input_buffer_left = (float*)calloc(OPUS_MAX_FRAME_SIZE, sizeof(float));
    codec->input_buffer_right = (float*)calloc(OPUS_MAX_FRAME_SIZE, sizeof(float));
    codec->output_buffer_left = (float*)calloc(OPUS_MAX_FRAME_SIZE, sizeof(float));
    codec->output_buffer_right = (float*)calloc(OPUS_MAX_FRAME_SIZE, sizeof(float));
    codec->interleaved_input = (float*)calloc(OPUS_MAX_FRAME_SIZE * OPUS_CHANNELS, sizeof(float));
    codec->interleaved_output = (float*)calloc(OPUS_MAX_FRAME_SIZE * OPUS_CHANNELS, sizeof(float));
    codec->opus_packet = (unsigned char*)calloc(OPUS_MAX_PACKET_SIZE, sizeof(unsigned char));
    
    // Allocate ring buffer
    codec->output_ring_left = (float*)calloc(codec->ring_size, sizeof(float));
    codec->output_ring_right = (float*)calloc(codec->ring_size, sizeof(float));
    
    // Check buffer allocation
    if (!codec->input_buffer_left || !codec->input_buffer_right ||
        !codec->output_buffer_left || !codec->output_buffer_right ||
        !codec->interleaved_input || !codec->interleaved_output ||
        !codec->opus_packet || !codec->output_ring_left || !codec->output_ring_right) {
        opus_codec_destroy(codec);
        return NULL;
    }
    
    codec->buffer_pos = 0;
    codec->output_pos = 0;
    codec->output_available = 0;
    
    return codec;
}

void opus_codec_destroy(t_opus_codec *codec) {
    if (!codec) return;
    
    if (codec->encoder) opus_encoder_destroy(codec->encoder);
    if (codec->decoder) opus_decoder_destroy(codec->decoder);
    
    free(codec->input_buffer_left);
    free(codec->input_buffer_right);
    free(codec->output_buffer_left);
    free(codec->output_buffer_right);
    free(codec->interleaved_input);
    free(codec->interleaved_output);
    free(codec->opus_packet);
    free(codec->output_ring_left);
    free(codec->output_ring_right);
    
    free(codec);
}

int opus_codec_process_sample(t_opus_codec *codec, float in_left, float in_right,
                              float *out_left, float *out_right) {
    if (!codec || !out_left || !out_right) return OPUS_CODEC_ERROR;
    
    // Add input to frame buffer
    codec->input_buffer_left[codec->buffer_pos] = in_left;
    codec->input_buffer_right[codec->buffer_pos] = in_right;
    codec->buffer_pos++;
    
    // When we have a full frame, process it
    if (codec->buffer_pos >= codec->frame_size) {
        codec->buffer_pos = 0;
        
        // Interleave samples for Opus
        for (int i = 0; i < codec->frame_size; i++) {
            codec->interleaved_input[i * 2] = codec->input_buffer_left[i];
            codec->interleaved_input[i * 2 + 1] = codec->input_buffer_right[i];
        }
        
        // Encode the frame
        int packet_size = opus_encode_float(codec->encoder, 
                                            codec->interleaved_input,
                                            codec->frame_size,
                                            codec->opus_packet,
                                            OPUS_MAX_PACKET_SIZE);
        
        if (packet_size > 0) {
            // Decode the packet immediately
            int decoded_samples = opus_decode_float(codec->decoder,
                                                    codec->opus_packet,
                                                    packet_size,
                                                    codec->interleaved_output,
                                                    codec->frame_size,
                                                    0);
            
            if (decoded_samples > 0) {
                // Add decoded samples to ring buffer
                for (int i = 0; i < decoded_samples; i++) {
                    codec->output_ring_left[codec->ring_write_pos] = codec->interleaved_output[i * 2];
                    codec->output_ring_right[codec->ring_write_pos] = codec->interleaved_output[i * 2 + 1];
                    codec->ring_write_pos++;
                    if (codec->ring_write_pos >= codec->ring_size) {
                        codec->ring_write_pos = 0;
                    }
                }
            }
        }
    }
    
    // Output from ring buffer (like MP3 codec)
    int available;
    if (codec->ring_write_pos >= codec->ring_read_pos) {
        available = codec->ring_write_pos - codec->ring_read_pos;
    } else {
        available = (codec->ring_size - codec->ring_read_pos) + codec->ring_write_pos;
    }
    
    // Only output when we have enough samples (prevents clicking)
    if (available > codec->frame_size) {
        *out_left = codec->output_ring_left[codec->ring_read_pos];
        *out_right = codec->output_ring_right[codec->ring_read_pos];
        codec->ring_read_pos++;
        if (codec->ring_read_pos >= codec->ring_size) {
            codec->ring_read_pos = 0;
        }
    } else {
        // Not enough samples - output silence (but this should be rare now)
        *out_left = 0.0f;
        *out_right = 0.0f;
    }
    
    return OPUS_CODEC_OK;
}

// Parameter setters
int opus_codec_set_bitrate(t_opus_codec *codec, int bitrate) {
    if (!codec || bitrate < 6000 || bitrate > 510000) return OPUS_CODEC_ERROR;
    
    codec->bitrate = bitrate;
    return opus_encoder_ctl(codec->encoder, OPUS_SET_BITRATE(bitrate)) == OPUS_OK ? 
           OPUS_CODEC_OK : OPUS_CODEC_ERROR;
}

int opus_codec_set_complexity(t_opus_codec *codec, int complexity) {
    if (!codec || complexity < 0 || complexity > 10) return OPUS_CODEC_ERROR;
    
    codec->complexity = complexity;
    return opus_encoder_ctl(codec->encoder, OPUS_SET_COMPLEXITY(complexity)) == OPUS_OK ?
           OPUS_CODEC_OK : OPUS_CODEC_ERROR;
}

int opus_codec_set_vbr_mode(t_opus_codec *codec, int mode) {
    if (!codec || mode < 0 || mode > 2) return OPUS_CODEC_ERROR;
    
    codec->vbr_mode = mode;
    int result;
    
    switch (mode) {
        case 0:  // CBR
            result = opus_encoder_ctl(codec->encoder, OPUS_SET_VBR(0));
            break;
        case 1:  // VBR
            result = opus_encoder_ctl(codec->encoder, OPUS_SET_VBR(1));
            if (result == OPUS_OK) {
                result = opus_encoder_ctl(codec->encoder, OPUS_SET_VBR_CONSTRAINT(0));
            }
            break;
        case 2:  // CVBR
            result = opus_encoder_ctl(codec->encoder, OPUS_SET_VBR(1));
            if (result == OPUS_OK) {
                result = opus_encoder_ctl(codec->encoder, OPUS_SET_VBR_CONSTRAINT(1));
            }
            break;
        default:
            return OPUS_CODEC_ERROR;
    }
    
    return result == OPUS_OK ? OPUS_CODEC_OK : OPUS_CODEC_ERROR;
}

int opus_codec_set_signal_type(t_opus_codec *codec, int type) {
    if (!codec || (type != OPUS_SIGNAL_VOICE && type != OPUS_SIGNAL_MUSIC)) {
        return OPUS_CODEC_ERROR;
    }
    
    codec->signal_type = type;
    return opus_encoder_ctl(codec->encoder, OPUS_SET_SIGNAL(type)) == OPUS_OK ?
           OPUS_CODEC_OK : OPUS_CODEC_ERROR;
}

int opus_codec_set_packet_loss(t_opus_codec *codec, int percentage) {
    if (!codec || percentage < 0 || percentage > 100) return OPUS_CODEC_ERROR;
    
    codec->packet_loss_perc = percentage;
    return opus_encoder_ctl(codec->encoder, OPUS_SET_PACKET_LOSS_PERC(percentage)) == OPUS_OK ?
           OPUS_CODEC_OK : OPUS_CODEC_ERROR;
}

int opus_codec_set_dtx(t_opus_codec *codec, int enable) {
    if (!codec) return OPUS_CODEC_ERROR;
    
    codec->use_dtx = enable ? 1 : 0;
    return opus_encoder_ctl(codec->encoder, OPUS_SET_DTX(codec->use_dtx)) == OPUS_OK ?
           OPUS_CODEC_OK : OPUS_CODEC_ERROR;
}

int opus_codec_set_fec(t_opus_codec *codec, int enable) {
    if (!codec) return OPUS_CODEC_ERROR;
    
    codec->use_fec = enable ? 1 : 0;
    return opus_encoder_ctl(codec->encoder, OPUS_SET_INBAND_FEC(codec->use_fec)) == OPUS_OK ?
           OPUS_CODEC_OK : OPUS_CODEC_ERROR;
}

int opus_codec_reset(t_opus_codec *codec) {
    if (!codec) return OPUS_CODEC_ERROR;
    
    // Reset encoder and decoder states
    int enc_result = opus_encoder_ctl(codec->encoder, OPUS_RESET_STATE);
    int dec_result = opus_decoder_ctl(codec->decoder, OPUS_RESET_STATE);
    
    // Clear buffers
    memset(codec->input_buffer_left, 0, OPUS_MAX_FRAME_SIZE * sizeof(float));
    memset(codec->input_buffer_right, 0, OPUS_MAX_FRAME_SIZE * sizeof(float));
    memset(codec->output_buffer_left, 0, OPUS_MAX_FRAME_SIZE * sizeof(float));
    memset(codec->output_buffer_right, 0, OPUS_MAX_FRAME_SIZE * sizeof(float));
    memset(codec->interleaved_input, 0, OPUS_MAX_FRAME_SIZE * OPUS_CHANNELS * sizeof(float));
    memset(codec->interleaved_output, 0, OPUS_MAX_FRAME_SIZE * OPUS_CHANNELS * sizeof(float));
    
    codec->buffer_pos = 0;
    codec->output_pos = 0;
    codec->output_available = 0;
    
    return (enc_result == OPUS_OK && dec_result == OPUS_OK) ? 
           OPUS_CODEC_OK : OPUS_CODEC_ERROR;
}

int opus_codec_get_latency(t_opus_codec *codec) {
    if (!codec) return -1;
    
    opus_int32 lookahead;
    opus_encoder_ctl(codec->encoder, OPUS_GET_LOOKAHEAD(&lookahead));
    
    // Total latency = encoder lookahead + frame size + decoder delay
    // Decoder has a fixed delay of 6.5ms (scaled by sample rate)
    int decoder_delay = (int)(codec->sample_rate * 6.5 / 1000.0);
    return lookahead + codec->frame_size + decoder_delay;
}

// Frame size configuration (must be called when no audio is being processed)
int opus_codec_set_frame_size_ms(t_opus_codec *codec, float ms) {
    if (!codec) return OPUS_CODEC_ERROR;
    
    // Valid frame sizes: 2.5, 5, 10, 20, 40, 60 ms
    // Calculate samples based on current sample rate
    int samples;
    if (ms == 2.5f) samples = (int)(codec->sample_rate * 2.5 / 1000.0);
    else if (ms == 5.0f) samples = (int)(codec->sample_rate * 5.0 / 1000.0);
    else if (ms == 10.0f) samples = (int)(codec->sample_rate * 10.0 / 1000.0);
    else if (ms == 20.0f) samples = (int)(codec->sample_rate * 20.0 / 1000.0);
    else if (ms == 40.0f) samples = (int)(codec->sample_rate * 40.0 / 1000.0);
    else if (ms == 60.0f) samples = (int)(codec->sample_rate * 60.0 / 1000.0);
    else return OPUS_CODEC_ERROR;
    
    codec->frame_size = samples;
    codec->buffer_pos = 0;  // Reset buffer position
    codec->output_pos = 0;
    codec->output_available = 0;
    
    return OPUS_CODEC_OK;
}