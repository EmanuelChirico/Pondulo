/*
 * sd_storage.c - Stub per l'interfaccia filesystem SD e parser JSON minimale.
 *
 * NOTA PER IL COLLEGA:
 * Su questa shield (X-NUCLEO-GFX01M2) non è presente uno slot microSD fisico.
 * Le funzioni leggiSD() e scriviSD() sono fornite come stub intenzionali
 * che ritornano sempre false senza accedere ad alcun hardware (SPI/FatFS).
 * Quando il modulo hardware esterno SD sarà collegato, sostituire
 * l'implementazione di leggiSD() e scriviSD() con le chiamate FatFS reali,
 * mantenendo inalterate le firme e il contratto di percorsi (FILESYSTEM.md).
 *
 * DATI DI TEST TEMPORANEI (2026-09-04): leggiSD() qui sotto risponde a due
 * percorsi fissi con dati finti (stessi due usati in tests/mock/
 * sd_storage_fixture.c) SOLO per poter esercitare il flusso Fruttivendolo
 * dal vivo su hardware reale prima che la SD vera sia collegata. RIMUOVERE
 * questo blocco (tornare a return false incondizionato) non appena la SD
 * vera è collegata, o comunque prima che questo firmware sia considerato
 * definitivo - non è il contratto stub concordato con il collega, è solo
 * un aiuto per il test manuale di oggi.
 */

#include "sd_storage.h"
#include <ctype.h>
#include <string.h>

#if defined(__GNUC__)
__attribute__((weak))
#endif
bool leggiSD(const char *percorso, char *out_buf, size_t buf_size) {
  if ((percorso != NULL) && (out_buf != NULL) && (buf_size > 0U)) {
    /* DATO DI TEST TEMPORANEO - vedi nota in cima al file. */
    if (strcmp(percorso, "/menu/fruttivendolo/articoli/001.json") == 0) {
      static const char test_001[] =
        "{\"nome\":\"Mele Gala\",\"prezzo_centesimi_al_kg\":249,\"peso_grammi\":1200}";
      if (strlen(test_001) < buf_size) {
        strcpy(out_buf, test_001);
        return true;
      }
    } else if (strcmp(percorso, "/menu/fruttivendolo/articoli/042.json") == 0) {
      static const char test_042[] =
        "{\"nome\":\"Banane Chiquita\",\"prezzo_centesimi_al_kg\":199,\"peso_grammi\":850}";
      if (strlen(test_042) < buf_size) {
        strcpy(out_buf, test_042);
        return true;
      }
    }
  }
  /* Stub intenzionale: nessun accesso hardware, ritorna sempre false per
   * qualunque altro percorso. */
  return false;
}

#if defined(__GNUC__)
__attribute__((weak))
#endif
bool scriviSD(const char *percorso, const char *dati, size_t len) {
  (void)percorso;
  (void)dati;
  (void)len;
  /* Stub intenzionale: nessun accesso hardware, ritorna sempre false */
  return false;
}

/* Helper per saltare spazi e separatori */
static const char *skip_whitespace(const char *p) {
  while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
    p++;
  }
  return p;
}

/* Helper per trovare la chiave JSON '"key"' e posizionarsi dopo ':' */
static const char *find_json_value(const char *json, const char *key) {
  size_t key_len = strlen(key);
  const char *p = json;

  while (*p) {
    if (*p == '"') {
      p++;
      if (strncmp(p, key, key_len) == 0 && p[key_len] == '"') {
        p += key_len + 1;
        p = skip_whitespace(p);
        if (*p == ':') {
          p++;
          return skip_whitespace(p);
        }
      }
    } else {
      p++;
    }
  }
  return NULL;
}

static bool parse_json_string(const char *json, const char *key, char *out, size_t out_size) {
  const char *p = find_json_value(json, key);
  if (p == NULL || *p != '"' || out == NULL || out_size == 0) {
    return false;
  }
  p++; /* Salta '"' iniziale */

  size_t idx = 0;
  while (*p && *p != '"') {
    if (*p == '\\') {
      p++;
      if (*p == '\0') {
        return false;
      }
    }
    if (idx + 1 >= out_size) {
      /* Stringa troppo lunga per il buffer fornito */
      return false;
    }
    out[idx++] = *p++;
  }

  if (*p != '"' || idx == 0) {
    /* Manca la virgoletta di chiusura oppure stringa vuota */
    return false;
  }
  p++; /* Salta '"' finale */
  p = skip_whitespace(p);
  if (*p != ',' && *p != '}' && *p != '\0') {
    return false;
  }

  out[idx] = '\0';
  return true;
}

static bool parse_json_uint16(const char *json, const char *key, uint16_t *out) {
  const char *p = find_json_value(json, key);
  if (p == NULL || !isdigit((unsigned char)*p) || out == NULL) {
    return false;
  }

  uint32_t val = 0;
  while (*p && isdigit((unsigned char)*p)) {
    val = val * 10U + (uint32_t)(*p - '0');
    if (val > 65535U) {
      /* Overflow uint16_t */
      return false;
    }
    p++;
  }

  /* Verifica delimitatore valido dopo il numero */
  if (*p != '\0' && *p != ',' && *p != '}' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
    return false;
  }

  *out = (uint16_t)val;
  return true;
}

bool parse_articolo_json(const char *json,
                         char *nome_out,
                         size_t nome_out_size,
                         uint16_t *prezzo_centesimi_al_kg_out,
                         uint16_t *peso_grammi_out) {
  if (json == NULL || nome_out == NULL || nome_out_size == 0 ||
      prezzo_centesimi_al_kg_out == NULL || peso_grammi_out == NULL) {
    return false;
  }

  if (!parse_json_string(json, "nome", nome_out, nome_out_size)) {
    return false;
  }

  if (!parse_json_uint16(json, "prezzo_centesimi_al_kg", prezzo_centesimi_al_kg_out)) {
    return false;
  }

  if (!parse_json_uint16(json, "peso_grammi", peso_grammi_out)) {
    return false;
  }

  return true;
}
