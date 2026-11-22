/**
 * @file    mp3_player.h
 * @brief   Driver for Serial MP3 Player v1.2 (YX5300 chipset)
 *          running on STM32 Nucleo-F401RE with ChibiOS.
 */

#ifndef MP3_PLAYER_H
#define MP3_PLAYER_H

#include "hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================== */
/* Parametri Hardware predefiniti (sovrascrivibili prima del #include)         */
/* =========================================================================== */
#if !defined(MP3_SERIAL_DRIVER)
#define MP3_SERIAL_DRIVER           SD5
#endif

#if !defined(MP3_TX_PORT)
#define MP3_TX_PORT                 GPIOC
#define MP3_TX_PIN                  12U
#define MP3_TX_AF                   5U
#endif

#if !defined(MP3_RX_PORT)
#define MP3_RX_PORT                 GPIOD
#define MP3_RX_PIN                  2U
#define MP3_RX_AF                   5U
#endif

#if !defined(MP3_AUTO_INIT_PINS)
#define MP3_AUTO_INIT_PINS          TRUE
#endif

/* Equalizer presets */
#define MP3_EQ_NORMAL               0
#define MP3_EQ_POP                  1
#define MP3_EQ_ROCK                 2
#define MP3_EQ_JAZZ                 3
#define MP3_EQ_CLASSIC              4
#define MP3_EQ_BASS                 5

/* Codici di notifica / evento inviati dal modulo YX5300 */
#define MP3_EVENT_TRACK_FINISHED    0x3D
#define MP3_EVENT_CARD_INSERTED     0x3A
#define MP3_EVENT_CARD_REMOVED      0x3B
#define MP3_EVENT_ACK               0x41
#define MP3_EVENT_ERROR             0x40

/* =========================================================================== */
/* Mappe sonore semantiche DEMO6 (da mindmap.io 2026-09-04)                   */
/* =========================================================================== */
#define SOUND_BENVENUTO_MENU         3U  /* Traccia 3: "Bip, bop, bip, traduzione in corso: Ciao umano!" */
#define SOUND_OPERAZIONE_OK         10U  /* Traccia 10: "Operazione completata con successo!" */
#define SOUND_TASTO_BIP             13U  /* Traccia 13: Bip / Chime acustico di notifica */
#define SOUND_CASSA                 23U  /* Traccia 23: Cassa registratore / Monete */
#define SOUND_DING_DONG             28U  /* Traccia 28: Notifica melodica / Ding-Dong */

/* =========================================================================== */
/* API Modulare e Ultraleggera (Zero allocazioni dinamiche, ~500 byte Flash)   */
/* =========================================================================== */

/**
 * @brief Inizializzazione rapida (usa la porta e i pin predefiniti sopra).
 */
void mp3_init(void);

/**
 * @brief Inizializzazione flessibile per merge istantaneo in qualsiasi progetto.
 * @param[in] sdp         Puntatore al SerialDriver (es: &SD1, &SD2, &SD3, &LPUART1)
 * @param[in] config_pins Se true configura automaticamente i pin GPIO TX/RX in alternate mode
 * @param[in] tx_port     Porta GPIO TX (es: GPIOA)
 * @param[in] tx_pin      Numero pin TX (es: 9)
 * @param[in] tx_af       Alternate function TX (es: 7)
 * @param[in] rx_port     Porta GPIO RX (es: GPIOA)
 * @param[in] rx_pin      Numero pin RX (es: 10)
 * @param[in] rx_af       Alternate function RX (es: 7)
 */
void mp3_init_custom(SerialDriver *sdp, bool config_pins,
                     ioportid_t tx_port, uint16_t tx_pin, uint8_t tx_af,
                     ioportid_t rx_port, uint16_t rx_pin, uint8_t rx_af);

void mp3_send_cmd(uint8_t cmd, uint16_t dat);

/* Controlli di riproduzione */
void mp3_play(void);
void mp3_pause(void);
void mp3_stop(void);
void mp3_next(void);
void mp3_prev(void);
void mp3_play_track(uint16_t track_num);
void mp3_play_folder_track(uint8_t folder, uint8_t track);

/* Volume ed Equalizzazione */
void mp3_set_volume(uint8_t volume); /* range 0 - 30 */
void mp3_volume_up(void);
void mp3_volume_down(void);
void mp3_set_eq(uint8_t eq);

/* Modalità loop e reset */
void mp3_repeat_track(uint16_t track_num);
void mp3_repeat_all(bool enable);
void mp3_reset(void);
void mp3_select_tf_card(void);

/* Lettura eventi non bloccante (opzionale per il nuovo codice) */
bool mp3_poll_event(uint8_t *cmd, uint16_t *dat);

/* Getters di stato */
uint8_t  mp3_get_volume(void);
uint16_t mp3_get_current_track(void);
bool     mp3_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif /* MP3_PLAYER_H */
