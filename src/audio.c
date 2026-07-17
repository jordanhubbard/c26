#include "c26.h"
#include "c26_audio.h"
#include "c26_virtio.h"

#define VIRTIO_DEVICE_SOUND 25U
#define SOUND_PERIOD_FRAMES 256U
#define SOUND_PERIOD_COUNT 4U
#define SOUND_PERIOD_BYTES (SOUND_PERIOD_FRAMES * 2U * sizeof(int16_t))

#define SND_R_PCM_INFO 0x0100U
#define SND_R_PCM_SET_PARAMS 0x0101U
#define SND_R_PCM_PREPARE 0x0102U
#define SND_R_PCM_START 0x0104U
#define SND_S_OK 0x8000U
#define SND_DIRECTION_OUTPUT 0U
#define SND_FORMAT_S16 5U
#define SND_RATE_48000 7U

typedef struct {
    uint32_t phase;
    uint32_t step;
    uint32_t noise;
    uint16_t envelope;
    uint8_t volume;
    uint8_t pan;
    uint8_t waveform;
    uint8_t active;
} audio_voice_t;

typedef struct {
    uint32_t code;
} snd_header_t;

typedef struct {
    uint32_t code;
    uint32_t stream_id;
} snd_pcm_header_t;

typedef struct {
    uint32_t code;
    uint32_t start_id;
    uint32_t count;
    uint32_t size;
} snd_query_info_t;

typedef struct {
    uint32_t hda_fn_nid;
    uint32_t features;
    uint64_t formats;
    uint64_t rates;
    uint8_t direction;
    uint8_t channels_min;
    uint8_t channels_max;
    uint8_t padding[5];
} snd_pcm_info_t;

typedef struct {
    snd_pcm_header_t header;
    uint32_t buffer_bytes;
    uint32_t period_bytes;
    uint32_t features;
    uint8_t channels;
    uint8_t format;
    uint8_t rate;
    uint8_t padding;
} snd_set_params_t;

typedef struct {
    uint32_t stream_id;
} snd_pcm_transfer_t;

typedef struct {
    uint32_t status;
    uint32_t latency_bytes;
} snd_pcm_status_t;

typedef struct {
    uint32_t code;
    uint32_t data;
} snd_event_t;

typedef struct {
    c26_virtq_desc_t descriptors[C26_VIRTQ_SIZE];
    c26_virtq_avail_t available;
    c26_virtq_used_t used;
} sound_queue_memory_t;

static audio_voice_t voices[C26_AUDIO_VOICE_COUNT];
static c26_virtio_device_t sound_device;
static c26_virtq_t control_queue;
static c26_virtq_t event_queue;
static c26_virtq_t transmit_queue;
static c26_virtq_t receive_queue;
static sound_queue_memory_t control_memory __attribute__((aligned(4096)));
static sound_queue_memory_t event_memory __attribute__((aligned(4096)));
static sound_queue_memory_t transmit_memory __attribute__((aligned(4096)));
static sound_queue_memory_t receive_memory __attribute__((aligned(4096)));
static snd_event_t sound_events[C26_VIRTQ_SIZE];
static snd_pcm_transfer_t transfers[SOUND_PERIOD_COUNT];
static snd_pcm_status_t period_status[SOUND_PERIOD_COUNT];
static int16_t periods[SOUND_PERIOD_COUNT][SOUND_PERIOD_FRAMES * 2]
    __attribute__((aligned(64)));
static uint32_t selected_stream;
static int backend_online;

void c26_audio_mixer_init(void)
{
    for (unsigned int i = 0; i < C26_AUDIO_VOICE_COUNT; i++) {
        voices[i] = (audio_voice_t){0, 0, 0x13579bdfU ^ i, 0, 0, 128, 0, 0};
    }
}

int c26_audio_voice_start(unsigned int voice, c26_waveform_t waveform,
                          uint32_t frequency_hz, uint8_t volume, uint8_t pan)
{
    if (voice >= C26_AUDIO_VOICE_COUNT || frequency_hz == 0 ||
        frequency_hz > C26_AUDIO_SAMPLE_RATE / 2) {
        return 0;
    }
    voices[voice].phase = 0;
    voices[voice].step = (uint32_t)(((uint64_t)frequency_hz << 32) /
                                   C26_AUDIO_SAMPLE_RATE);
    voices[voice].noise = 0x9e3779b9U ^ (voice * 0x10203U);
    voices[voice].envelope = 0;
    voices[voice].volume = volume;
    voices[voice].pan = pan;
    voices[voice].waveform = (uint8_t)waveform;
    voices[voice].active = 1;
    return 1;
}

