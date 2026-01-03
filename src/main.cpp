#include <Arduino.h>

#include "audio.h"
#include "synth.h"
#include "midi.h"

void setup() {
    Serial.begin(115200);

    audio_init();
    synth_init();
    midi_init();
}

void loop() {
    midi_process();     // entrée (bouton)
    audio_process();    // sortie audio
}
