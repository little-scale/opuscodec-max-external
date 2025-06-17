#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "opus_codec_core.h"

// Max external object structure
typedef struct _opuscodec {
    t_pxobject ob;               // Max audio object
    t_opus_codec *codec;         // Opus codec instance
    double host_sample_rate;     // Host sample rate for compatibility
    
    // Attributes for real-time parameter control
    long bitrate;                // Current bitrate
    long complexity;             // Current complexity
    long vbr_mode;              // VBR mode (0=CBR, 1=VBR, 2=CVBR)
    t_symbol *signal_type;      // "voice" or "music"
    long packet_loss;           // Expected packet loss percentage
    long dtx;                   // DTX enable/disable
    long fec;                   // FEC enable/disable
    double framesize;           // Frame size in ms
    
    // Status
    long bypass;                // Bypass mode
    
} t_opuscodec;

// Class pointer
static t_class *opuscodec_class;

// Function prototypes
void *opuscodec_new(t_symbol *s, long argc, t_atom *argv);
void opuscodec_free(t_opuscodec *x);
void opuscodec_assist(t_opuscodec *x, void *b, long m, long a, char *s);

// DSP methods
void opuscodec_dsp64(t_opuscodec *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void opuscodec_perform64(t_opuscodec *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam);

// Message handlers
void opuscodec_bitrate(t_opuscodec *x, long bitrate);
void opuscodec_complexity(t_opuscodec *x, long complexity);
void opuscodec_vbr(t_opuscodec *x, long mode);
void opuscodec_mode(t_opuscodec *x, t_symbol *type);
void opuscodec_loss(t_opuscodec *x, long percentage);
void opuscodec_dtx(t_opuscodec *x, long enable);
void opuscodec_fec(t_opuscodec *x, long enable);
void opuscodec_framesize(t_opuscodec *x, double ms);
void opuscodec_bypass(t_opuscodec *x, long bypass);
void opuscodec_reset(t_opuscodec *x);

// No attribute setters needed - using message system

// Class registration
void ext_main(void *r) {
    // Create class
    t_class *c = class_new("opuscodec~", (method)opuscodec_new, (method)opuscodec_free, 
                          sizeof(t_opuscodec), NULL, A_GIMME, 0);
    
    // Add DSP methods
    class_addmethod(c, (method)opuscodec_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)opuscodec_assist, "assist", A_CANT, 0);
    
    // Message-based parameter control (no attributes - they're unreliable)
    class_addmethod(c, (method)opuscodec_bitrate, "bitrate", A_LONG, 0);
    class_addmethod(c, (method)opuscodec_complexity, "complexity", A_LONG, 0);
    class_addmethod(c, (method)opuscodec_vbr, "vbr", A_LONG, 0);
    class_addmethod(c, (method)opuscodec_mode, "mode", A_SYM, 0);
    class_addmethod(c, (method)opuscodec_loss, "loss", A_LONG, 0);
    class_addmethod(c, (method)opuscodec_dtx, "dtx", A_LONG, 0);
    class_addmethod(c, (method)opuscodec_fec, "fec", A_LONG, 0);
    class_addmethod(c, (method)opuscodec_framesize, "framesize", A_FLOAT, 0);
    class_addmethod(c, (method)opuscodec_bypass, "bypass", A_LONG, 0);
    class_addmethod(c, (method)opuscodec_reset, "reset", 0);
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    opuscodec_class = c;
    
    post("opuscodec~ - Real-time Opus audio codec for Max");
}

