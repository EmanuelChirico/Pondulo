/**
 * @file    mp3_player.c
 * @brief   Driver modulare e ultraleggero per Serial MP3 Player v1.2 (YX5300)
 *          Supporta qualsiasi MCU STM32 su ChibiOS (Nucleo-F401RE, G474RE, ecc.)
 */

#include "mp3_player.h"

/* Configurazione UART: 9600 baud, 8N1 */
static const SerialConfig mp3_serial_cfg = {
  9600,
  0,
  0,
  0
};

static SerialDriver *s_driver = &MP3_SERIAL_DRIVER;
static uint8_t       s_volume = 25;          /* Volume predefinito (0 - 30) */
static uint16_t      s_current_track = 1;
static bool          s_is_playing = false;

void mp3_init(void) {
  mp3_init_custom(&MP3_SERIAL_DRIVER, MP3_AUTO_INIT_PINS,
                  MP3_TX_PORT, MP3_TX_PIN, MP3_TX_AF,
                  MP3_RX_PORT, MP3_RX_PIN, MP3_RX_AF);
}

void mp3_init_custom(SerialDriver *sdp, bool config_pins,
                     ioportid_t tx_port, uint16_t tx_pin, uint8_t tx_af,
                     ioportid_t rx_port, uint16_t rx_pin, uint8_t rx_af) {
  s_driver = sdp;

#if defined(HAL_USE_PAL) && (HAL_USE_PAL == TRUE)
  if (config_pins) {
    palSetPadMode(tx_port, tx_pin, PAL_MODE_ALTERNATE(tx_af));
    palSetPadMode(rx_port, rx_pin, PAL_MODE_ALTERNATE(rx_af));
  }
#else
  (void)config_pins; (void)tx_port; (void)tx_pin; (void)tx_af;
  (void)rx_port; (void)rx_pin; (void)rx_af;
#endif

  /* Avvio della seriale associata al driver MP3 */
  sdStart(s_driver, &mp3_serial_cfg);

  /* Attesa di avvio e stabilizzazione del chip YX5300 all'accensione */
  chThdSleepMilliseconds(500);

  /* Seleziona scheda TF / MicroSD */
  mp3_select_tf_card();
  chThdSleepMilliseconds(500);       /* Tempo critico: il chip deve leggere la FAT della MicroSD */

  /* Imposta volume alto per default (28 su 30) */
  s_volume = 28;
  mp3_set_volume(s_volume);
  chThdSleepMilliseconds(100);
}

/**
 * @brief Invia un pacchetto standard da 8 byte al modulo YX5300.
 *        Formato: 0x7E | 0xFF | 0x06 | CMD | FEEDBACK | DAT_HI | DAT_LO | 0xEF
 */
void mp3_send_cmd(uint8_t cmd, uint16_t dat) {
  uint8_t buf[8];
  buf[0] = 0x7E;                     /* Start byte */
  buf[1] = 0xFF;                     /* Versione protocollo */
  buf[2] = 0x06;                     /* Lunghezza payload */
  buf[3] = cmd;                      /* Codice comando */
  buf[4] = 0x00;                     /* Feedback (0x00=No, 0x01=Sì) */
  buf[5] = (uint8_t)(dat >> 8);      /* Parametro byte alto */
  buf[6] = (uint8_t)(dat & 0xFF);    /* Parametro byte basso */
  buf[7] = 0xEF;                     /* End byte */

  chnWrite(s_driver, buf, 8);
  chThdSleepMilliseconds(50);        /* Pausa necessaria tra comandi successivi */
}

void mp3_play(void) {
  /* Su YX5300 il comando 0x03 garantisce l'avvio anche a freddo */
  mp3_send_cmd(0x03, s_current_track);
  s_is_playing = true;
}

void mp3_pause(void) {
  mp3_send_cmd(0x0E, 0x0000);
  s_is_playing = false;
}

