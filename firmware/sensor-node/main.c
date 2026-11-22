/*
 * ============================================================================
 * Firmware Fork Node: Sensore ToF VL53L7CX - Cursore Mouse Virtuale
 * Shield: X-NUCLEO-53L7A1 montato su NUCLEO-G474RE
 *
 * Calcola la posizione pixel (x, y) della zona ToF più vicina entro la
 * soglia di interazione e trasmette il protocollo compatto $XY,...
 * su USART3 dedicata (PC10=TX @ 921600 baud) verso il renderer su Board B.
 * SD2/USART2 (PA2=TX @ 921600 baud) resta dedicata alla telemetria/debug VCP.
 * ============================================================================
 */

#include "ch.h"
#include "hal.h"
#include "vl53l7cx_api.h"
#include "chprintf.h"
#include "cursor.h"
#include "keypad_link.h"
#include "gesture.h"

/* Serial driver stream pointer */
static BaseSequentialStream *chp = (BaseSequentialStream*)&SD2;

/* Serial configuration: 921600 baud, 8N1 */
static const SerialConfig serial_cfg = {
  921600,
  0,
  USART_CR2_STOP1_BITS,
  0
};

/* I2C Configuration (Fast Mode Plus 1MHz timing for STM32G4 at PCLK1=85MHz) */
static const I2CConfig i2ccfg = {
  STM32_TIMINGR_PRESC(4U) |
  STM32_TIMINGR_SCLDEL(2U) | STM32_TIMINGR_SDADEL(0U) | STM32_TIMINGR_SCLH(4U) |
  STM32_TIMINGR_SCLL(10U),
  0, 0
};

static const VL53L7CX_Platform vl53l7cxcfg = {
  .i2cp = &I2CD1,
  .i2ccfg = &i2ccfg,
  .address = 0x29
};

/*********************************/
/*   VL53L7CX ranging variables  */
/*********************************/

static VL53L7CX_Configuration Dev;
static VL53L7CX_ResultsData Results;

/* Binary semaphore signaled by Data-Ready EXTI ISR on PA4 */
static binary_semaphore_t data_ready_bsem;

/*
 * Interrupt service callback: Triggered on PA4 FALLING edge (VL53L7CX INT pin / Arduino A2).
 * Signals that a new ranging frame is ready in sensor memory.
 */
static void data_ready_cb(void *arg) {
  (void)arg;
  chSysLockFromISR();
  chBSemSignalI(&data_ready_bsem);
  chSysUnlockFromISR();
}

/* Sensor operating modes */
typedef enum {
  MODE_8X8_15HZ = 0,
  MODE_4X4_60HZ = 1
} sensor_mode_t;

static volatile sensor_mode_t current_mode = MODE_8X8_15HZ;
static volatile bool mode_change_requested = false;
static volatile bool sensor_healthy = false;

/*
 * Blinker thread: Heartbeat indicating RTOS & Sensor status.
 * - 1 Hz (500ms on / 500ms off): Sensor operating nominally.
 * - 5 Hz (100ms on / 100ms off): Sensor error or disconnected.
 */
static THD_WORKING_AREA(waBlinkerThread, 256);
static THD_FUNCTION(BlinkerThread, arg) {
  (void)arg;
  chRegSetThreadName("blinker");
  while (true) {
    systime_t sleep_time = sensor_healthy ? TIME_MS2I(500) : TIME_MS2I(100);
    palClearPad(GPIOA, GPIOA_LED_GREEN);
    chThdSleep(sleep_time);
    palSetPad(GPIOA, GPIOA_LED_GREEN);
    chThdSleep(sleep_time);
  }
}

/*
 * User Button Thread: Monitors PC13 (B1) for mode switching.
 */
static THD_WORKING_AREA(waButtonThread, 256);
static THD_FUNCTION(ButtonThread, arg) {
  (void)arg;
  chRegSetThreadName("button_monitor");
  while (true) {
    if (palReadPad(GPIOC, GPIOC_BUTTON) == PAL_LOW) {
      /* Debounce wait */
      chThdSleepMilliseconds(60);
      if (palReadPad(GPIOC, GPIOC_BUTTON) == PAL_LOW) {
        /* Toggle mode */
        current_mode = (current_mode == MODE_8X8_15HZ) ? MODE_4X4_60HZ : MODE_8X8_15HZ;
        mode_change_requested = true;

        /* Wait until button released */
        while (palReadPad(GPIOC, GPIOC_BUTTON) == PAL_LOW) {
          chThdSleepMilliseconds(20);
        }
      }
    }
    chThdSleepMilliseconds(50);
  }
}

