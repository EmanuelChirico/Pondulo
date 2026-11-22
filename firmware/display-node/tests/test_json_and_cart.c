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

static void test_leggi_scrivi_sd_stub(void) {
  char buf[64];
  CHECK(leggiSD("/menu/fruttivendolo/articoli/001.json", buf, sizeof(buf)) == false);
  CHECK(scriviSD("/menu/fruttivendolo/articoli/001.json", "data", 4) == false);
}

static void test_parse_json_valid(void) {
  const char *json = "{\n"
                     "  \"nome\": \"Mele Gala\",\n"
                     "  \"prezzo_centesimi_al_kg\": 249,\n"
                     "  \"peso_grammi\": 1500\n"
                     "}";
  char nome[32];
  uint16_t prezzo = 0;
  uint16_t peso = 0;

  bool ok = parse_articolo_json(json, nome, sizeof(nome), &prezzo, &peso);
  CHECK(ok == true);
  CHECK(strcmp(nome, "Mele Gala") == 0);
  CHECK(prezzo == 249);
  CHECK(peso == 1500);
}

static void test_parse_json_field_order_and_whitespace(void) {
  const char *json = "{\"peso_grammi\":850,\"nome\":\"Banane Chiquita\",\"prezzo_centesimi_al_kg\":199}";
  char nome[32];
  uint16_t prezzo = 0;
  uint16_t peso = 0;

  bool ok = parse_articolo_json(json, nome, sizeof(nome), &prezzo, &peso);
  CHECK(ok == true);
  CHECK(strcmp(nome, "Banane Chiquita") == 0);
  CHECK(prezzo == 199);
  CHECK(peso == 850);
}

static void test_parse_json_missing_fields(void) {
  char nome[32];
  uint16_t prezzo = 0;
  uint16_t peso = 0;

  /* Missing nome */
  const char *j1 = "{\"prezzo_centesimi_al_kg\": 100, \"peso_grammi\": 500}";
  CHECK(parse_articolo_json(j1, nome, sizeof(nome), &prezzo, &peso) == false);

  /* Missing prezzo */
  const char *j2 = "{\"nome\": \"Arance\", \"peso_grammi\": 500}";
  CHECK(parse_articolo_json(j2, nome, sizeof(nome), &prezzo, &peso) == false);

  /* Missing peso */
  const char *j3 = "{\"nome\": \"Arance\", \"prezzo_centesimi_al_kg\": 100}";
  CHECK(parse_articolo_json(j3, nome, sizeof(nome), &prezzo, &peso) == false);
}

static void test_parse_json_malformed(void) {
  char nome[32];
  uint16_t prezzo = 0;
  uint16_t peso = 0;

  /* Unclosed quote in string */
  const char *j1 = "{\"nome\": \"Arance, \"prezzo_centesimi_al_kg\": 100, \"peso_grammi\": 500}";
  CHECK(parse_articolo_json(j1, nome, sizeof(nome), &prezzo, &peso) == false);

  /* Non-digit in integer */
  const char *j2 = "{\"nome\": \"Arance\", \"prezzo_centesimi_al_kg\": abc, \"peso_grammi\": 500}";
  CHECK(parse_articolo_json(j2, nome, sizeof(nome), &prezzo, &peso) == false);

  /* Integer overflow (> 65535) */
  const char *j3 = "{\"nome\": \"Arance\", \"prezzo_centesimi_al_kg\": 70000, \"peso_grammi\": 500}";
  CHECK(parse_articolo_json(j3, nome, sizeof(nome), &prezzo, &peso) == false);

  /* Null inputs */
  CHECK(parse_articolo_json(NULL, nome, sizeof(nome), &prezzo, &peso) == false);
  CHECK(parse_articolo_json(j1, NULL, sizeof(nome), &prezzo, &peso) == false);
  CHECK(parse_articolo_json(j1, nome, 0, &prezzo, &peso) == false);
  CHECK(parse_articolo_json(j1, nome, sizeof(nome), NULL, &peso) == false);
  CHECK(parse_articolo_json(j1, nome, sizeof(nome), &prezzo, NULL) == false);
}

/* Simulation of cart logic to verify equivalence */
typedef struct {
  char     nome[24];
  uint16_t peso_grammi;
  uint16_t prezzo_centesimi_al_kg;
  uint32_t subtotale_centesimi;
} test_cart_entry_t;

#define TEST_CART_MAX 8
static test_cart_entry_t test_cart[TEST_CART_MAX];
static uint8_t test_cart_count = 0;

static void test_cart_add_record(const char *nome, uint16_t peso_grammi, uint16_t prezzo_centesimi_al_kg) {
  if (nome == NULL || test_cart_count >= TEST_CART_MAX) {
    return;
  }
  test_cart_entry_t *entry = &test_cart[test_cart_count];
  strncpy(entry->nome, nome, sizeof(entry->nome) - 1);
  entry->nome[sizeof(entry->nome) - 1] = '\0';
  entry->peso_grammi = peso_grammi;
  entry->prezzo_centesimi_al_kg = prezzo_centesimi_al_kg;
  entry->subtotale_centesimi = ((uint32_t)entry->peso_grammi * entry->prezzo_centesimi_al_kg) / 1000U;
  test_cart_count++;
}

static void test_cart_logic(void) {
  test_cart_count = 0;

  /* 1. Item from Macelleria (e.g. Manzo: 1890 centesimi/kg, 1000g) */
  test_cart_add_record("Manzo", 1000, 1890);
  CHECK(test_cart_count == 1);
  CHECK(strcmp(test_cart[0].nome, "Manzo") == 0);
  CHECK(test_cart[0].subtotale_centesimi == 1890);

  /* 2. Item from Fruttivendolo (e.g. Mele: 250 centesimi/kg, 1500g) */
  test_cart_add_record("Mele Gala", 1500, 250);
  CHECK(test_cart_count == 2);
  CHECK(strcmp(test_cart[1].nome, "Mele Gala") == 0);
  /* 1500 * 250 / 1000 = 375 centesimi (3.75 EUR) */
  CHECK(test_cart[1].subtotale_centesimi == 375);

  /* 3. Combined Total */
  uint32_t total = 0;
  for (uint8_t i = 0; i < test_cart_count; i++) {
    total += test_cart[i].subtotale_centesimi;
  }
  CHECK(total == (1890 + 375));
}

int main(void) {
  printf("=== RUNNING TEST JSON & CART ===\n");
  test_leggi_scrivi_sd_stub();
  test_parse_json_valid();
  test_parse_json_field_order_and_whitespace();
  test_parse_json_missing_fields();
  test_parse_json_malformed();
  test_cart_logic();

  if (failures == 0) {
    printf("ALL TESTS PASSED.\n");
    return 0;
  } else {
    printf("TESTS FAILED: %d failures\n", failures);
    return 1;
  }
}