// Constructor
void *opuscodec_new(t_symbol *s, long argc, t_atom *argv) {
    t_opuscodec *x = (t_opuscodec *)object_alloc(opuscodec_class);
    
    if (x) {
        // Initialize DSP with 2 inlets and 2 outlets (stereo)
        dsp_setup((t_pxobject *)x, 2);
        outlet_new(x, "signal");
        outlet_new(x, "signal");
        
        // Initialize default values - lowest quality/maximum compression
        x->bitrate = 32000;      // Reasonable quality/compression balance
        x->complexity = 5;       // Balanced complexity for better quality
        x->vbr_mode = 0;         // CBR for most predictable compression
        x->signal_type = gensym("music");  // Music mode for better audio quality
        x->packet_loss = 0;
        x->dtx = 0;              // DTX disabled for reliability
        x->fec = 0;
        x->framesize = 20.0;     // 20ms frames for standard quality
        x->bypass = 0;
        
        // Process positional arguments (no attributes)
        if (argc > 0 && atom_gettype(argv) == A_LONG) {
            x->bitrate = atom_getlong(argv);
        }
        if (argc > 1 && atom_gettype(argv + 1) == A_LONG) {
            x->complexity = atom_getlong(argv + 1);
        }
        
        // Codec will be created in dsp64 method when sample rate is known
        x->codec = NULL;
        x->host_sample_rate = 48000.0;  // Default assumption
        
        post("opuscodec~: External created - codec will initialize on DSP start");
        post("opuscodec~: Default settings - bitrate=%ld kbps, complexity=%ld, framesize=%.1fms, mode=%s", 
             x->bitrate/1000, x->complexity, x->framesize, x->signal_type->s_name);
        
        x->ob.z_misc = Z_NO_INPLACE;
    }
    
    return x;
}

// Destructor
void opuscodec_free(t_opuscodec *x) {
    dsp_free((t_pxobject *)x);
    if (x->codec) {
        opus_codec_destroy(x->codec);
    }
}

// Help/assist
void opuscodec_assist(t_opuscodec *x, void *b, long m, long a, char *s) {
    if (m == ASSIST_INLET) {
        switch (a) {
            case 0: strcpy(s, "(signal) Left Input"); break;
            case 1: strcpy(s, "(signal) Right Input"); break;
        }
    } else {
        switch (a) {
            case 0: strcpy(s, "(signal) Left Output"); break;
            case 1: strcpy(s, "(signal) Right Output"); break;
        }
    }
}

// DSP setup
void opuscodec_dsp64(t_opuscodec *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags) {
    // Store host sample rate
    x->host_sample_rate = samplerate;
    
    // Recreate codec if sample rate changed
    if (x->codec) {
        opus_codec_destroy(x->codec);
    }
    
    // Create codec with host sample rate
    x->codec = opus_codec_create((int)samplerate);
    if (!x->codec) {
        object_error((t_object *)x, "Failed to create Opus codec for sample rate %.0f Hz", samplerate);
        return;
    }
    
    // Apply all attribute values to new codec instance
    opus_codec_set_bitrate(x->codec, x->bitrate);
    opus_codec_set_complexity(x->codec, x->complexity);
    opus_codec_set_vbr_mode(x->codec, x->vbr_mode);
    opus_codec_set_frame_size_ms(x->codec, (float)x->framesize);
    opus_codec_set_dtx(x->codec, x->dtx);
    opus_codec_set_fec(x->codec, x->fec);
    opus_codec_set_packet_loss(x->codec, x->packet_loss);
    
    // Set signal type
    int sig_type = (x->signal_type == gensym("voice")) ? OPUS_SIGNAL_VOICE : OPUS_SIGNAL_MUSIC;
    opus_codec_set_signal_type(x->codec, sig_type);
    
    post("opuscodec~: Codec created for %.0f Hz sample rate", samplerate);
    post("opuscodec~: Applied attributes - bitrate=%ld, complexity=%ld, mode=%s", 
         x->bitrate, x->complexity, x->signal_type ? x->signal_type->s_name : "music");
    
    object_method(dsp64, gensym("dsp_add64"), x, opuscodec_perform64, 0, NULL);
}

// Audio processing perform routine
void opuscodec_perform64(t_opuscodec *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam) {
    double *in_left = ins[0];
    double *in_right = ins[1];
    double *out_left = outs[0];
    double *out_right = outs[1];
    
    if (x->bypass || !x->codec) {
        // Bypass mode - just copy input to output
        for (long i = 0; i < sampleframes; i++) {
            out_left[i] = in_left[i];
            out_right[i] = in_right[i];
        }
        return;
    }
    
    // Process each sample through Opus codec
    for (long i = 0; i < sampleframes; i++) {
        float opus_out_left, opus_out_right;
        
        int result = opus_codec_process_sample(x->codec, 
                                              (float)in_left[i], 
                                              (float)in_right[i],
                                              &opus_out_left, 
                                              &opus_out_right);
        
        if (result == OPUS_CODEC_OK) {
            out_left[i] = (double)opus_out_left;
            out_right[i] = (double)opus_out_right;
        } else {
            // Error - output silence
            out_left[i] = 0.0;
            out_right[i] = 0.0;
        }
        
        // Clean processing - no debug output
    }
}