void mp3_stop(void) {
  mp3_send_cmd(0x16, 0x0000);
  s_is_playing = false;
}

void mp3_next(void) {
  mp3_send_cmd(0x01, 0x0000);
  s_current_track++;
  s_is_playing = true;
}

void mp3_prev(void) {
  mp3_send_cmd(0x02, 0x0000);
  if (s_current_track > 1) {
    s_current_track--;
  }
  s_is_playing = true;
}

void mp3_play_track(uint16_t track_num) {
  if (track_num == 0) {
    track_num = 1;
  }
  /* Comando standard 0x03 per riprodurre file presenti nella cartella principale (root) */
  mp3_send_cmd(0x03, track_num);
  s_current_track = track_num;
  s_is_playing = true;
}

void mp3_play_folder_track(uint8_t folder, uint8_t track) {
  uint16_t dat = ((uint16_t)folder << 8) | track;
  mp3_send_cmd(0x0F, dat);
  s_current_track = track;
  s_is_playing = true;
}

void mp3_set_volume(uint8_t volume) {
  if (volume > 30) {
    volume = 30;
  }
  s_volume = volume;
  mp3_send_cmd(0x06, (uint16_t)volume);
}

void mp3_volume_up(void) {
  if (s_volume < 30) {
    s_volume++;
  }
  mp3_send_cmd(0x04, 0x0000);
}

void mp3_volume_down(void) {
  if (s_volume > 0) {
    s_volume--;
  }
  mp3_send_cmd(0x05, 0x0000);
}

void mp3_set_eq(uint8_t eq) {
  if (eq > 5) {
    eq = 0;
  }
  mp3_send_cmd(0x07, (uint16_t)eq);
}

void mp3_repeat_track(uint16_t track_num) {
  mp3_send_cmd(0x08, track_num);
  s_is_playing = true;
}

void mp3_repeat_all(bool enable) {
  mp3_send_cmd(0x11, enable ? 0x0001 : 0x0000);
}

void mp3_reset(void) {
  mp3_send_cmd(0x0C, 0x0000);
  s_is_playing = false;
}

void mp3_select_tf_card(void) {
  mp3_send_cmd(0x09, 0x0002);        /* 0x02 seleziona la MicroSD */
}

/**
 * @brief Polling non bloccante per verificare se è arrivato un pacchetto di evento dal modulo.
 * @return true se è stato ricevuto un pacchetto valido, false altrimenti.
 */
bool mp3_poll_event(uint8_t *cmd, uint16_t *dat) {
  msg_t b = chnGetTimeout((BaseChannel *)s_driver, TIME_IMMEDIATE);
  if (b == 0x7E) {
    msg_t ver = chnGetTimeout((BaseChannel *)s_driver, TIME_MS2I(50));
    msg_t len = chnGetTimeout((BaseChannel *)s_driver, TIME_MS2I(50));
    msg_t c   = chnGetTimeout((BaseChannel *)s_driver, TIME_MS2I(50));
    msg_t fbk = chnGetTimeout((BaseChannel *)s_driver, TIME_MS2I(50));
    msg_t hi  = chnGetTimeout((BaseChannel *)s_driver, TIME_MS2I(50));
    msg_t lo  = chnGetTimeout((BaseChannel *)s_driver, TIME_MS2I(50));
    msg_t end = chnGetTimeout((BaseChannel *)s_driver, TIME_MS2I(50));

    (void)ver; (void)len; (void)fbk;

    if (end == 0xEF) {
      if (cmd != NULL) *cmd = (uint8_t)c;
      if (dat != NULL) *dat = ((uint16_t)(hi & 0xFF) << 8) | (lo & 0xFF);
      return true;
    }
  }
  return false;
}

uint8_t mp3_get_volume(void) {
  return s_volume;
}

uint16_t mp3_get_current_track(void) {
  return s_current_track;
}

bool mp3_is_playing(void) {
  return s_is_playing;
}
