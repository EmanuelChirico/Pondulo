/*
 * sd_storage_fixture.c - Mock di sd_storage per i test host.
 *
 * NOTA: Questo file è compilato ESCLUSIVAMENTE nei test host per simulare
 * la presenza di file su scheda SD. Non è linkato nella build del firmware embedded.
 */

#include "sd_storage.h"
#include <string.h>
#include <stdio.h>

bool leggiSD(const char *percorso, char *out_buf, size_t buf_size) {
  if (percorso == NULL || out_buf == NULL || buf_size == 0) {
    return false;
  }

  /* Mock: codice "001" -> Mele Gala */
  if (strcmp(percorso, "/menu/fruttivendolo/articoli/001.json") == 0) {
    const char *mock_data = "{\n"
                            "  \"nome\": \"Mele Gala\",\n"
                            "  \"prezzo_centesimi_al_kg\": 249,\n"
                            "  \"peso_grammi\": 1200\n"
                            "}";
    if (strlen(mock_data) >= buf_size) {
      return false;
    }
    strncpy(out_buf, mock_data, buf_size - 1);
    out_buf[buf_size - 1] = '\0';
    return true;
  }

  /* Mock: codice "042" -> Banane Chiquita */
  if (strcmp(percorso, "/menu/fruttivendolo/articoli/042.json") == 0) {
    const char *mock_data = "{\"nome\":\"Banane Chiquita\",\"prezzo_centesimi_al_kg\":199,\"peso_grammi\":850}";
    if (strlen(mock_data) >= buf_size) {
      return false;
    }
    strncpy(out_buf, mock_data, buf_size - 1);
    out_buf[buf_size - 1] = '\0';
    return true;
  }

  /* Codice "999" o qualunque altro -> non trovato */
  return false;
}

bool scriviSD(const char *percorso, const char *dati, size_t len) {
  (void)percorso;
  (void)dati;
  (void)len;
  return false;
}
