#include "audio.h"
#include "synth.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <string.h> // memset

static constexpr int SAMPLE_RATE = 44100;
static constexpr int BLOCK_SIZE  = 128;

// Buffers
static int16_t i2s_buffer[BLOCK_SIZE * 2]; // stéréo interleaved
static float   mix_buffer[BLOCK_SIZE];     // mono float

void audio_init() {
    // ---- I2S config (C++ safe) ----
    i2s_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = BLOCK_SIZE;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    // cfg.intr_alloc_flags = 0; // volontairement omis (pas toujours présent selon ESP-IDF)

    i2s_pin_config_t pins;
    memset(&pins, 0, sizeof(pins));

    pins.bck_io_num = 26;             // BCK
    pins.ws_io_num = 25;              // LRCK/WS
    pins.data_out_num = 27;           // DATA -> DIN
    pins.data_in_num = I2S_PIN_NO_CHANGE;

    i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pins);

    // Force clock config
    i2s_set_clk(I2S_NUM_0,
                SAMPLE_RATE,
                I2S_BITS_PER_SAMPLE_16BIT,
                I2S_CHANNEL_STEREO);

    i2s_zero_dma_buffer(I2S_NUM_0);
    i2s_start(I2S_NUM_0);
}

void audio_process() {
    // 1) Génération synthé (mono float)
    synth_process(mix_buffer, BLOCK_SIZE);

    // 2) Conversion vers I2S stéréo 16 bits
    for (int i = 0; i < BLOCK_SIZE; i++) {
        float s = mix_buffer[i];

        // soft clip
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;

        int16_t v = (int16_t)(s * 12000.0f);

        i2s_buffer[2 * i]     = v; // L
        i2s_buffer[2 * i + 1] = v; // R
    }

    // 3) Envoi DMA
    size_t bytes_written = 0;
    i2s_write(I2S_NUM_0,
              i2s_buffer,
              sizeof(i2s_buffer),
              &bytes_written,
              portMAX_DELAY);
}