void c26_audio_voice_stop(unsigned int voice)
{
    if (voice < C26_AUDIO_VOICE_COUNT) {
        voices[voice].active = 0;
    }
}

static int32_t voice_sample(audio_voice_t *voice)
{
    uint32_t phase = voice->phase;
    voice->phase += voice->step;
    switch (voice->waveform) {
    case C26_WAVE_SQUARE:
        return (phase & 0x80000000U) != 0 ? 32767 : -32768;
    case C26_WAVE_SAW:
        return (int32_t)(phase >> 16) - 32768;
    case C26_WAVE_TRIANGLE: {
        uint32_t value = phase >> 15;
        if (value > 65535) value = 131071 - value;
        return (int32_t)value - 32768;
    }
    default:
        voice->noise ^= voice->noise << 13;
        voice->noise ^= voice->noise >> 17;
        voice->noise ^= voice->noise << 5;
        return (int16_t)(voice->noise >> 16);
    }
}

static int16_t clamp_sample(int64_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

void c26_audio_render(int16_t *stereo_samples, size_t frames)
{
    for (size_t frame = 0; frame < frames; frame++) {
        int64_t left = 0;
        int64_t right = 0;
        for (unsigned int i = 0; i < C26_AUDIO_VOICE_COUNT; i++) {
            audio_voice_t *voice = &voices[i];
            if (!voice->active) continue;
            if (voice->envelope < 64511) voice->envelope += 1024;
            else voice->envelope = 65535;
            int64_t sample = voice_sample(voice);
            sample = sample * voice->volume * voice->envelope /
                     (255LL * 65535LL);
            left += sample * (255 - voice->pan) / 255;
            right += sample * voice->pan / 255;
        }
        stereo_samples[frame * 2] = clamp_sample(left);
        stereo_samples[frame * 2 + 1] = clamp_sample(right);
    }
}

static int wait_for_queue(c26_virtq_t *queue, uint32_t expected)
{
    uint32_t id;
    for (uint32_t spin = 0; spin < 50000000U; spin++) {
        if (c26_virtq_pop(queue, &id, 0)) {
            c26_virtio_ack_interrupt(&sound_device);
            return id == expected;
        }
    }
    return 0;
}

static int control_command(void *request, uint32_t request_length)
{
    static uint32_t response;
    response = 0;
    control_memory.descriptors[0] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)request, request_length, C26_VIRTQ_DESC_NEXT, 1};
    control_memory.descriptors[1] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)&response, sizeof(response),
        C26_VIRTQ_DESC_WRITE, 0};
    c26_virtq_submit(&control_queue, 0);
    return wait_for_queue(&control_queue, 0) && response == SND_S_OK;
}

static int query_stream(uint32_t stream, snd_pcm_info_t *info)
{
    static snd_query_info_t query;
    static uint32_t response;
    query = (snd_query_info_t){SND_R_PCM_INFO, stream, 1, sizeof(*info)};
    response = 0;
    *info = (snd_pcm_info_t){0, 0, 0, 0, 0, 0, 0, {0,0,0,0,0}};
    control_memory.descriptors[0] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)&query, sizeof(query), C26_VIRTQ_DESC_NEXT, 1};
    control_memory.descriptors[1] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)&response, sizeof(response),
        C26_VIRTQ_DESC_WRITE | C26_VIRTQ_DESC_NEXT, 2};
    control_memory.descriptors[2] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)info, sizeof(*info), C26_VIRTQ_DESC_WRITE, 0};
    c26_virtq_submit(&control_queue, 0);
    return wait_for_queue(&control_queue, 0) && response == SND_S_OK;
}

static void submit_period(unsigned int period)
{
    unsigned int first = period * 3;
    c26_audio_render(periods[period], SOUND_PERIOD_FRAMES);
    transfers[period].stream_id = selected_stream;
    period_status[period] = (snd_pcm_status_t){0, 0};
    transmit_memory.descriptors[first] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)&transfers[period], sizeof(transfers[period]),
        C26_VIRTQ_DESC_NEXT, (uint16_t)(first + 1)};
    transmit_memory.descriptors[first + 1] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)periods[period], SOUND_PERIOD_BYTES,
        C26_VIRTQ_DESC_NEXT, (uint16_t)(first + 2)};
    transmit_memory.descriptors[first + 2] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)&period_status[period], sizeof(period_status[period]),
        C26_VIRTQ_DESC_WRITE, 0};
    c26_virtq_submit(&transmit_queue, (uint16_t)first);
}

