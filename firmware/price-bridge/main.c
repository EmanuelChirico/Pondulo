/*
 * Collegamenti STM32 <-> ESP8266
 *
 * STM32 TX -> ESP8266 RX
 * STM32 RX <- ESP8266 TX
 * CH_EN     -> 3.3 V
 * VCC       -> 3.3 V
 * GND       -> GND
 *
 * Il web server gira sull'ESP8266, programmato tramite Arduino IDE.
 * Questo firmware STM32 riceve sulla SD1 gli eventi generati dal server
 * e mostra messaggi di debug sulla SD2.
 */

#include "ch.h"
#include "hal.h"
#include "chprintf.h"
#include "price_parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GPIO_LED GPIOA
#define LED_PIN  5

#define ESP_LINE_SIZE 192
#define MAX_PRODUCTS PRICE_COUNT
#define PRODUCT_CODE_SIZE 7
#define PRODUCT_NAME_SIZE 41

typedef struct {
    bool used;
    uint32_t id;
    char code[PRODUCT_CODE_SIZE];
    char name[PRODUCT_NAME_SIZE];
    uint32_t priceCents;
} ProductRecord;

static ProductRecord productTable[MAX_PRODUCTS];
static mutex_t productTableMutex;
static uint32_t listVersion;
static bool webReady;
static char espIp[48];
static const char *const productCodes[PRICE_COUNT] = {"0001", "0002", "0003", "0004"};

static BaseSequentialStream *debugStream = (BaseSequentialStream *)&SD2;

static const SerialConfig uartConfig = {
    38400,
    0,
    USART_CR2_STOP1_BITS,
    0
};

static void serial_init(void) {
    /* UART di debug: PA2 TX, PA3 RX. */
    palSetPadMode(GPIOA, 2, PAL_MODE_ALTERNATE(7));
    palSetPadMode(GPIOA, 3, PAL_MODE_ALTERNATE(7));
    sdStart(&SD2, &uartConfig);

    /* UART collegata all'ESP8266: PC4 TX, PC5 RX. */
    palSetPadMode(GPIOC, 4, PAL_MODE_ALTERNATE(7));
    palSetPadMode(GPIOC, 5, PAL_MODE_ALTERNATE(7));
    sdStart(&SD1, &uartConfig);
}

static void control_led(bool state) {
    if (state) {
        palSetPad(GPIO_LED, LED_PIN);
    } else {
        palClearPad(GPIO_LED, LED_PIN);
    }
}

static void copy_text(char *destination,
                      size_t destinationSize,
                      const char *source) {
    if (destinationSize == 0U) {
        return;
    }

    strncpy(destination, source, destinationSize - 1U);
    destination[destinationSize - 1U] = '\0';
}

static int find_product_by_code_locked(const char *code) {
    for (size_t i = 0U; i < MAX_PRODUCTS; i++) {
        if (productTable[i].used &&
            (strcmp(productTable[i].code, code) == 0)) {
            return (int)i;
        }
    }

    return -1;
}

/*
 * Funzione che la logica della bilancia potra' chiamare.
 * Restituisce true e copia il prezzo se il codice PLU e' disponibile.
 */
bool get_product_price_by_code(const char *code, uint32_t *priceCents) {
    int index;
    bool found = false;

    if ((code == NULL) || (priceCents == NULL)) {
        return false;
    }

    chMtxLock(&productTableMutex);
    index = find_product_by_code_locked(code);
    if ((index >= 0) && (productTable[index].priceCents > 0U)) {
        *priceCents = productTable[index].priceCents;
        found = true;
    }
    chMtxUnlock(&productTableMutex);

    return found;
}

static void print_price(uint32_t priceCents) {
    chprintf(debugStream,
             "%lu,%02lu euro/kg",
             (unsigned long)(priceCents / 100UL),
             (unsigned long)(priceCents % 100UL));
}

/* New ESP menu sends PRODUCT_ADD + PRODUCT_CODE for each of the four
 * products. Publish all four together after validating the complete list.
 * The RX thread never blocks on USB debug output. */
static void publish_prices(const uint32_t prices[PRICE_COUNT]) {
    chMtxLock(&productTableMutex);
    for (unsigned i = 0; i < PRICE_COUNT; ++i) {
        productTable[i].used = true;
        productTable[i].id = i + 1U;
        productTable[i].priceCents = prices[i];
        copy_text(productTable[i].name, sizeof(productTable[i].name), price_names[i]);
        copy_text(productTable[i].code, sizeof(productTable[i].code), productCodes[i]);
    }
    ++listVersion;
    chMtxUnlock(&productTableMutex);
    control_led(true);
}

