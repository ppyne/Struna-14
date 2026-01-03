#pragma once
#include <stdint.h>

void synth_init();
void synth_process(float* out, int nframes);

// interface MIDI
void synth_note_on(uint8_t note, uint8_t velocity);
