#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "sd_storage.h"

static int failures = 0;

#define CHECK(cond) do { \
  if (!(cond)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    failures++; \
  } \
} while (0)

int main(void) {
  printf("=== RUNNING TEST SD FIXTURE & END-TO-END FLOW ===\n");
  char buf[256];
  char nome[32];
  uint16_t prezzo = 0;
  uint16_t peso = 0;

  /* 1. Test code 001 exists in fixture */
  CHECK(leggiSD("/menu/fruttivendolo/articoli/001.json", buf, sizeof(buf)) == true);
  CHECK(parse_articolo_json(buf, nome, sizeof(nome), &prezzo, &peso) == true);
  CHECK(strcmp(nome, "Mele Gala") == 0);
  CHECK(prezzo == 249);
  CHECK(peso == 1200);

  /* 2. Test code 042 exists in fixture */
  CHECK(leggiSD("/menu/fruttivendolo/articoli/042.json", buf, sizeof(buf)) == true);
  CHECK(parse_articolo_json(buf, nome, sizeof(nome), &prezzo, &peso) == true);
  CHECK(strcmp(nome, "Banane Chiquita") == 0);
  CHECK(prezzo == 199);
  CHECK(peso == 850);

  /* 3. Test code 999 not found */
  CHECK(leggiSD("/menu/fruttivendolo/articoli/999.json", buf, sizeof(buf)) == false);

  if (failures == 0) {
    printf("ALL FIXTURE TESTS PASSED.\n");
    return 0;
  } else {
    printf("FIXTURE TESTS FAILED: %d failures\n", failures);
    return 1;
  }
}
