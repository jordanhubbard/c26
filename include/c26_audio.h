#ifndef C26_AUDIO_H
#define C26_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define C26_AUDIO_SAMPLE_RATE 48000U
#define C26_AUDIO_VOICE_COUNT 8U

typedef enum {
    C26_WAVE_SQUARE = 0,
    C26_WAVE_SAW = 1,
    C26_WAVE_TRIANGLE = 2,
    C26_WAVE_NOISE = 3,
} c26_waveform_t;

void c26_audio_mixer_init(void);
int c26_audio_voice_start(unsigned int voice, c26_waveform_t waveform,
                          uint32_t frequency_hz, uint8_t volume, uint8_t pan);
void c26_audio_voice_stop(unsigned int voice);
void c26_audio_render(int16_t *stereo_samples, size_t frames);
int c26_audio_backend_online(void);
void c26_audio_poll(void);

#endif