/* Shared cursor state between main ranging loop and UiTickThread */
typedef struct {
  bool valid;
  uint16_t x;
  uint16_t y;
  uint16_t dist_mm;
} cursor_pos_t;

static cursor_pos_t current_cursor = {false, 0, 0, 0};
static mutex_t cursor_mutex;

static int32_t display_x_q8 = 0;
static int32_t display_y_q8 = 0;
static bool display_active = false;

/*
 * UiTickThread: Disaccoppiato dal framerate del ToF (15Hz/60Hz).
 * Invia frame $XY,... su USART3 a ~100Hz con glide temporale EMA.
 * Quando la posizione o validita' cambia (o come heartbeat periodico ogni ~500ms).
 */
static THD_WORKING_AREA(waUiTickThread, 512);
static THD_FUNCTION(UiTickThread, arg) {
  (void)arg;
  chRegSetThreadName("ui_tick");

  /* Static FSM state - zero stack pressure */
  static gesture_state_t gesture_st;
  gesture_reset(&gesture_st);

  while (true) {
    bool send_valid = false;
    uint16_t send_x = 0;
    uint16_t send_y = 0;
    uint16_t send_dist_mm = 0;

    chMtxLock(&cursor_mutex);
    if (current_cursor.valid) {
      int32_t target_x_q8 = ((int32_t)current_cursor.x) << 8;
      int32_t target_y_q8 = ((int32_t)current_cursor.y) << 8;

      if (!display_active) {
        /*
         * Primo aggancio del bersaglio: inizializzazione istantanea.
         * Evita uno scivolamento da (0,0) attraverso lo schermo
         * al primo ingresso della mano nel FoV.
         */
        display_x_q8 = target_x_q8;
        display_y_q8 = target_y_q8;
        display_active = true;
      } else {
        /* EMA glide temporale in Q8 */
        display_x_q8 = cursor_ema_update(display_x_q8, target_x_q8, ALPHA_NUM, ALPHA_DEN);
        display_y_q8 = cursor_ema_update(display_y_q8, target_y_q8, ALPHA_NUM, ALPHA_DEN);
      }

      send_valid = true;
      send_x = (uint16_t)(display_x_q8 >> 8);
      send_y = (uint16_t)(display_y_q8 >> 8);
      send_dist_mm = current_cursor.dist_mm;
    } else {
      /*
       * Target non valido (mano uscita dal range): azzera lo stato d'inseguimento.
       * Il cursore scompare di scatto, non scivola via.
       */
      display_x_q8 = 0;
      display_y_q8 = 0;
      display_active = false;
      send_valid = false;
      send_x = 0;
      send_y = 0;
      send_dist_mm = 0;
    }
    chMtxUnlock(&cursor_mutex);

    systime_t now = chVTGetSystemTimeX();
    keypad_link_send_if_dirty(send_valid, send_x, send_y, now);

    gesture_event_t g = gesture_update(&gesture_st, send_valid, send_x, send_y, send_dist_mm, now);
    if (g != GESTURE_NONE) {
      keypad_link_send_gesture(g, now);
    }

    chThdSleepMilliseconds(10);
  }
}

/* Sharpener: percentuale di "blur" fra zone adiacenti da rimuovere */
#define SHARPENER_PERCENT 30

/*
 * Helper function to configure VL53L7CX mode
 */
static uint8_t configure_sensor_mode(sensor_mode_t mode) {
  uint8_t status = VL53L7CX_STATUS_OK;

  status |= vl53l7cx_set_ranging_mode(&Dev, VL53L7CX_RANGING_MODE_CONTINUOUS);
  status |= vl53l7cx_set_sharpener_percent(&Dev, SHARPENER_PERCENT);

  if (mode == MODE_8X8_15HZ) {
    status |= vl53l7cx_set_resolution(&Dev, VL53L7CX_RESOLUTION_8X8);
    status |= vl53l7cx_set_ranging_frequency_hz(&Dev, 15);
    status |= vl53l7cx_set_target_order(&Dev, VL53L7CX_TARGET_ORDER_CLOSEST);
    chprintf(chp, "# Mode changed to 8x8 (64 zones) @ 15 Hz\r\n");
  } else {
    status |= vl53l7cx_set_resolution(&Dev, VL53L7CX_RESOLUTION_4X4);
    status |= vl53l7cx_set_ranging_frequency_hz(&Dev, 60);
    status |= vl53l7cx_set_target_order(&Dev, VL53L7CX_TARGET_ORDER_CLOSEST);
    chprintf(chp, "# Mode changed to 4x4 (16 zones) @ 60 Hz\r\n");
  }

  return status;
}

