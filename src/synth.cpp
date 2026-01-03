#include "synth.h"
#include <math.h>
#include <string.h>
#include <stdint.h>

static constexpr int   NUM_VOICES  = 14;
static constexpr float SAMPLE_RATE = 44100.0f;
static constexpr float DECAY_TIME  = 0.8f;

// Gain par voix (vous pouvez ajuster ensuite)
static constexpr float VOICE_GAIN  = 0.12f;

static constexpr float PLUCK_TIME = 0.0014f;
static constexpr float PLUCK_STRENGTH  = 0.3f;
static constexpr float PLUCK_LPF_FREQ  = 1200.0f;  // Hz (boisé)

static const float PLUCK_LPF_ALPHA =
    1.0f - expf(-2.0f * M_PI * PLUCK_LPF_FREQ / SAMPLE_RATE);

static constexpr float DETUNE_CENTS = 4.0f; // très musical

static constexpr float INHARMONIC_LEVEL  = 0.08f; // subtil
static constexpr float INHARMONIC_CENTS  = 3.5f;  // désaccord du partiel
static constexpr float INHARMONIC_CENTS_BASE = 1.5f;
static constexpr float INHARMONIC_CENTS_SLOPE = 2.0f;

// ---------- Wavetable sine ----------
static constexpr int   TABLE_BITS = 11;                  // 2^11 = 2048
static constexpr int   TABLE_SIZE = 1 << TABLE_BITS;
static float sine_table[TABLE_SIZE];
static bool table_init_done = false;

static void init_sine_table() {
    if (table_init_done) return;
    for (int i = 0; i < TABLE_SIZE; i++) {
        float ph = (2.0f * (float)M_PI * (float)i) / (float)TABLE_SIZE;
        sine_table[i] = sinf(ph);
    }
    table_init_done = true;
}

static inline float osc_saw(uint32_t phase) {
    // phase ∈ [0..2^32[
    return ((float)phase / 4294967296.0f) * 2.0f - 1.0f;
}

// phase 32-bit: index = high TABLE_BITS bits
static inline float osc_sine(uint32_t phase) {
    uint32_t idx = phase >> (32 - TABLE_BITS);
    return sine_table[idx & (TABLE_SIZE - 1)];
}

static inline uint32_t freq_to_phase_inc(float freq) {
    // phase_inc = freq * 2^32 / Fs
    double inc = (double)freq * (4294967296.0 / (double)SAMPLE_RATE);
    if (inc < 0.0) inc = 0.0;
    if (inc > 4294967295.0) inc = 4294967295.0;
    return (uint32_t)inc;
}

// ---------- Envelope ----------
enum EnvState { ENV_OFF, ENV_ATTACK, ENV_DECAY };

struct Voice {
    bool     active;

    uint32_t phase;
    uint32_t phase_inc;

    float    env;
    EnvState env_state;

    float    attack_coeff;   // attack: env += (1-env)*attack_coeff
    float    decay_coeff;    // decay:  env *= decay_coeff

    float pluck_env;
    float pluck_decay;
    float pluck_lpf;

    uint32_t age;

    float detune;

    uint32_t phase2;
    uint32_t phase2_inc;
};

static Voice voices[NUM_VOICES];
static uint32_t global_age = 0;

static inline float cents_to_ratio(float cents) {
    return powf(2.0f, cents / 1200.0f);
}

static inline float white_noise() {
    return ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
}

// MIDI note -> freq
static inline float midi_note_to_freq(uint8_t note) {
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

void synth_init() {
    init_sine_table();
    memset(voices, 0, sizeof(voices));
    global_age = 0;
}

// voix à voler : inactive -> plus faible -> plus ancienne
static int find_voice_to_steal() {
    for (int i = 0; i < NUM_VOICES; i++)
        if (!voices[i].active) return i;

    int idx = 0;
    float min_env = voices[0].env;
    for (int i = 1; i < NUM_VOICES; i++) {
        if (voices[i].env < min_env) {
            min_env = voices[i].env;
            idx = i;
        }
    }
    uint32_t oldest = voices[idx].age;
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voices[i].env == min_env && voices[i].age < oldest) {
            oldest = voices[i].age;
            idx = i;
        }
    }
    return idx;
}

