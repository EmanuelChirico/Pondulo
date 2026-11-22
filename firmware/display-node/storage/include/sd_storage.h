#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Stub per la lettura di un file da filesystem SD.
 * In questa versione hardware non è presente interfaccia SD su GFX01M2,
 * pertanto la funzione restituisce sempre false senza accedere all'hardware.
 *
 * @param percorso Percorso assoluto del file secondo FILESYSTEM.md
 * @param out_buf Buffer di destinazione per i dati letti
 * @param buf_size Dimensione massima del buffer out_buf
 * @return false incondizionatamente in questa build stub.
 */
bool leggiSD(const char *percorso, char *out_buf, size_t buf_size);

/**
 * @brief Stub per la scrittura di un file su filesystem SD.
 * Restituisce sempre false in questa versione hardware.
 *
 * @param percorso Percorso assoluto del file secondo FILESYSTEM.md
 * @param dati Buffer con i dati da scrivere
 * @param len Lunghezza dei dati da scrivere
 * @return false incondizionatamente in questa build stub.
 */
bool scriviSD(const char *percorso, const char *dati, size_t len);

/**
 * @brief Parser JSON minimale, puro e difensivo per record articolo.
 * Schema flat atteso da FILESYSTEM.md:
 * {
 *   "nome": "stringa",
 *   "prezzo_centesimi_al_kg": intero,
 *   "peso_grammi": intero
 * }
 *
 * @param json Stringa JSON null-terminata in ingresso
 * @param nome_out Buffer in cui copiare il nome estratto
 * @param nome_out_size Dimensione massima di nome_out (incluso terminatore '\0')
 * @param prezzo_centesimi_al_kg_out Puntatore a uint16_t per il prezzo in centesimi/kg
 * @param peso_grammi_out Puntatore a uint16_t per il peso di default in grammi
 * @return true se tutti i 3 campi sono presenti e validi, false altrimenti.
 */
bool parse_articolo_json(const char *json,
                         char *nome_out,
                         size_t nome_out_size,
                         uint16_t *prezzo_centesimi_al_kg_out,
                         uint16_t *peso_grammi_out);

#ifdef __cplusplus
}
#endif

#endif /* SD_STORAGE_H */
