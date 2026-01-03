#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

// Initialisation du moteur audio
void audio_init();

void audio_process();

// Déclenche une note (fréquence en Hz)
void audio_note_on(float freq);

// (optionnel pour plus tard)
void audio_note_off();

#endif