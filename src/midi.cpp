#include "midi.h"
#include "synth.h"
#include <Arduino.h>

// UART MIDI
static constexpr int MIDI_RX_PIN = 16;

// État du parseur MIDI
static uint8_t running_status = 0;
static uint8_t data1 = 0;
static bool waiting_data1 = true;

void midi_init() {
    Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, -1);
}

void midi_process() {
    while (Serial2.available()) {
        uint8_t b = Serial2.read();

        // ----- Status byte -----
        if (b & 0x80) {
            running_status = b;
            waiting_data1 = true;
            continue;
        }

        // ----- Data byte -----
        if (running_status == 0)
            continue; // pas encore de status valide

        uint8_t status = running_status & 0xF0;
        uint8_t channel = running_status & 0x0F;
        (void)channel; // mono pour l'instant

        if (waiting_data1) {
            data1 = b;
            waiting_data1 = false;
        } else {
            uint8_t data2 = b;
            waiting_data1 = true;

            // ----- NOTE ON -----
            if (status == 0x90) {
                Serial.println("NoteOn");
                if (data2 > 0) {
                    synth_note_on(data1, data2);
                }
                // Note On avec vélocité 0 = Note Off (ignoré pour l'instant)
            }

            // ----- NOTE OFF (optionnel, pas encore utilisé) -----
            // else if (status == 0x80) {
            //     synth_note_off(data1);
            // }
        }
    }
}