/*
 * Application entry point.
 */
int main(void) {
  uint8_t status, isAlive;
  uint32_t integration_time_ms = 0;

  /*
   * System initializations.
   */
  halInit();
  chSysInit();

  /* Start Serial Port @ 921600 baud */
  sdStart(&SD2, &serial_cfg);

  /* UART2 GPIOs (ST-LINK VCP Debug on PA2/PA3) */
  palSetPadMode(GPIOA, 2U, PAL_MODE_ALTERNATE(7));
  palSetPadMode(GPIOA, 3U, PAL_MODE_ALTERNATE(7));

  /* UART3 GPIOs (Dedicated Data Link to Board B: PC10=TX, PC11=RX AF7).
   * OSPEED_HIGHEST matters here in particular: PC10 is the pin actually
   * driving the 921600 baud signal onto the wire to Board B. Without a fast
   * slew rate, bit transitions across an inter-board wire (not a PCB trace)
   * can be too slow to register cleanly on the far end, which looks like
   * "no signal ever arrives" since the receiver's line parser just silently
   * drops anything that doesn't parse as a valid $XY frame. */
  palSetPadMode(GPIOC, 10U, PAL_MODE_ALTERNATE(7) | PAL_STM32_OSPEED_HIGHEST);
  palSetPadMode(GPIOC, 11U, PAL_MODE_ALTERNATE(7) | PAL_STM32_OSPEED_HIGHEST);
  sdStart(&SD3, &serial_cfg);

  /* I2C1 GPIOs (PB8: SCL, PB9: SDA) */
  palSetLineMode(
      PAL_LINE(GPIOB, 8U),
      PAL_MODE_ALTERNATE(4) | PAL_STM32_OTYPE_OPENDRAIN | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUPDR_PULLUP);
  palSetLineMode(
      PAL_LINE(GPIOB, 9U),
      PAL_MODE_ALTERNATE(4) | PAL_STM32_OTYPE_OPENDRAIN | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUPDR_PULLUP);

  /* Shield power & control pins setup (X-NUCLEO-53L7A1) */
  palSetPadMode(GPIOC, 0U, PAL_MODE_OUTPUT_PUSHPULL);  /* LPn central sensor */
  palSetPadMode(GPIOA, 0U, PAL_MODE_OUTPUT_PUSHPULL);  /* PWR_EN shield */
  palSetPadMode(GPIOA, 5U, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOA, 8U, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOA, 9U, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOB, 0U, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOB, 10U, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOB, 4U, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOB, 5U, PAL_MODE_OUTPUT_PUSHPULL);
  palSetPadMode(GPIOB, 3U, PAL_MODE_OUTPUT_PUSHPULL);

  /* User Button (PC13) */
  palSetPadMode(GPIOC, GPIOC_BUTTON, PAL_MODE_INPUT_PULLUP);

  /* Power up central VL53L7CX and isolate satellites */
  palSetLine(PAL_LINE(GPIOC, 0U));
  palSetLine(PAL_LINE(GPIOA, 0U));
  palClearLine(PAL_LINE(GPIOA, 5U));
  palClearLine(PAL_LINE(GPIOA, 8U));
  palClearLine(PAL_LINE(GPIOA, 9U));
  palSetLine(PAL_LINE(GPIOB, 0U));
  palSetLine(PAL_LINE(GPIOB, 10U));
  palSetLine(PAL_LINE(GPIOB, 4U));
  palSetLine(PAL_LINE(GPIOB, 5U));
  palSetLine(PAL_LINE(GPIOB, 3U));
  palClearLine(PAL_LINE(GPIOB, 3U));

  /* Initialize Data-Ready binary semaphore */
  chBSemObjectInit(&data_ready_bsem, true);

  /* Configure INT pin on PA4 (Arduino A2) with internal pull-up and falling edge EXTI */
  palSetPadMode(GPIOA, 4U, PAL_MODE_INPUT_PULLUP);
  palSetLineCallback(PAL_LINE(GPIOA, 4U), data_ready_cb, NULL);
  palEnableLineEvent(PAL_LINE(GPIOA, 4U), PAL_EVENT_MODE_FALLING_EDGE);

  /* Initialize Cursor Mutex and Serial Link on SD3 */
  chMtxObjectInit(&cursor_mutex);
  keypad_link_init((BaseSequentialStream *)&SD3);

  /* Create background threads */
  chThdCreateStatic(waBlinkerThread, sizeof(waBlinkerThread), NORMALPRIO + 1, BlinkerThread, NULL);
  chThdCreateStatic(waButtonThread, sizeof(waButtonThread), NORMALPRIO + 1, ButtonThread, NULL);
  chThdCreateStatic(waUiTickThread, sizeof(waUiTickThread), NORMALPRIO, UiTickThread, NULL);

  /* Initialize I2C platform driver */
  Dev.platform = vl53l7cxcfg;

  /* Probe sensor */
  chprintf(chp, "# Probing VL53L7CX on I2C1 (0x%02X)...\r\n", Dev.platform.address);
  status = vl53l7cx_is_alive(&Dev, &isAlive);
  if (status || !isAlive) {
    chprintf(chp, "# ERROR: VL53L7CX not detected on I2C bus! (status: %u, isAlive: %u)\r\n", status, isAlive);
    sensor_healthy = false;
    while (1) {
      chThdSleepMilliseconds(1000);
    }
  }

  chprintf(chp, "# VL53L7CX detected! Loading ULD firmware...\r\n");

  /* Initialize sensor and download firmware blob */
  status = vl53l7cx_init(&Dev);
  if (status) {
    chprintf(chp, "# ERROR: VL53L7CX ULD firmware load failed (status: %u)\r\n", status);
    sensor_healthy = false;
    while (1) {
      chThdSleepMilliseconds(1000);
    }
  }

  sensor_healthy = true;
  chprintf(chp, "# VL53L7CX ULD initialized (Driver Revision: %s)\r\n", VL53L7CX_API_REVISION);

  /* Initial configuration: 8x8 @ 15Hz */
  configure_sensor_mode(current_mode);
  vl53l7cx_get_integration_time_ms(&Dev, &integration_time_ms);
  chprintf(chp, "# Integration time: %u ms\r\n", (unsigned int)integration_time_ms);

  /* Start ranging */
  status = vl53l7cx_start_ranging(&Dev);
  if (status) {
    chprintf(chp, "# ERROR: Failed to start ranging!\r\n");
  }

  /*********************************/
  /*       Main Ranging Loop       */
  /*********************************/
  uint32_t consecutive_errors = 0;
  const uint32_t MAX_CONSECUTIVE_ERRORS = 10;

  while (true) {
    /* Handle dynamic mode changes from user button */
    if (mode_change_requested) {
      mode_change_requested = false;
      sensor_mode_t prev_mode = (current_mode == MODE_8X8_15HZ) ? MODE_4X4_60HZ : MODE_8X8_15HZ;
      vl53l7cx_stop_ranging(&Dev);
      status = configure_sensor_mode(current_mode);
      if (status != VL53L7CX_STATUS_OK) {
        chprintf(chp, "# ERROR: Failed to configure mode, reverting\r\n");
        current_mode = prev_mode;
        configure_sensor_mode(current_mode);
      }
      status = vl53l7cx_start_ranging(&Dev);
      if (status != VL53L7CX_STATUS_OK) {
        chprintf(chp, "# ERROR: Failed to restart ranging after mode change\r\n");
        consecutive_errors++;
      }
    }

    /* Wait for Data-Ready interrupt from VL53L7CX on PA4 with a 100ms timeout */
    msg_t msg = chBSemWaitTimeout(&data_ready_bsem, TIME_MS2I(100));

    if (msg == MSG_TIMEOUT) {
      consecutive_errors++;
      if (consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
        sensor_healthy = false;
      }
    } else {
      status = vl53l7cx_get_ranging_data(&Dev, &Results);
      if (status != VL53L7CX_STATUS_OK) {
        consecutive_errors++;
        if (consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
          sensor_healthy = false;
        }
      } else {
        consecutive_errors = 0;
        sensor_healthy = true;

        uint8_t grid_dim = (current_mode == MODE_8X8_15HZ) ? 8U : 4U;
        uint8_t total_zones = grid_dim * grid_dim;

        int8_t closest_zone = -1;
        uint16_t min_dist = 0xFFFFU;

        for (uint8_t z = 0; z < total_zones; z++) {
          uint16_t dist_raw = Results.distance_mm[VL53L7CX_NB_TARGET_PER_ZONE * z];
          uint8_t stat = Results.target_status[VL53L7CX_NB_TARGET_PER_ZONE * z];

          /*
           * Target filtering:
           * status in (5, 9): ST ToF standard (5 = Range Valid, 9 = Large pulse)
           * dist_raw > 30 mm (reject coverglass reflection)
           * dist_raw < 250 mm (active interaction zone threshold)
           */
          if (((stat == 5U) || (stat == 9U)) && (dist_raw > 30U) && (dist_raw < 250U)) {
            if (dist_raw < min_dist) {
              min_dist = dist_raw;
              closest_zone = (int8_t)z;
            }
          }
        }

        chMtxLock(&cursor_mutex);
        if (closest_zone >= 0) {
          /*
           * Raccoglie la zona piu' vicina e le sue zone 4-vicine (row+/-1, col+/-1)
           * che passano lo stesso filtro di validita' e la cui distanza non differisce
           * da min_dist per piu' di NEIGHBOR_DIST_MARGIN_MM (40mm).
           */
          cursor_target_zone_t cluster[5];
          uint8_t cluster_cnt = 0;

          uint8_t c_row = (uint8_t)closest_zone / grid_dim;
          uint8_t c_col = (uint8_t)closest_zone % grid_dim;

          /*
           * invert_h: il sensore guarda verso l'utente (effetto specchio),
           * come gia' gestito da build_zone_key_map(invert_h) nei tool host
           * Python di questo progetto. La colonna usata per il lookup in
           * Results[] resta quella nativa del sensore (nz sotto, invariato);
           * solo la colonna memorizzata per il centroide/pixel viene
           * specchiata, cosi' muovere la mano a destra (dal punto di vista
           * dell'utente) sposta il cursore a destra sullo schermo.
           */
          cluster[cluster_cnt].row = c_row;
          cluster[cluster_cnt].col = (uint8_t)((grid_dim - 1U) - c_col);
          cluster[cluster_cnt].dist_mm = min_dist;
          cluster_cnt++;

          static const int8_t dr[4] = {-1, 1, 0, 0};
          static const int8_t dc[4] = {0, 0, -1, 1};

          for (uint8_t k = 0; k < 4U; k++) {
            int8_t nr = (int8_t)c_row + dr[k];
            int8_t nc = (int8_t)c_col + dc[k];
            if ((nr >= 0) && (nr < (int8_t)grid_dim) && (nc >= 0) && (nc < (int8_t)grid_dim)) {
              uint8_t nz = (uint8_t)(nr * (int8_t)grid_dim + nc);
              uint16_t ndist = Results.distance_mm[VL53L7CX_NB_TARGET_PER_ZONE * nz];
              uint8_t nstat = Results.target_status[VL53L7CX_NB_TARGET_PER_ZONE * nz];

              if (((nstat == 5U) || (nstat == 9U)) && (ndist > 30U) && (ndist < 250U)) {
                int32_t diff = (int32_t)ndist - (int32_t)min_dist;
                if (diff < 0) {
                  diff = -diff;
                }
                if (diff <= (int32_t)NEIGHBOR_DIST_MARGIN_MM) {
                  cluster[cluster_cnt].row = (uint8_t)nr;
                  cluster[cluster_cnt].col = (uint8_t)((grid_dim - 1U) - (uint8_t)nc); /* invert_h, vedi sopra */
                  cluster[cluster_cnt].dist_mm = ndist;
                  cluster_cnt++;
                }
              }
            }
          }

          int32_t row_centroid_q16 = 0, col_centroid_q16 = 0;
          cursor_calculate_centroid(cluster, cluster_cnt, &row_centroid_q16, &col_centroid_q16);

          uint16_t px = 0, py = 0;
          cursor_centroid_to_pixel(row_centroid_q16, col_centroid_q16, grid_dim, &px, &py);

          current_cursor.valid = true;
          current_cursor.x = px;
          current_cursor.y = py;
          current_cursor.dist_mm = min_dist;
        } else {
          current_cursor.valid = false;
          current_cursor.x = 0;
          current_cursor.y = 0;
          current_cursor.dist_mm = 0;
        }
        chMtxUnlock(&cursor_mutex);
      }
    }
  }

  return 0;
}