/* Preserve the original boot/IP diagnostics, without treating them as proof
 * that prices have arrived. All serial printing is done in main(). */
static void process_esp_status(const char *line) {
    chMtxLock(&productTableMutex);
    if (strcmp(line, "WEB_SERVER_READY") == 0) webReady = true;
    else if (strncmp(line, "IP_ADDRESS:", 11U) == 0)
        copy_text(espIp, sizeof(espIp), line + 11U);
    chMtxUnlock(&productTableMutex);
}

static THD_WORKING_AREA(waEspReceiver, 1536);
static THD_FUNCTION(thdEspReceiver, arg) {
    (void)arg;
    chRegSetThreadName("ESP8266 receiver");
    price_parser_t parser;
    price_parser_init(&parser);
    uint32_t prices[PRICE_COUNT];
    char line[ESP_LINE_SIZE];
    size_t lineLength = 0;
    bool overflow = false;
    while (true) {
        msg_t value = sdGetTimeout(&SD1, TIME_MS2I(100));
        if (value < MSG_OK) continue;
        char character = (char)value;
        if (price_parser_feed(&parser, character,
              (uint32_t)TIME_I2MS(chVTGetSystemTimeX()), prices)) publish_prices(prices);
        if (character == '\r') continue;
        if (character == '\n') {
            if (!overflow && lineLength) {
                line[lineLength] = '\0';
                process_esp_status(line);
            }
            lineLength = 0;
            overflow = false;
        } else if (!overflow) {
            if ((unsigned char)character < 32U || (unsigned char)character > 126U ||
                lineLength >= sizeof(line) - 1U) overflow = true;
            else line[lineLength++] = character;
        }
    }
}

int main(void) {
    halInit();
    chSysInit();

    chMtxObjectInit(&productTableMutex);
    memset(productTable, 0, sizeof(productTable));

    serial_init();

    palSetPadMode(GPIO_LED, LED_PIN, PAL_MODE_OUTPUT_PUSHPULL);
    control_led(false);

    chprintf(debugStream, "\r\n========================================\r\n");
    chprintf(debugStream, "Avvio interfaccia STM32 - ESP8266\r\n");
    chprintf(debugStream, "UART ESP: SD1 a 38400 baud\r\n");
    chprintf(debugStream, "Debug:    SD2 a 38400 baud\r\n");
    chprintf(debugStream, "Premere SALVA sul nuovo menu ESP: Manzo, Pollo, Maiale, Agnello.\r\n");
    chprintf(debugStream, "========================================\r\n");

    chThdCreateStatic(waEspReceiver,
                      sizeof(waEspReceiver),
                      NORMALPRIO + 1,
                      thdEspReceiver,
                      NULL);

    uint32_t printedVersion = 0;
    bool printedReady = false;
    char printedIp[48] = "";
    while (true) {
        ProductRecord snapshot[PRICE_COUNT];
        char ip[48];
        chMtxLock(&productTableMutex);
        uint32_t version = listVersion;
        bool ready = webReady;
        memcpy(snapshot, productTable, sizeof(snapshot));
        copy_text(ip, sizeof(ip), espIp);
        chMtxUnlock(&productTableMutex);
        if (ready && !printedReady) {
            chprintf(debugStream, "[WEB] Server ESP avviato. Attendo SALVA.\r\n");
            printedReady = true;
        }
        if (ip[0] && strcmp(ip, printedIp) != 0) {
            chprintf(debugStream, "[WEB] Aprire http://%s/\r\n", ip);
            copy_text(printedIp, sizeof(printedIp), ip);
        }
        if (version != printedVersion) {
            chprintf(debugStream, "\r\n[LISTINO OK #%lu] Quattro prezzi ricevuti\r\n",
                     (unsigned long)version);
            for (unsigned i = 0; i < PRICE_COUNT; ++i) {
                chprintf(debugStream, "[PREZZO] %s | PLU %s | ", snapshot[i].name, snapshot[i].code);
                print_price(snapshot[i].priceCents);
                chprintf(debugStream, "\r\n");
            }
            printedVersion = version;
        }
        chThdSleepMilliseconds(100);
    }
}