// Message handlers
void opuscodec_bitrate(t_opuscodec *x, long bitrate) {
    if (bitrate >= 6000 && bitrate <= 510000) {
        x->bitrate = bitrate;
        if (x->codec) {
            opus_codec_set_bitrate(x->codec, bitrate);
        }
        post("opuscodec~: Bitrate set to %ld bps (%.1f kbps)", bitrate, bitrate/1000.0);
    } else {
        object_error((t_object *)x, "Bitrate must be between 6000 and 510000 bps");
    }
}

void opuscodec_complexity(t_opuscodec *x, long complexity) {
    if (complexity >= 0 && complexity <= 10) {
        x->complexity = complexity;
        if (x->codec) {
            opus_codec_set_complexity(x->codec, complexity);
        }
        post("opuscodec~: Complexity set to %ld", complexity);
    } else {
        object_error((t_object *)x, "Complexity must be between 0 and 10");
    }
}

void opuscodec_vbr(t_opuscodec *x, long mode) {
    if (mode >= 0 && mode <= 2) {
        x->vbr_mode = mode;
        if (x->codec) {
            opus_codec_set_vbr_mode(x->codec, mode);
        }
        const char* mode_names[] = {"CBR", "VBR", "CVBR"};
        post("opuscodec~: VBR mode set to %ld (%s)", mode, mode_names[mode]);
    } else {
        object_error((t_object *)x, "VBR mode must be 0 (CBR), 1 (VBR), or 2 (CVBR)");
    }
}

void opuscodec_mode(t_opuscodec *x, t_symbol *type) {
    if (type == gensym("voice") || type == gensym("music")) {
        x->signal_type = type;
        if (x->codec) {
            int sig_type = (type == gensym("voice")) ? OPUS_SIGNAL_VOICE : OPUS_SIGNAL_MUSIC;
            opus_codec_set_signal_type(x->codec, sig_type);
        }
        post("opuscodec~: Signal mode set to %s", type->s_name);
    } else {
        object_error((t_object *)x, "Signal mode must be 'voice' or 'music'");
    }
}

void opuscodec_loss(t_opuscodec *x, long percentage) {
    if (percentage >= 0 && percentage <= 100) {
        x->packet_loss = percentage;
        if (x->codec) {
            opus_codec_set_packet_loss(x->codec, percentage);
        }
        post("opuscodec~: Expected packet loss set to %ld%%", percentage);
    } else {
        object_error((t_object *)x, "Packet loss must be between 0 and 100 percent");
    }
}

void opuscodec_dtx(t_opuscodec *x, long enable) {
    x->dtx = enable ? 1 : 0;
    if (x->codec) {
        opus_codec_set_dtx(x->codec, x->dtx);
    }
    post("opuscodec~: DTX (discontinuous transmission) %s", x->dtx ? "enabled" : "disabled");
}

void opuscodec_fec(t_opuscodec *x, long enable) {
    x->fec = enable ? 1 : 0;
    if (x->codec) {
        opus_codec_set_fec(x->codec, x->fec);
    }
    post("opuscodec~: FEC (forward error correction) %s", x->fec ? "enabled" : "disabled");
}

void opuscodec_framesize(t_opuscodec *x, double ms) {
    // Valid frame sizes: 2.5, 5, 10, 20, 40, 60 ms
    if (ms == 2.5 || ms == 5.0 || ms == 10.0 || ms == 20.0 || ms == 40.0 || ms == 60.0) {
        x->framesize = ms;
        if (x->codec) {
            opus_codec_set_frame_size_ms(x->codec, (float)ms);
        }
        post("opuscodec~: Frame size set to %.1f ms", ms);
    } else {
        object_error((t_object *)x, "Frame size must be 2.5, 5, 10, 20, 40, or 60 ms");
    }
}

void opuscodec_bypass(t_opuscodec *x, long bypass) {
    x->bypass = bypass ? 1 : 0;
    if (x->bypass) {
        post("opuscodec~: Bypass enabled");
    } else {
        post("opuscodec~: Bypass disabled");
    }
}

void opuscodec_reset(t_opuscodec *x) {
    if (x->codec) {
        opus_codec_reset(x->codec);
        post("opuscodec~: Codec reset");
    }
}

// Attributes abandoned - using proven message system only