static int initialize_backend(void)
{
    if (!c26_virtio_find(VIRTIO_DEVICE_SOUND, 0, &sound_device) ||
        !c26_virtio_begin(&sound_device, 0, 0) ||
        !c26_virtio_queue_setup(&sound_device, 0, &control_queue,
            control_memory.descriptors, &control_memory.available,
            &control_memory.used, C26_VIRTQ_SIZE) ||
        !c26_virtio_queue_setup(&sound_device, 1, &event_queue,
            event_memory.descriptors, &event_memory.available,
            &event_memory.used, C26_VIRTQ_SIZE) ||
        !c26_virtio_queue_setup(&sound_device, 2, &transmit_queue,
            transmit_memory.descriptors, &transmit_memory.available,
            &transmit_memory.used, C26_VIRTQ_SIZE) ||
        !c26_virtio_queue_setup(&sound_device, 3, &receive_queue,
            receive_memory.descriptors, &receive_memory.available,
            &receive_memory.used, C26_VIRTQ_SIZE) ||
        !c26_virtio_finish(&sound_device)) {
        return 0;
    }

    for (uint16_t i = 0; i < event_queue.size; i++) {
        event_memory.descriptors[i] = (c26_virtq_desc_t){
            (uint64_t)(uintptr_t)&sound_events[i], sizeof(sound_events[i]),
            C26_VIRTQ_DESC_WRITE, 0};
        c26_virtq_submit(&event_queue, i);
    }

    uint32_t streams = c26_virtio_config_read32(&sound_device, 4);
    int found = 0;
    for (uint32_t stream = 0; stream < streams; stream++) {
        snd_pcm_info_t info;
        if (query_stream(stream, &info) && info.direction == SND_DIRECTION_OUTPUT &&
            (info.formats & (1ULL << SND_FORMAT_S16)) != 0 &&
            (info.rates & (1ULL << SND_RATE_48000)) != 0 &&
            info.channels_min <= 2 && info.channels_max >= 2) {
            selected_stream = stream;
            found = 1;
            break;
        }
    }
    if (!found) return 0;

    snd_set_params_t parameters = {
        {SND_R_PCM_SET_PARAMS, selected_stream},
        SOUND_PERIOD_BYTES * SOUND_PERIOD_COUNT,
        SOUND_PERIOD_BYTES,
        0,
        2,
        SND_FORMAT_S16,
        SND_RATE_48000,
        0,
    };
    if (!control_command(&parameters, sizeof(parameters))) return 0;
    snd_pcm_header_t prepare = {SND_R_PCM_PREPARE, selected_stream};
    if (!control_command(&prepare, sizeof(prepare))) return 0;
    for (unsigned int i = 0; i < SOUND_PERIOD_COUNT; i++) {
        submit_period(i);
    }
    snd_pcm_header_t start = {SND_R_PCM_START, selected_stream};
    return control_command(&start, sizeof(start));
}

int c26_audio_backend_online(void)
{
    return backend_online;
}

void c26_audio_poll(void)
{
    if (!backend_online) return;
    uint32_t id;
    while (c26_virtq_pop(&transmit_queue, &id, 0)) {
        unsigned int period = id / 3;
        if (period < SOUND_PERIOD_COUNT &&
            period_status[period].status == SND_S_OK) {
            submit_period(period);
        }
    }
    while (c26_virtq_pop(&event_queue, &id, 0)) {
        if (id < event_queue.size) {
            c26_virtq_submit(&event_queue, (uint16_t)id);
        }
    }
    c26_virtio_ack_interrupt(&sound_device);
}

void c26_audio_demo(void)
{
    c26_audio_mixer_init();
    c26_audio_voice_start(0, C26_WAVE_SQUARE, 220, 70, 48);
    c26_audio_voice_start(1, C26_WAVE_TRIANGLE, 330, 85, 128);
    c26_audio_voice_start(2, C26_WAVE_SAW, 440, 55, 208);
    backend_online = initialize_backend();

    int16_t verification[64 * 2];
    c26_audio_render(verification, 64);
    uint32_t checksum = 2166136261U;
    for (unsigned int i = 0; i < 64 * 2; i++) {
        checksum ^= (uint16_t)verification[i];
        checksum *= 16777619U;
    }
    c26_puts("AUDIO MIXER: 8 voices, 48kHz stereo PCM, pan + envelope\n");
    c26_puts("VIRTIO SOUND: ");
    c26_puts(backend_online ? "PCM output stream online\n" :
                              "not present, software mixer active\n");
    c26_puts("AUDIO DSP CHECKSUM: ");
    c26_put_hex(checksum);
    c26_uart_putc('\n');

    /* The checksum voices were a verification vector, not a soundtrack —
       silence them so the machine boots quiet. */
    c26_audio_voice_stop(0);
    c26_audio_voice_stop(1);
    c26_audio_voice_stop(2);
}