void synth_note_on(uint8_t note, uint8_t velocity) {

    int v = find_voice_to_steal();

    float freq = midi_note_to_freq(note);

    // désaccord statique par voix (réparti autour de 0)
    float detune_cents =
        ((float)v / (NUM_VOICES - 1) - 0.5f) * 2.0f * DETUNE_CENTS;

    voices[v].detune = cents_to_ratio(detune_cents);

    // IMPORTANT: phase peut être 0 (attaque propre) ou légèrement décalée;
    // ici on garde 0 car l’oscillateur est stable et l’attaque n’est pas instantanée.
    voices[v].phase = 0;

    voices[v].phase_inc = freq_to_phase_inc(freq * voices[v].detune);

    float inh_cents =
        INHARMONIC_CENTS_BASE +
        INHARMONIC_CENTS_SLOPE * log2f(freq / 440.0f);

    float jitter = ((float)rand() / RAND_MAX - 0.5f) * 0.6f; // ±0.3 cent
    inh_cents += jitter;

    float inh_freq = freq * 2.0f * cents_to_ratio(inh_cents);

    voices[v].phase2 = 0;
    voices[v].phase2_inc = freq_to_phase_inc(inh_freq);

    // enveloppe
    voices[v].env = 0.0001f;
    voices[v].env_state = ENV_ATTACK;

    voices[v].attack_coeff = 0.02f; // ~attaque courte
    // decay exponentiel basé sur DECAY_TIME:
    voices[v].decay_coeff  = expf(-1.0f / (DECAY_TIME * SAMPLE_RATE));

    // vélocité : on peut la mettre comme facteur d’amplitude (optionnel mais utile)
    // ici on l'applique en "plafond" via un gain sur VOICE_GAIN dans le mix, donc:
    // (si vous voulez vraiment la vélocité, on peut stocker un voice_gain par voix)
    (void)velocity;

    voices[v].active = true;
    voices[v].age = global_age++;

    // pincement
    voices[v].pluck_env   = 1.0f;
    voices[v].pluck_decay = expf(-1.0f / (PLUCK_TIME * SAMPLE_RATE)); // ~2 ms
    voices[v].pluck_lpf   = 0.0f;

}

void synth_process(float* out, int nframes) {

    memset(out, 0, nframes * sizeof(float));

    for (int v = 0; v < NUM_VOICES; v++) {
        if (!voices[v].active) continue;

        for (int i = 0; i < nframes; i++) {

            // bruit de pincement (attaque)
            float pluck = 0.0f;
            if (voices[v].pluck_env > 1e-4f) {
                // bruit blanc
                float n = white_noise();

                // passe-bas 1 pôle (boisé)
                voices[v].pluck_lpf += PLUCK_LPF_ALPHA * (n - voices[v].pluck_lpf);

                pluck = voices[v].pluck_lpf * voices[v].pluck_env;
                voices[v].pluck_env *= voices[v].pluck_decay;
            }

            // sinus principal
            float s1 = osc_sine(voices[v].phase);
            //float s2 = osc_sine(voices[v].phase2); // (1)
            float saw = osc_saw(voices[v].phase2); // (2)

            //float inh_level = INHARMONIC_LEVEL * (0.4f + 0.6f * voices[v].env); // (1)
            //float s = s1 + s2 * inh_level; // (1)
            float s = s1 + 0.15f * saw;

            // mix : pincement + résonance
            out[i] += (s * voices[v].env + pluck * PLUCK_STRENGTH) * VOICE_GAIN;

            voices[v].phase += voices[v].phase_inc;
            voices[v].phase2 += voices[v].phase2_inc;

            // enveloppe
            if (voices[v].env_state == ENV_ATTACK) {
                voices[v].env += (1.0f - voices[v].env) * voices[v].attack_coeff;
                if (voices[v].env > 0.99f)
                    voices[v].env_state = ENV_DECAY;
            } else if (voices[v].env_state == ENV_DECAY) {
                voices[v].env *= voices[v].decay_coeff;
                if (voices[v].env < 1e-4f) {
                    voices[v].env = 0.0f;
                    voices[v].active = false;
                    voices[v].env_state = ENV_OFF;
                    break;
                }
            }
        }
    }

    // Sécurité légère (optionnelle) contre pics résiduels sans “pompage”
    for (int i = 0; i < nframes; i++) {
        float x = out[i];
        // soft-clip doux type x/(1+|x|)
        out[i] = x / (1.0f + fabsf(x));
    }
}
