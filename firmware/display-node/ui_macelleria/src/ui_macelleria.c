/*
 * ui_macelleria - see include/ui_macelleria.h for the module contract.
 *
 * Screens are drawn once on entry, then kept up to date incrementally:
 * uiMacelleriaTick() only repaints the small regions that actually changed
 * (a hit-region losing its dwell progress bar, the cursor dot moving) so a
 * ~30Hz tick doesn't require redrawing the whole 320x240 panel over SPI.
 */

#include "ui_macelleria.h"
#include "cursor_glide.h"
#include "hal.h"
#include "gfx.h"
#include "mp3_player.h"
#include <stdio.h>
#include <string.h>

#define DWELL_MS            1500U
#define CURSOR_RADIUS       6
#define CURSOR_BOX_DIM      ((CURSOR_RADIUS + 1) * 2)   /* save/restore box side, px */

/* Serializes ALL gdisp/SPI access between the main thread (screen
 * transitions, via uiMacelleriaHandleGesture) and UiTickThread (dwell
 * progress + cursor overlay + status line, via uiMacelleriaTick) - both
 * threads call into gdisp independently with no other coordination. A
 * large multi-packet operation (e.g. a full-screen fill) from one thread
 * getting interleaved on the SPI bus with a small draw from the other
 * corrupts the low-level streaming transaction - observed as a full-screen
 * fill landing as only a thin stripe. Held for the whole body of each
 * public entry point below, not just individual draw calls, so a screen
 * transition can't be interrupted partway through by a tick (or vice
 * versa). */
static mutex_t gdisp_mutex;
#define MAX_REGIONS         16U

#define ACTION_CANCELLA     100
#define ACTION_CONFERMA     101
#define ACTION_NUOVO        200
#define ID_HOME_CLINICO       300
#define ID_HOME_MACELLERIA    301
#define ID_HOME_CASA          303 /* Riservato, vedi FILESYSTEM.md - fuori scope ora */
#define ACTION_CARRELLO         400
#define ACTION_STAMPA           401
#define ACTION_MOLTIPLICATORE   402 /* dwell/push on the price/weight block opens the quantity keypad */

typedef enum {
  MAC_STATE_HOME = 0,
  MAC_STATE_CLINICO,          /* chart/anamnesi sub-screens, see clinico_screen_idx */
  /* Dormant - kept intact for when a full Buongiorno intro is reactivated
   * ahead of the carousel. */
  MAC_STATE_BUONGIORNO,
  MAC_STATE_CAROUSEL,         /* MACELLERIA: grid item browse view */
  MAC_STATE_BILANCIA,         /* MACELLERIA: scale readout - weight x price multiplier */
  MAC_STATE_MOLTIPLICATORE,   /* MACELLERIA: quantity keypad (0-9, CANC, OK) - NOT a price editor */
  MAC_STATE_RECEIPT           /* Multi-item receipt screen */
} mac_state_t;

typedef struct {
  int16_t x0, y0, x1, y1; /* box: [x0,x1) x [y0,y1) */
  int     id;
} region_t;

/* Exactly 4, matching the 2x2 grid carousel layout below. */
static const carousel_item_t default_items[] = {
  { "Manzo",    1890 },
  { "Pollo",     690 },
  { "Maiale",    990 },
  { "Agnello",  2290 },
};
#define DEFAULT_ITEM_COUNT ((uint8_t)(sizeof(default_items) / sizeof(default_items[0])))

static font_t fontTitle; /* DejaVuSans24 */
static font_t fontBody;  /* DejaVuSans16 */
static font_t fontSmall; /* DejaVuSans12 */

static cursor_glide_t cursor_glide;

/* Centralized UI Theme (Step D.6 of PLAN.md) */
typedef struct {
  color_t bg_screen;
  color_t btn_bg_normal;
  color_t btn_bg_primary;
  color_t btn_bg_danger;
  color_t btn_border_normal;
  color_t btn_border_highlight;
  color_t text_primary;
  color_t text_muted;
  color_t text_accent;
  color_t dwell_bar;
  color_t cursor_fill;
  color_t cursor_outline;
} ui_theme_t;

static const ui_theme_t ui_theme = {
  .bg_screen             = Black,
  .btn_bg_normal         = HTML2COLOR(0x1E293B), /* Modern dark slate card (Tailwind Slate 800) */
  .btn_bg_primary        = HTML2COLOR(0x0284C7), /* Vibrant Sky / Teal accent (Tailwind Sky 600) */
  .btn_bg_danger         = HTML2COLOR(0xB91C1C), /* Vibrant Crimson / Red (Tailwind Red 700) */
  .btn_border_normal     = HTML2COLOR(0x475569), /* Slate border (Tailwind Slate 600) */
  .btn_border_highlight  = Yellow,               /* Focus / Hover Highlight */
  .text_primary          = White,
  .text_muted            = HTML2COLOR(0x94A3B8), /* Light slate / Silver text (Tailwind Slate 400) */
  .text_accent           = Yellow,
  .dwell_bar             = Yellow,
  .cursor_fill           = Yellow,
  .cursor_outline        = White
};

static mac_state_t state = MAC_STATE_HOME;

/* Home entries table (data-driven 2x2 grid for N entries) */
typedef struct {
  const char *label;
  int         id;
  color_t     bg_color;
} home_entry_t;

static const home_entry_t home_entries[] = {
  { "CLINICO",       ID_HOME_CLINICO,       HTML2COLOR(0x0284C7) }, /* Tailwind Sky 600 */
  { "MACELLERIA",    ID_HOME_MACELLERIA,    HTML2COLOR(0x1E293B) }  /* Tailwind Slate 800 */
};
#define HOME_ENTRY_COUNT ((uint8_t)(sizeof(home_entries) / sizeof(home_entries[0])))

static carousel_item_t items[CAROUSEL_MAX_ITEMS];
static uint8_t item_count;
static uint8_t selected_item = 0xFFU;
static uint8_t carousel_cursor_idx = 0; /* which item the swipe-through carousel is showing */
static uint8_t home_highlight = 0; /* gesture-driven pick on Home (0..HOME_ENTRY_COUNT-1), alongside dwell */
static bool home_gesture_nav_active = false; /* true only after explicit SWIPE gesture on Home */
static bool carousel_gesture_nav_active = false; /* true only after explicit SWIPE gesture on Carousel */

/* CLINICO: 0 = cartella clinica (parametri vitali), 1 = anamnesi */
static uint8_t clinico_screen_idx = 0;

#define PATIENT_COUNT 3U
typedef struct {
  const char *nome;
  uint16_t    fc_bpm;
  uint16_t    pa_sistolica;
  uint16_t    pa_diastolica;
  uint16_t    spo2_percento;
  uint16_t    temp_decimi_c;
  /* Anagrafica/anamnesi estesa (2026-09-04), mostrata nella seconda
   * sotto-schermata CLINICO ("Anamnesi", clinico_screen_idx == 1). */
  uint8_t     eta_anni;
  char        sesso;             /* 'M' o 'F' */
  const char *gruppo_sanguigno;
  const char *allergie;
  const char *terapia_in_corso;
  const char *reparto;           /* ultimo reparto/visita */
  const char *note_medico;
} patient_t;

static const patient_t patients[PATIENT_COUNT] = {
  { "Mario Rossi",    72, 120, 80, 98, 367,
    58, 'M', "0+",  "Penicillina",
    "Ramipril 5mg/die", "Cardiologia - 20/08/2026",
    "Controllo di routine, pressione stabile." },
  { "Laura Bianchi",  84, 135, 85, 96, 374,
    45, 'F', "A-",  "Nessuna nota",
    "Nessuna", "Pronto Soccorso - 02/09/2026",
    "Accesso per dolore toracico, ECG nella norma." },
  { "Giuseppe Verdi", 65, 115, 75, 99, 365,
    71, 'M', "AB+", "Lattice",
    "Metformina 850mg x2/die", "Diabetologia - 28/08/2026",
    "Glicemia a digiuno in miglioramento." }
};
static uint8_t current_patient_idx = 0;

static region_t regions[MAX_REGIONS];
static uint8_t region_count;

static int       dwell_target_id = -1;
static bool      dwell_active = false;
static systime_t dwell_start_time;

/* Default 0g per item until a real reading arrives from the scale (a
 * newly-selected item shows 0 until it's actually weighed, not a fake
 * placeholder weight).
 * item_weight_grams[selected_item] is the hook point for the live load-cell
 * reading, fed in by uiMacelleriaSetLiveWeight() below. That reading comes
 * from a SEPARATE Nucleo-G474RE board (load cell + HX711 + 7-segment
 * display) as an analog voltage on PA4, read by main.c's weight_adc module
 * (see weight_adc.h for the full rationale, including why two earlier
 * approaches - a software-UART serial link, then a PWM link - were tried
 * and replaced). */
static uint16_t item_weight_grams[CAROUSEL_MAX_ITEMS];

static inline uint16_t bilancia_get_weight_grams(uint8_t item_idx) {
  if (item_idx < CAROUSEL_MAX_ITEMS) {
    return item_weight_grams[item_idx];
  }
  return 1000U;
}

/* Public hook for main.c's weight_adc reading (transport-agnostic on
 * purpose - this file has no idea weight_adc.h even exists, matching how
 * uiMacelleriaTick() takes x/y as plain parameters instead of depending on
 * xy_link.h).
 *
 * Gated on state == MAC_STATE_BILANCIA on purpose (2026-09-05): main.c calls
 * this unconditionally every UI tick, regardless of what screen is showing.
 * Without this gate, the scale's live reading kept getting mirrored into
 * item_weight_grams[selected_item] in the background even while Home/the
 * carousel was on screen - selected_item still pointed at whatever item was
 * last opened, so browsing the carousel silently overwrote that item's
 * weight with whatever happened to be on the physical scale at that
 * moment, with nothing forcing a resync when a *different* item was opened
 * next (bilancia_last_drawn_grams gets reset in enterBilancia(), but
 * item_weight_grams[] itself does not - it only reads back whatever the
 * background writes left there). Restricting writes to "the Bilancia
 * screen is actually the one on screen right now" means a freshly opened
 * item always starts from its own real last-known weight (0 the first
 * time, see the defaults above) and only updates from the scale while the
 * operator can actually see it happening. */
void uiMacelleriaSetLiveWeight(uint16_t grams) {
  chMtxLock(&gdisp_mutex);
  if (state == MAC_STATE_BILANCIA && selected_item < item_count) {
    item_weight_grams[selected_item] = grams;
  }
  chMtxUnlock(&gdisp_mutex);
}

/* Last weight value drawn on the Bilancia screen (2026-09-05) - compared
 * every tick against the live reading so the on-screen weight/total follow
 * item_weight_grams[] automatically as soon as something updates it,
 * instead of only ever being drawn once on screen entry. 0xFFFF is not a
 * realistic gram value (nothing on a butcher's counter weighs 65.5kg),
 * used here as "force a draw" so the very first tick after enterBilancia()
 * doesn't skip drawing just because it happens to match a stale value. */
static uint16_t bilancia_last_drawn_grams = 0xFFFFU;

/* Redraw deadband, grams (2026-09-05, retuned 2026-09-06 in this DEMO7
 * experiment): an analog voltage read over an unshielded breadboard jumper
 * has measurement noise even when the physical weight is perfectly stable.
 * Without this, that noise alone made current_grams tick up/down almost
 * every UI tick, triggering a redraw on every single change - visibly
 * flickering/reloading constantly, reported as very distracting to read.
 * weight_adc_get_grams() now applies an EMA filter (see weight_adc.c) that
 * should keep steady-state noise well under this threshold on its own;
 * this was nudged from 3 to 5 as a second, independent margin on top of
 * that filter - not a replacement for it. Only redrawing once the reading
 * moves by more than this threshold keeps the display reacting to real
 * weight changes while ignoring noise-level wiggle. */
#define BILANCIA_REDRAW_DEADBAND_G 5U

/* Quantity/multiplier for the item currently on the Bilancia screen
 * (2026-09-05) - e.g. "2" for two identical weighed pieces. NOT a price
 * override (price comes from the WiFi price-list link, see FILESYSTEM.md) -
 * multiplies the scale weight itself before pricing, reset to 1 every time
 * a fresh item is selected (see enterBilancia()). */
static uint8_t bilancia_multiplier = 1U;

/* Single-portion weight (from the scale) x bilancia_multiplier, clamped to
 * uint16_t - grams * a two-digit multiplier can exceed 65535 (e.g. 1000g x
 * 99), which would silently wrap without this. */
static inline uint16_t bilancia_get_total_grams(uint8_t item_idx) {
  uint32_t total = (uint32_t)bilancia_get_weight_grams(item_idx) * bilancia_multiplier;
  return (total > 65535U) ? 65535U : (uint16_t)total;
}

/* Cart structures (RAM-only session storage) */
typedef struct {
  char     nome[CAROUSEL_ITEM_NAME_LEN];
  uint16_t peso_grammi;
  uint16_t prezzo_centesimi_al_kg;
  uint32_t subtotale_centesimi;
} cart_entry_t;

#define CART_MAX_ENTRIES 8U
static cart_entry_t cart[CART_MAX_ENTRIES];
static uint8_t      cart_count = 0;

static cart_entry_t receipt_cart[CART_MAX_ENTRIES];
static uint8_t      receipt_cart_count = 0;

/* Generalization: cart_add_record is the single source of truth for cart append & pricing */
static void cart_add_record(const char *nome, uint16_t peso_grammi, uint16_t prezzo_centesimi_al_kg) {
  if (nome == NULL || cart_count >= CART_MAX_ENTRIES) {
    return;
  }
  cart_entry_t *entry = &cart[cart_count];
  strncpy(entry->nome, nome, CAROUSEL_ITEM_NAME_LEN - 1U);
  entry->nome[CAROUSEL_ITEM_NAME_LEN - 1U] = '\0';
  entry->peso_grammi = peso_grammi;
  entry->prezzo_centesimi_al_kg = prezzo_centesimi_al_kg;
  entry->subtotale_centesimi = ((uint32_t)entry->peso_grammi * entry->prezzo_centesimi_al_kg) / 1000U;
  cart_count++;
}

static void cart_add_item(uint8_t item_idx) {
  if (item_idx >= item_count) {
    return;
  }
  cart_add_record(items[item_idx].nome,
                  bilancia_get_total_grams(item_idx),
                  items[item_idx].prezzo_centesimi_al_kg);
}

static bool     cursor_drawn = false;
static uint16_t cursor_last_x, cursor_last_y;

/* Stationary-cursor flicker fix (2026-09-05): restoreCursorIfDrawn() (see
 * below) erases the cursor's last position by redrawing whatever screen
 * content was under its clip box, every single tick that a valid pointer
 * is present - regardless of whether the pointer actually moved. A hand
 * held still to READ a content-heavy screen (e.g. the receipt/scontrino)
 * therefore forced a full erase+redraw of that content on every ~20ms
 * tick forever, visible as continuous, distracting flicker - completely
 * unrelated to the weight link (whose own live-weight redraw is already
 * gated by BILANCIA_REDRAW_DEADBAND_G above and only ever touches
 * MAC_STATE_BILANCIA, not the receipt).
 * Fix: when the raw incoming (x,y) hasn't moved by more than a couple of
 * pixels since last tick, skip the whole restore/redraw-content/redraw-
 * cursor cycle - the screen is already showing the right thing, so there
 * is nothing to erase or repaint. Gated on dwell_target_id < 0 (see its
 * use just below) so this never suppresses the dwell progress-bar
 * animation, which legitimately needs a fresh redraw every tick while the
 * hand is held still over a selectable button - the exact opposite case
 * from "just resting there reading". */
#define CURSOR_STATIONARY_TOLERANCE_PX 2U
static uint16_t stationary_ref_x, stationary_ref_y;
static bool     stationary_have_ref = false;

/* forward declarations - screens reference each other and the tick function
 * dispatches into all of them. */
static void drawRegionStatic(uint8_t idx);
static void enterHome(void);
static void drawCarouselGrid(void);
static void enterCarousel(void);
static void returnToCarousel(void);
static void enterBilancia(void);
static void drawBilanciaButton(uint8_t idx);
static void drawBilanciaInfo(void);
static void drawBilanciaDynamicFields(void);
static void enterMoltiplicatore(void);
static void drawQtyKey(uint8_t idx);
static void drawQtyReadout(void);
static void returnToBilanciaFromQty(void);
static void enterReceipt(void);
static void drawReceiptDetails(void);
static int  hitTest(uint16_t x, uint16_t y);
static int  regionIndexForId(int id);
static void fireSelection(int id);
static void drawDwellProgress(uint8_t idx);
static void restoreCursorIfDrawn(void);
static void saveAndDrawCursor(uint16_t x, uint16_t y);
static void drawHomeTitle(void);
static void enterClinico(void);
static void drawClinicoScreen(void);
static void drawChartScreen(void);
static void drawPatientDetailsScreen(void);

/* ---- Home (entry point) ---------------------------------------------------- */

/* Single-column vertical stack (CLINICO on top, MACELLERIA below, per
 * home_entries[] order, 2026-09-05) - was a 2x2 grid, left over from when
 * there were up to 4 entries; with only 2 today a full-width stack reads
 * better than two narrow side-by-side tiles. */
static void buildHomeRegions(void) {
  gCoord w = gdispGetWidth();
  gCoord h = gdispGetHeight();
  gCoord top = 44;
  gCoord cellH = (gCoord)((h - top - 10) / HOME_ENTRY_COUNT);

  region_count = 0;
  for (uint8_t i = 0; i < HOME_ENTRY_COUNT; i++) {
    region_t *r = &regions[region_count];
    r->x0 = 10;
    r->x1 = (int16_t)(w - 10);
    r->y0 = (int16_t)(top + i * cellH + 6);
    r->y1 = (int16_t)(top + (i + 1) * cellH - 6);
    r->id = home_entries[i].id;
    region_count++;
  }
}

static void drawHomeButton(uint8_t idx) {
  if (idx >= region_count || idx >= HOME_ENTRY_COUNT) {
    return;
  }
  const region_t *r = &regions[idx];
  const home_entry_t *entry = &home_entries[idx];
  bool highlighted;

  if (dwell_target_id >= 0) {
    highlighted = (r->id == dwell_target_id);
  } else if (home_gesture_nav_active) {
    highlighted = (idx == home_highlight);
  } else {
    highlighted = false;
  }

  color_t bg = entry->bg_color;
  if (highlighted) {
    bg = (r->id == ID_HOME_CLINICO) ? HTML2COLOR(0x0369A1) : HTML2COLOR(0x334155);
  }

  gdispFillArea(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0), bg);

  color_t border_col = highlighted ? ui_theme.btn_border_highlight : ui_theme.btn_border_normal;
  gdispDrawBox(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0), border_col);
  gdispDrawBox((gCoord)(r->x0 + 1), (gCoord)(r->y0 + 1),
               (gCoord)(r->x1 - r->x0 - 2), (gCoord)(r->y1 - r->y0 - 2), border_col);

  font_t f = (strlen(entry->label) > 11U) ? fontSmall : fontBody;
  gdispDrawStringBox(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0),
                     entry->label, f, ui_theme.text_primary, justifyCenter);
}

static void drawHomeTitle(void) {
  gdispFillArea(0, 0, gdispGetWidth(), 36, ui_theme.bg_screen);
  gdispDrawStringBox(0, 10, gdispGetWidth(), 24, "Seleziona un contesto", fontBody, ui_theme.text_muted, justifyCenter);
}

static void enterHome(void) {
  state = MAC_STATE_HOME;
  home_gesture_nav_active = false;
  dwell_active = false;
  dwell_target_id = -1;
  cursor_drawn = false;
  cursor_glide_reset(&cursor_glide);

  gdispFillArea(0, 0, gdispGetWidth(), gdispGetHeight(), ui_theme.bg_screen);
  buildHomeRegions();
  for (uint8_t i = 0; i < region_count; i++) {
    drawRegionStatic(i);
  }
  drawHomeTitle();
}

/* ---- CLINICO: cartella clinica + anamnesi ---------------------------------- */

static void drawChartScreen(void) {
  gCoord w = gdispGetWidth();
  const patient_t *p = &patients[current_patient_idx];
  char line[48];

  gdispDrawStringBox(0, 4, w, 24, "Cartella clinica", fontTitle, ui_theme.text_primary, justifyCenter);

  snprintf(line, sizeof(line), "Paziente: %s", p->nome);
  gdispDrawStringBox(0, 32, w, 20, line, fontBody, ui_theme.text_accent, justifyCenter);

  snprintf(line, sizeof(line), "FC: %u bpm", (unsigned)p->fc_bpm);
  gdispDrawStringBox(0, 58, w, 20, line, fontBody, ui_theme.text_primary, justifyCenter);

  snprintf(line, sizeof(line), "PA: %u/%u mmHg", (unsigned)p->pa_sistolica, (unsigned)p->pa_diastolica);
  gdispDrawStringBox(0, 82, w, 20, line, fontBody, ui_theme.text_primary, justifyCenter);

  snprintf(line, sizeof(line), "SpO2: %u%%", (unsigned)p->spo2_percento);
  gdispDrawStringBox(0, 106, w, 20, line, fontBody, ui_theme.text_primary, justifyCenter);

  snprintf(line, sizeof(line), "Temp: %u.%u C",
           (unsigned)(p->temp_decimi_c / 10U), (unsigned)(p->temp_decimi_c % 10U));
  gdispDrawStringBox(0, 130, w, 20, line, fontBody, ui_theme.text_primary, justifyCenter);

  gdispDrawStringBox(0, (gCoord)(gdispGetHeight() - 34), w, 16,
                      "Swipe su: cambia paziente", fontSmall, ui_theme.text_muted, justifyCenter);
  gdispDrawStringBox(0, (gCoord)(gdispGetHeight() - 18), w, 16,
                      "Swipe sx: Home | dx: Anamnesi", fontSmall, ui_theme.text_muted, justifyCenter);
}

/* Third CLINICO sub-screen (2026-09-04): extended patient record (anagraphic
 * + allergies/therapy/department/notes) beyond the vital signs shown in
 * drawChartScreen(). All fields are drawn left-justified, single line each -
 * kept deliberately short in patients[] since this codebase never wraps
 * text (see other screens: every string is a single gdispDrawStringBox
 * call, no multi-line layout engine). */
static void drawPatientDetailsScreen(void) {
  gCoord w = gdispGetWidth();
  const patient_t *p = &patients[current_patient_idx];
  char line[56];

  gdispDrawStringBox(0, 4, w, 24, "Anamnesi", fontTitle, ui_theme.text_primary, justifyCenter);

  snprintf(line, sizeof(line), "Paziente: %s", p->nome);
  gdispDrawStringBox(0, 30, w, 20, line, fontBody, ui_theme.text_accent, justifyCenter);

  snprintf(line, sizeof(line), "Eta: %u  Sesso: %c  Gruppo: %s",
           (unsigned)p->eta_anni, p->sesso, p->gruppo_sanguigno);
  gdispDrawStringBox(8, 54, (gCoord)(w - 16), 16, line, fontSmall, ui_theme.text_primary, justifyLeft);

  snprintf(line, sizeof(line), "Allergie: %s", p->allergie);
  gdispDrawStringBox(8, 74, (gCoord)(w - 16), 16, line, fontSmall, ui_theme.text_primary, justifyLeft);

  snprintf(line, sizeof(line), "Terapia: %s", p->terapia_in_corso);
  gdispDrawStringBox(8, 94, (gCoord)(w - 16), 16, line, fontSmall, ui_theme.text_primary, justifyLeft);

  snprintf(line, sizeof(line), "Reparto: %s", p->reparto);
  gdispDrawStringBox(8, 114, (gCoord)(w - 16), 16, line, fontSmall, ui_theme.text_primary, justifyLeft);

  snprintf(line, sizeof(line), "Note: %s", p->note_medico);
  gdispDrawStringBox(8, 134, (gCoord)(w - 16), 16, line, fontSmall, ui_theme.text_muted, justifyLeft);

  gdispDrawStringBox(0, (gCoord)(gdispGetHeight() - 34), w, 16,
                      "Swipe su: cambia paziente", fontSmall, ui_theme.text_muted, justifyCenter);
  gdispDrawStringBox(0, (gCoord)(gdispGetHeight() - 18), w, 16,
                      "Swipe sx: Home | dx: Cartella", fontSmall, ui_theme.text_muted, justifyCenter);
}

static void drawClinicoScreen(void) {
  gdispFillArea(0, 0, gdispGetWidth(), gdispGetHeight(), ui_theme.bg_screen);
  if (clinico_screen_idx == 0U) {
    drawChartScreen();
  } else {
    drawPatientDetailsScreen();
  }
}

static void enterClinico(void) {
  state = MAC_STATE_CLINICO;
  clinico_screen_idx = 0;
  current_patient_idx = 0;
  region_count = 0; /* gesture-driven for now: SWIPE toggles screen */
  dwell_active = false;
  dwell_target_id = -1;
  cursor_drawn = false;
  cursor_glide_reset(&cursor_glide);
  drawClinicoScreen();
}

/* ---- MACELLERIA: swipe-through item carousel ------------------------------- */

/* 2x2 grid, one region per item (up to 4 - matches default_items exactly).
 * Each tile is independently dwell-selectable; carousel_cursor_idx is only
 * the gesture-driven highlight (SWIPE moves it, PUSH confirms it) used for
 * testing without a pointer - dwelling directly on any tile selects that
 * item regardless of which one is currently highlighted.
 *
 * Deliberate central dead zone (user request, 2026-09-04): the 4 tiles are
 * pushed toward the corners, leaving a cross-shaped rest area in the middle
 * (~18% of width, ~17% of height) where hovering hits no region at all -
 * a safe place to park the hand between selections without triggering a
 * dwell by accident. Only this dense 2x2 grid gets this treatment (not the
 * 2-button Home/Bilancia screens, not the numeric keypad - explicit user
 * scoping). */
static void buildCarouselRegions(void) {
  gCoord w = gdispGetWidth();
  gCoord h = gdispGetHeight();
  gCoord edgeMargin = 12;
  gCoord centerGapX = (gCoord)(w * 18 / 100); /* ~18% central dead zone, horizontal */
  gCoord centerGapY = (gCoord)(h * 17 / 100); /* ~17% central dead zone, vertical */
  gCoord cellW = (gCoord)((w - (2 * edgeMargin) - centerGapX) / 2);
  gCoord cellH = (gCoord)((h - (2 * edgeMargin) - centerGapY) / 2);
  uint8_t n = (item_count < 4U) ? item_count : 4U;
  uint8_t i;

  region_count = n;
  for (i = 0; i < n; i++) {
    uint8_t row = i / 2U, col = i % 2U;
    gCoord x0 = (gCoord)(edgeMargin + col * (cellW + centerGapX));
    gCoord y0 = (gCoord)(edgeMargin + row * (cellH + centerGapY));
    regions[i].x0 = (int16_t)x0;
    regions[i].x1 = (int16_t)(x0 + cellW);
    regions[i].y0 = (int16_t)y0;
    regions[i].y1 = (int16_t)(y0 + cellH);
    regions[i].id = i;
  }
}

static void drawCarouselItem(uint8_t idx) {
  const region_t *r = &regions[idx];
  char line[32];
  bool highlighted;

  if (dwell_target_id >= 0) {
    highlighted = (r->id == dwell_target_id);
  } else if (carousel_gesture_nav_active) {
    highlighted = (idx == carousel_cursor_idx);
  } else {
    highlighted = false;
  }

  color_t bg = highlighted ? HTML2COLOR(0x0369A1) : ui_theme.btn_bg_normal;
  gCoord midY = (gCoord)(r->y0 + (r->y1 - r->y0) / 2);

  gdispFillArea(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0), bg);

  color_t border_col = highlighted ? ui_theme.btn_border_highlight : ui_theme.btn_border_normal;
  gdispDrawBox(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0), border_col);
  gdispDrawBox((gCoord)(r->x0 + 1), (gCoord)(r->y0 + 1),
               (gCoord)(r->x1 - r->x0 - 2), (gCoord)(r->y1 - r->y0 - 2), border_col);

  snprintf(line, sizeof(line), "%s", items[idx].nome);
  gdispDrawStringBox(r->x0, (gCoord)(midY - 22), (gCoord)(r->x1 - r->x0), 22,
                      line, fontBody, ui_theme.text_primary, justifyCenter);

  snprintf(line, sizeof(line), "%u.%02u EUR/kg",
           (unsigned)(items[idx].prezzo_centesimi_al_kg / 100U),
           (unsigned)(items[idx].prezzo_centesimi_al_kg % 100U));
  gdispDrawStringBox(r->x0, (gCoord)(midY + 2), (gCoord)(r->x1 - r->x0), 20,
                      line, fontSmall, ui_theme.text_muted, justifyCenter);
}

static void drawCarouselGrid(void) {
  gdispFillArea(0, 0, gdispGetWidth(), gdispGetHeight(), ui_theme.bg_screen);
  buildCarouselRegions();
  for (uint8_t i = 0; i < region_count; i++) {
    drawRegionStatic(i);
  }
}

static void enterCarousel(void) {
  state = MAC_STATE_CAROUSEL;
  carousel_gesture_nav_active = false;
  carousel_cursor_idx = 0;
  selected_item = 0xFFU;
  dwell_active = false;
  dwell_target_id = -1;
  cursor_drawn = false;
  cursor_glide_reset(&cursor_glide);
  drawCarouselGrid();
}

/* Same as enterCarousel() but keeps the current highlight - used when
 * coming back from the keypad/receipt rather than freshly from Home. */
static void returnToCarousel(void) {
  state = MAC_STATE_CAROUSEL;
  carousel_gesture_nav_active = false;
  selected_item = 0xFFU;
  dwell_active = false;
  dwell_target_id = -1;
  cursor_drawn = false;
  cursor_glide_reset(&cursor_glide);
  drawCarouselGrid();
}

/* ---- Scale screen (Bilancia) ---------------------------------------------- */

static void buildBilanciaRegions(void) {
  gCoord w = gdispGetWidth();
  gCoord h = gdispGetHeight();

  region_count = 3;
  /* Button 0: ACTION_CARRELLO */
  regions[0].x0 = 16;
  regions[0].x1 = (int16_t)(w / 2 - 8);
  regions[0].y0 = (int16_t)(h - 64);
  regions[0].y1 = (int16_t)(h - 14);
  regions[0].id = ACTION_CARRELLO;

  /* Button 1: ACTION_STAMPA */
  regions[1].x0 = (int16_t)(w / 2 + 8);
  regions[1].x1 = (int16_t)(w - 16);
  regions[1].y0 = (int16_t)(h - 64);
  regions[1].y1 = (int16_t)(h - 14);
  regions[1].id = ACTION_STAMPA;

  /* Region 2: the whole price/weight/total info block (matches
   * drawBilanciaInfo()'s own fill area) - dwelling or PUSHing anywhere on
   * it opens the quantity keypad (2026-09-05). No button chrome is drawn
   * for this one (see drawBilanciaButton's early return below) since the
   * info text itself IS the visible surface. */
  regions[2].x0 = 0;
  regions[2].x1 = (int16_t)w;
  regions[2].y0 = 0;
  regions[2].y1 = 175;
  regions[2].id = ACTION_MOLTIPLICATORE;
}

static void drawBilanciaButton(uint8_t idx) {
  const region_t *r = &regions[idx];
  bool highlighted = (r->id == dwell_target_id);
  color_t bg;
  const char *label;

  if (r->id == ACTION_MOLTIPLICATORE) {
    /* No button chrome - drawBilanciaInfo() (and the dwell progress bar
     * drawn separately, see drawDwellProgress) already give feedback. */
    return;
  }

  if (r->id == ACTION_CARRELLO) {
    label = "+ CARRELLO";
    bg = highlighted ? HTML2COLOR(0x0369A1) : ui_theme.btn_bg_primary;
  } else {
    label = "STAMPA";
    bg = highlighted ? HTML2COLOR(0x15803D) : HTML2COLOR(0x166534);
  }

  gdispFillArea(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0), bg);

  color_t border_col = highlighted ? ui_theme.btn_border_highlight : ui_theme.btn_border_normal;
  gdispDrawBox(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0), border_col);
  gdispDrawBox((gCoord)(r->x0 + 1), (gCoord)(r->y0 + 1),
               (gCoord)(r->x1 - r->x0 - 2), (gCoord)(r->y1 - r->y0 - 2), border_col);

  gdispDrawStringBox(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0),
                      label, fontBody, ui_theme.text_primary, justifyCenter);
}

/* Shared by drawBilanciaInfo() (full redraw) and drawBilanciaDynamicFields()
 * (live-weight partial redraw, see below) so the pricing/floor logic only
 * exists in one place. */
static void bilanciaComputeDisplayValues(uint16_t *out_p, uint16_t *out_grams,
                                          uint32_t *out_total_cents) {
  uint16_t p = (selected_item < item_count) ? items[selected_item].prezzo_centesimi_al_kg : 0U;
  /* Peso: the single-portion reading straight from the scale (unchanged
   * meaning). Totale: priced on peso x bilancia_multiplier - see
   * bilancia_get_total_grams(). */
  uint16_t grams = (selected_item < item_count) ? bilancia_get_weight_grams(selected_item) : 1000U;
  uint16_t total_grams = (selected_item < item_count) ? bilancia_get_total_grams(selected_item) : grams;
  uint32_t total_cents = ((uint32_t)total_grams * p) / 1000U;

  /* Displayed-total floor at 1 EUR (2026-09-05): below that, the total is
   * just the ADC noise floor on a near-empty scale wobbling a few cents up
   * and down - not a real amount anyone is about to pay. Showing it as a
   * flat "0.00 EUR" here (display only - bilancia_get_total_grams() and
   * cart_add_item() are untouched, so a genuinely tiny but real weighed
   * amount still prices correctly once actually added to the cart) means
   * the total only ever visibly changes once it reaches a real euro of
   * value, instead of visibly ticking between a few cents at rest. */
  if (total_cents < 100U) {
    total_cents = 0U;
  }

  *out_p = p;
  *out_grams = grams;
  *out_total_cents = total_cents;
}

static void drawBilanciaInfo(void) {
  gCoord w = gdispGetWidth();
  char line[48];
  const char *nome = (selected_item < item_count) ? items[selected_item].nome : "";
  uint16_t p, grams;
  uint32_t total_cents;
  bilanciaComputeDisplayValues(&p, &grams, &total_cents);

  gdispFillArea(0, 0, w, 175, ui_theme.bg_screen);

  /* Title with item name */
  snprintf(line, sizeof(line), "BILANCIA - %s", nome);
  gdispDrawStringBox(0, 6, w, 24, line, fontTitle, ui_theme.text_primary, justifyCenter);

  /* Weight & Price per kg */
  snprintf(line, sizeof(line), "Prezzo: %u.%02u EUR/kg", (unsigned)(p / 100U), (unsigned)(p % 100U));
  gdispDrawStringBox(16, 38, 140, 20, line, fontSmall, ui_theme.text_muted, justifyLeft);

  snprintf(line, sizeof(line), "Peso: %u g", (unsigned)grams);
  gdispDrawStringBox(164, 38, 140, 20, line, fontSmall, ui_theme.text_muted, justifyRight);

  /* Quantity/multiplier - always shown (even at the x1 default) so the
   * operator always knows this isn't accidentally set to something else. */
  snprintf(line, sizeof(line), "Quantita': x%u  (push qui per cambiare)", (unsigned)bilancia_multiplier);
  gdispDrawStringBox(0, 58, w, 18, line, fontSmall, ui_theme.text_muted, justifyCenter);

  /* Calculated total */
  snprintf(line, sizeof(line), "Totale: %u.%02u EUR",
           (unsigned)(total_cents / 100U), (unsigned)(total_cents % 100U));
  gdispDrawStringBox(0, 78, w, 28, line, fontTitle, ui_theme.text_accent, justifyCenter);

  /* Status of cart */
  snprintf(line, sizeof(line), "Articoli nel carrello: %u", (unsigned)cart_count);
  gdispDrawStringBox(0, 112, w, 20, line, fontSmall, ui_theme.text_muted, justifyCenter);

  /* Gestures hint */
  gdispDrawStringBox(0, 138, w, 20, "Swipe sx: Torna",
                     fontSmall, ui_theme.text_muted, justifyCenter);
}

/* Live-weight partial redraw (2026-09-06, DEMO7 experiment): a weight-
 * change tick only ever changes two numbers - "Peso" and "Totale" - never
 * the title, price/kg, quantity or cart-count lines. Repainting just those
 * two small boxes instead of the whole 175px block (drawBilanciaInfo()
 * above) cuts the SPI traffic of each live update roughly by the ratio of
 * their areas to the full block - the visible "flash" scales with how much
 * of the screen actually gets touched, so this is a direct second line of
 * defense against flicker, independent of how well the EMA filter in
 * weight_adc.c smooths the input. Used ONLY from the per-tick weight-poll
 * path in uiMacelleriaTick() - screen entry and the cursor-restore path
 * still use the full drawBilanciaInfo(), since they can't assume nothing
 * else needs repainting. */
static void drawBilanciaDynamicFields(void) {
  gCoord w = gdispGetWidth();
  char line[48];
  uint16_t p, grams;
  uint32_t total_cents;
  bilanciaComputeDisplayValues(&p, &grams, &total_cents);
  (void)p;

  gdispFillArea(164, 38, 140, 20, ui_theme.bg_screen);
  snprintf(line, sizeof(line), "Peso: %u g", (unsigned)grams);
  gdispDrawStringBox(164, 38, 140, 20, line, fontSmall, ui_theme.text_muted, justifyRight);

  gdispFillArea(0, 78, w, 28, ui_theme.bg_screen);
  snprintf(line, sizeof(line), "Totale: %u.%02u EUR",
           (unsigned)(total_cents / 100U), (unsigned)(total_cents % 100U));
  gdispDrawStringBox(0, 78, w, 28, line, fontTitle, ui_theme.text_accent, justifyCenter);
}

static void enterBilancia(void) {
  state = MAC_STATE_BILANCIA;
  bilancia_multiplier = 1U; /* fresh selection - always starts at x1 */
  dwell_active = false;
  dwell_target_id = -1;
  cursor_drawn = false;
  cursor_glide_reset(&cursor_glide);

  gdispFillArea(0, 0, gdispGetWidth(), gdispGetHeight(), ui_theme.bg_screen);
  buildBilanciaRegions();
  for (uint8_t i = 0; i < region_count; i++) {
    drawRegionStatic(i);
  }
  drawBilanciaInfo();
  bilancia_last_drawn_grams = (selected_item < item_count) ? bilancia_get_weight_grams(selected_item) : 0xFFFFU;

  /* Traccia 28 (ding-dong): riprodotta all'ingresso della schermata Bilancia
   * come approssimazione temporanea, dato che il modulo bilancia hardware reale
   * non e' ancora integrato e il peso e' attualmente un valore statico (1000g).
   * Spostare questa chiamata sul vero evento di stabilizzazione peso quando il
   * modulo bilancia verra' integrato (vedi PLAN.md punto 5). */
  mp3_play_track(SOUND_DING_DONG);
}

/* ---- Quantity keypad (Moltiplicatore) -------------------------------------
 * NOT a price editor (price comes from the WiFi price-list link) - this
 * only lets the operator type how many identical weighed pieces the
 * customer wants (e.g. "2"), multiplying the scale weight before pricing.
 * Reuses the same 4x3 layout as the old price keypad, just with a 2-digit
 * cap (1..99) instead of 4. */
#define MAX_QTY_DIGITS 2U
static char    qty_digits[MAX_QTY_DIGITS + 1U];
static uint8_t qty_digit_count;

static void buildQtyRegions(void) {
  gCoord w = gdispGetWidth();
  gCoord h = gdispGetHeight();
  gCoord top = 44;
  gCoord cellW = w / 3;
  gCoord cellH = (h - top) / 4;
  static const int layout[4][3] = {
    { 1, 2, 3 },
    { 4, 5, 6 },
    { 7, 8, 9 },
    { ACTION_CANCELLA, 0, ACTION_CONFERMA }
  };
  int row, col;

  region_count = 0;
  for (row = 0; row < 4; row++) {
    for (col = 0; col < 3; col++) {
      region_t *r = &regions[region_count];
      r->x0 = (int16_t)(col * cellW + 3);
      r->x1 = (int16_t)((col + 1) * cellW - 3);
      r->y0 = (int16_t)(top + row * cellH + 3);
      r->y1 = (int16_t)(top + (row + 1) * cellH - 3);
      r->id = layout[row][col];
      region_count++;
    }
  }
}

static void drawQtyKey(uint8_t idx) {
  const region_t *r = &regions[idx];
  char label[8];
  bool highlighted = (r->id == dwell_target_id);
  color_t bg;

  if (r->id == ACTION_CANCELLA) {
    snprintf(label, sizeof(label), "CANC");
    bg = highlighted ? HTML2COLOR(0xDC2626) : ui_theme.btn_bg_danger;
  } else if (r->id == ACTION_CONFERMA) {
    snprintf(label, sizeof(label), "OK");
    bg = highlighted ? HTML2COLOR(0x0369A1) : ui_theme.btn_bg_primary;
  } else {
    snprintf(label, sizeof(label), "%d", r->id);
    bg = highlighted ? HTML2COLOR(0x334155) : ui_theme.btn_bg_normal;
  }

  gdispFillArea(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0), bg);

  color_t border_col = highlighted ? ui_theme.btn_border_highlight : ui_theme.btn_border_normal;
  gdispDrawBox(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0), border_col);
  gdispDrawBox((gCoord)(r->x0 + 1), (gCoord)(r->y0 + 1),
               (gCoord)(r->x1 - r->x0 - 2), (gCoord)(r->y1 - r->y0 - 2), border_col);

  gdispDrawStringBox(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0),
                      label, (r->id == ACTION_CANCELLA || r->id == ACTION_CONFERMA) ? fontBody : fontTitle,
                      ui_theme.text_primary, justifyCenter);
}

static void drawQtyReadout(void) {
  gCoord w = gdispGetWidth();
  char line[40];

  gdispFillArea(0, 0, w, 40, ui_theme.bg_screen);
  if (qty_digit_count > 0U) {
    snprintf(line, sizeof(line), "Quantita': %s", qty_digits);
  } else {
    snprintf(line, sizeof(line), "Quantita': ---");
  }
  gdispDrawStringBox(0, 6, w, 28, line, fontBody, ui_theme.text_primary, justifyCenter);
}

static void enterMoltiplicatore(void) {
  state = MAC_STATE_MOLTIPLICATORE;
  qty_digit_count = 0;
  qty_digits[0] = '\0';
  dwell_active = false;
  dwell_target_id = -1;
  cursor_drawn = false;
  cursor_glide_reset(&cursor_glide);

  gdispFillArea(0, 0, gdispGetWidth(), gdispGetHeight(), ui_theme.bg_screen);
  buildQtyRegions();
  for (uint8_t i = 0; i < region_count; i++) {
    drawRegionStatic(i);
  }
  drawQtyReadout();
}

/* Back to Bilancia after confirming (or backing out of) the quantity
 * keypad - like enterBilancia() but does NOT reset bilancia_multiplier
 * (that's the whole point of just having set it) and skips the ding-dong
 * (no new weighing happened, just a quantity confirmation). */
static void returnToBilanciaFromQty(void) {
  state = MAC_STATE_BILANCIA;
  dwell_active = false;
  dwell_target_id = -1;
  cursor_drawn = false;
  cursor_glide_reset(&cursor_glide);

  gdispFillArea(0, 0, gdispGetWidth(), gdispGetHeight(), ui_theme.bg_screen);
  buildBilanciaRegions();
  for (uint8_t i = 0; i < region_count; i++) {
    drawRegionStatic(i);
  }
  drawBilanciaInfo();
  bilancia_last_drawn_grams = (selected_item < item_count) ? bilancia_get_weight_grams(selected_item) : 0xFFFFU;
}

/* ---- Multi-item Receipt (Scontrino) --------------------------------------- */

static void buildReceiptRegions(void) {
  gCoord w = gdispGetWidth();
  gCoord h = gdispGetHeight();

  region_count = 1;
  regions[0].x0 = (int16_t)(w / 4);
  regions[0].x1 = (int16_t)((w * 3) / 4);
  regions[0].y0 = (int16_t)(h - 58);
  regions[0].y1 = (int16_t)(h - 12);
  regions[0].id = ACTION_NUOVO;
}

static void drawReceiptButton(uint8_t idx) {
  const region_t *r = &regions[idx];
  bool highlighted = (r->id == dwell_target_id);
  color_t bg = highlighted ? HTML2COLOR(0x0369A1) : ui_theme.btn_bg_primary;
  color_t border_col = highlighted ? ui_theme.btn_border_highlight : ui_theme.btn_border_normal;

  gdispFillArea(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0), bg);
  gdispDrawBox(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0), border_col);
  gdispDrawBox((gCoord)(r->x0 + 1), (gCoord)(r->y0 + 1),
               (gCoord)(r->x1 - r->x0 - 2), (gCoord)(r->y1 - r->y0 - 2), border_col);

  gdispDrawStringBox(r->x0, r->y0, (gCoord)(r->x1 - r->x0), (gCoord)(r->y1 - r->y0),
                      "NUOVO", fontTitle, ui_theme.text_primary, justifyCenter);
}

static void drawReceiptDetails(void) {
  gCoord w = gdispGetWidth();
  char line[48];
  uint32_t total_cents = 0;

  gdispFillArea(0, 0, w, 180, ui_theme.bg_screen);
  gdispDrawStringBox(0, 6, w, 22, "SCONTRINO", fontTitle, ui_theme.text_primary, justifyCenter);

  gCoord y = 32;
  uint8_t max_display = (receipt_cart_count > 5U) ? 4U : receipt_cart_count;
  for (uint8_t i = 0; i < max_display; i++) {
    const cart_entry_t *c = &receipt_cart[i];
    total_cents += c->subtotale_centesimi;

    snprintf(line, sizeof(line), "%s (%ug)", c->nome, (unsigned)c->peso_grammi);
    gdispDrawStringBox(16, y, 190, 18, line, fontSmall, ui_theme.text_primary, justifyLeft);

    snprintf(line, sizeof(line), "%u.%02u EUR",
             (unsigned)(c->subtotale_centesimi / 100U),
             (unsigned)(c->subtotale_centesimi % 100U));
    gdispDrawStringBox(206, y, 98, 18, line, fontSmall, ui_theme.text_accent, justifyRight);
    y += 18;
  }

  if (receipt_cart_count > 5U) {
    for (uint8_t i = max_display; i < receipt_cart_count; i++) {
      total_cents += receipt_cart[i].subtotale_centesimi;
    }
    snprintf(line, sizeof(line), "+ %u altri articoli...", (unsigned)(receipt_cart_count - max_display));
    gdispDrawStringBox(16, y, 288, 18, line, fontSmall, ui_theme.text_muted, justifyLeft);
    y += 18;
  }

  /* Divider line */
  gdispDrawLine(16, 144, 304, 144, ui_theme.btn_border_normal);

  /* Grand Total */
  gdispDrawStringBox(16, 148, 120, 24, "TOTALE:", fontBody, ui_theme.text_primary, justifyLeft);
  snprintf(line, sizeof(line), "%u.%02u EUR",
           (unsigned)(total_cents / 100U), (unsigned)(total_cents % 100U));
  gdispDrawStringBox(136, 148, 168, 24, line, fontBody, ui_theme.text_accent, justifyRight);
}

static void enterReceipt(void) {
  state = MAC_STATE_RECEIPT;
  dwell_active = false;
  dwell_target_id = -1;
  cursor_drawn = false;
  cursor_glide_reset(&cursor_glide);

  /* Snapshot the cart entries to receipt_cart and empty active cart */
  memcpy(receipt_cart, cart, sizeof(cart_entry_t) * cart_count);
  receipt_cart_count = cart_count;
  cart_count = 0;

  gdispFillArea(0, 0, gdispGetWidth(), gdispGetHeight(), ui_theme.bg_screen);
  buildReceiptRegions();
  for (uint8_t i = 0; i < region_count; i++) {
    drawRegionStatic(i);
  }
  drawReceiptDetails();

  mp3_play_track(SOUND_CASSA);
}

/* ---- Shared region/dwell/cursor machinery ---------------------------------- */

static void drawRegionStatic(uint8_t idx) {
  switch (state) {
  case MAC_STATE_HOME:     drawHomeButton(idx);     break;
  case MAC_STATE_CAROUSEL: drawCarouselItem(idx);   break;
  case MAC_STATE_BILANCIA:       drawBilanciaButton(idx); break;
  case MAC_STATE_MOLTIPLICATORE: drawQtyKey(idx);         break;
  case MAC_STATE_RECEIPT:        drawReceiptButton(idx);  break;
  default: break;
  }
}

static int hitTest(uint16_t x, uint16_t y) {
  for (uint8_t i = 0; i < region_count; i++) {
    const region_t *r = &regions[i];
    if ((int16_t)x >= r->x0 && (int16_t)x < r->x1 && (int16_t)y >= r->y0 && (int16_t)y < r->y1) {
      return r->id;
    }
  }
  return -1;
}

static int regionIndexForId(int id) {
  for (uint8_t i = 0; i < region_count; i++) {
    if (regions[i].id == id) {
      return (int)i;
    }
  }
  return -1;
}

static void drawDwellProgress(uint8_t idx) {
  const region_t *r = &regions[idx];
  gCoord full = (gCoord)(r->x1 - r->x0);
  gCoord barH = 5;
  systime_t elapsed = chTimeDiffX(dwell_start_time, chVTGetSystemTimeX());
  uint32_t ms = (uint32_t)TIME_I2MS(elapsed);
  gCoord filled;

  if (ms > DWELL_MS) {
    ms = DWELL_MS;
  }
  filled = (gCoord)(((uint32_t)full * ms) / DWELL_MS);

  gdispFillArea(r->x0, (gCoord)(r->y1 - barH), filled, barH, ui_theme.dwell_bar);
}

static void fireSelection(int id) {
  switch (state) {
  case MAC_STATE_HOME:
    if (id == ID_HOME_CLINICO) {
      enterClinico();
    } else {
      enterCarousel(); /* ID_HOME_MACELLERIA */
    }
    break;

  case MAC_STATE_CAROUSEL:
    selected_item = (id >= 0 && id < (int)item_count) ? (uint8_t)id : carousel_cursor_idx;
    enterBilancia();
    break;

  case MAC_STATE_BILANCIA:
    if (id == ACTION_CARRELLO) {
      cart_add_item(selected_item);
      returnToCarousel();
    } else if (id == ACTION_STAMPA) {
      if (cart_count == 0U) {
        cart_add_item(selected_item);
      }
      enterReceipt();
    } else if (id == ACTION_MOLTIPLICATORE) {
      enterMoltiplicatore();
    }
    break;

  case MAC_STATE_MOLTIPLICATORE:
    if (id == ACTION_CANCELLA) {
      mp3_play_track(SOUND_TASTO_BIP);
      if (qty_digit_count > 0U) {
        qty_digits[--qty_digit_count] = '\0';
      }
      drawQtyReadout();
    } else if (id == ACTION_CONFERMA) {
      mp3_play_track(SOUND_TASTO_BIP);
      if (qty_digit_count > 0U) {
        uint32_t val = 0U;
        for (uint8_t i = 0; i < qty_digit_count; i++) {
          val = val * 10U + (uint32_t)(qty_digits[i] - '0');
        }
        if (val < 1U) {
          val = 1U;
        } else if (val > 99U) {
          val = 99U;
        }
        bilancia_multiplier = (uint8_t)val;
      }
      returnToBilanciaFromQty();
    } else if (qty_digit_count < MAX_QTY_DIGITS) {
      mp3_play_track(SOUND_TASTO_BIP);
      qty_digits[qty_digit_count++] = (char)('0' + id);
      qty_digits[qty_digit_count] = '\0';
      drawQtyReadout();
    }
    break;

  case MAC_STATE_RECEIPT:
    if (id == ACTION_NUOVO) {
      returnToCarousel();
    }
    break;

  default:
    break;
  }
}

/* Redraw-based cursor restore (Problema 1 post-hardware fix):
 * Reading pixels from ILI9341 over SPI via gdispGetPixelColor/read_data()
 * was corrupted because board_ILI9341.h reads 8-bit SPI data into a 16-bit
 * variable, leaving the upper byte as stack garbage.
 * Instead of hardware read-back, we restore the cursor bounding box by:
 * 1) Filling the bounding box with ui_theme.bg_screen (black).
 * 2) Repainting any interactive region overlapping the box via drawRegionStatic().
 * 3) Repainting any screen header/details overlapping the box.
 * Completely eliminates SPI pixel read hardware dependencies and frees 392B of RAM. */
static gCoord cursor_box_x0, cursor_box_y0;
static uint8_t cursor_box_w, cursor_box_h;

static void clampCursorBox(gCoord *x0, gCoord *y0, uint8_t *w, uint8_t *h) {
  gCoord sw = gdispGetWidth();
  gCoord sh = gdispGetHeight();
  gCoord x1 = (gCoord)(*x0 + CURSOR_BOX_DIM);
  gCoord y1 = (gCoord)(*y0 + CURSOR_BOX_DIM);

  if (*x0 < 0) *x0 = 0;
  if (*y0 < 0) *y0 = 0;
  if (x1 > sw) x1 = sw;
  if (y1 > sh) y1 = sh;

  *w = (uint8_t)((x1 > *x0) ? (x1 - *x0) : 0);
  *h = (uint8_t)((y1 > *y0) ? (y1 - *y0) : 0);
}

static bool boxOverlap(gCoord x0, gCoord y0, gCoord w, gCoord h, const region_t *r) {
  return !(x0 >= r->x1 || (x0 + w) <= r->x0 || y0 >= r->y1 || (y0 + h) <= r->y0);
}

static bool clinicoContentOverlap(gCoord x0, gCoord y0, gCoord w, gCoord h) {
  gCoord sw = gdispGetWidth();
  gCoord sh = gdispGetHeight();

  /* 1. Header overlap (title and patient name at top) */
  if (y0 < 38) {
    return true;
  }

  /* 2. Footer overlap (action hint/subtitle at bottom) */
  if ((gCoord)(y0 + h) > (gCoord)(sh - 36)) {
    return true;
  }

  /* 3. Center content overlap - Cartella clinica (vitali) e Anamnesi sono
   * entrambe testo entro quest'area. */
  {
    gCoord cx0 = 8;
    gCoord cx1 = (gCoord)(sw - 8);
    gCoord cy0 = 28;
    gCoord cy1 = 154;

    if (!(x0 >= cx1 || (gCoord)(x0 + w) <= cx0 || y0 >= cy1 || (gCoord)(y0 + h) <= cy0)) {
      return true;
    }
  }

  return false;
}

static void restoreCursorIfDrawn(void) {
  if (!cursor_drawn) {
    return;
  }

  if (cursor_box_w > 0 && cursor_box_h > 0) {
    /* 1. Blank the bounding box with screen background */
    gdispFillArea(cursor_box_x0, cursor_box_y0, cursor_box_w, cursor_box_h, ui_theme.bg_screen);

    /* Clip all subsequent redraws to the exact cursor box so that repainting
     * overlapping buttons/screens does not flicker the area outside the box. */
    gdispSetClip(cursor_box_x0, cursor_box_y0, cursor_box_w, cursor_box_h);

    /* 2. Redraw any region overlapping the cursor box */
    for (uint8_t i = 0; i < region_count; i++) {
      if (boxOverlap(cursor_box_x0, cursor_box_y0, cursor_box_w, cursor_box_h, &regions[i])) {
        drawRegionStatic(i);
      }
    }

    /* 3. Screen-specific static elements that might overlap with the cursor box */
    switch (state) {
    case MAC_STATE_HOME:
      if (cursor_box_y0 < 36) {
        drawHomeTitle();
      }
      break;

    case MAC_STATE_CLINICO:
      if (clinicoContentOverlap(cursor_box_x0, cursor_box_y0, cursor_box_w, cursor_box_h)) {
        drawClinicoScreen();
      }
      break;

    case MAC_STATE_CAROUSEL:
      break;

    case MAC_STATE_BILANCIA:
      if (cursor_box_y0 < 176) {
        drawBilanciaInfo();
      }
      break;

    case MAC_STATE_MOLTIPLICATORE:
      if (cursor_box_y0 < 40) {
        drawQtyReadout();
      }
      break;

    case MAC_STATE_RECEIPT:
      if (cursor_box_y0 < 182) {
        drawReceiptDetails();
      }
      break;

    default:
      break;
    }

    /* Reset clip back to full display immediately so subsequent draws
     * (dwell progress bar, highlight changes, cursor sprite) are unclipped. */
    gdispUnsetClip();
  }

  cursor_drawn = false;
}

static void saveAndDrawCursor(uint16_t x, uint16_t y) {
  gCoord bx0 = (gCoord)(x - CURSOR_RADIUS - 1);
  gCoord by0 = (gCoord)(y - CURSOR_RADIUS - 1);
  uint8_t w, h;

  clampCursorBox(&bx0, &by0, &w, &h);

  cursor_box_x0 = bx0;
  cursor_box_y0 = by0;
  cursor_box_w = w;
  cursor_box_h = h;

  gdispFillCircle(x, y, CURSOR_RADIUS, ui_theme.cursor_fill);
  gdispDrawCircle(x, y, CURSOR_RADIUS, ui_theme.cursor_outline);
  cursor_last_x = x;
  cursor_last_y = y;
  cursor_drawn = true;
}

/* ---- Public API ------------------------------------------------------------- */

void uiMacelleriaInit(void) {
  chMtxObjectInit(&gdisp_mutex);

  fontTitle = gdispOpenFont("DejaVuSans24");
  fontBody  = gdispOpenFont("DejaVuSans16");
  fontSmall = gdispOpenFont("DejaVuSans12");

  cursor_glide_init(&cursor_glide);

  memcpy(items, default_items, sizeof(default_items));
  item_count = DEFAULT_ITEM_COUNT;
  selected_item = 0xFFU;
  for (uint8_t i = 0; i < CAROUSEL_MAX_ITEMS; i++) {
    item_weight_grams[i] = 0U; /* real value arrives via weight_adc (main.c) once weighed */
  }
  cart_count = 0;
  receipt_cart_count = 0;
  current_patient_idx = 0;

  enterHome(); /* draws the initial screen too - this is the boot entry point */
}

void uiMacelleriaTick(bool valid, uint16_t x, uint16_t y) {
  int hit;
  uint16_t fx = x;
  uint16_t fy = y;

  if (state == MAC_STATE_BUONGIORNO) {
    return; /* dormant state, kept for when the carousel is reactivated */
  }

  /* See the comment by stationary_ref_x/y's declaration above: skip the
   * cursor erase/redraw cycle entirely once the raw pointer has settled,
   * as long as nothing is being hovered/dwelled on (dwell_target_id < 0)
   * - checked against LAST tick's value, since this tick's hit-test runs
   * further below; at worst this delays a fresh dwell's very first
   * progress-bar frame by one ~20ms tick, imperceptible in practice. */
  bool cursor_stationary = valid && stationary_have_ref &&
      ((x > stationary_ref_x ? (uint16_t)(x - stationary_ref_x)
                             : (uint16_t)(stationary_ref_x - x))
           <= CURSOR_STATIONARY_TOLERANCE_PX) &&
      ((y > stationary_ref_y ? (uint16_t)(y - stationary_ref_y)
                             : (uint16_t)(stationary_ref_y - y))
           <= CURSOR_STATIONARY_TOLERANCE_PX);
  bool skip_cursor_redraw = cursor_stationary && (dwell_target_id < 0);
  if (valid) {
    stationary_ref_x = x;
    stationary_ref_y = y;
    stationary_have_ref = true;
  }
  else {
    stationary_have_ref = false;
  }

  chMtxLock(&gdisp_mutex);

  /* Undo last tick's cursor paint-over FIRST, before any other drawing
   * below (dwell progress, etc.) - so that drawing lands on genuine
   * content, not on top of a stale cursor sprite. The cursor itself gets
   * repainted last, after everything else, so it's always on top. */
  if (!skip_cursor_redraw) {
    restoreCursorIfDrawn();
  }

  if (valid) {
    cursor_glide_update(&cursor_glide, x, y, &fx, &fy);
    home_gesture_nav_active = false;
    carousel_gesture_nav_active = false;
  } else {
    cursor_glide_reset(&cursor_glide);
  }

  hit = valid ? hitTest(fx, fy) : -1;

  /* ACTION_MOLTIPLICATORE (2026-09-05) is PUSH-only, never dwell-active:
   * it covers the whole top info block, which is roughly where the hand
   * already sits right after a PUSH from the Carousel (tiles occupy the
   * same central-upper area) - left as a normal dwell target, that lingering
   * hand position immediately started a fresh dwell/progress-bar on this
   * region the instant Bilancia was entered. uiMacelleriaHandleGesture's
   * own PUSH handling still finds it fine via its own hitTest() call,
   * independent of this suppression. */
  if (hit == ACTION_MOLTIPLICATORE) {
    hit = -1;
  }

  if (hit != dwell_target_id) {
    int oldId = dwell_target_id;
    dwell_target_id = hit;
    dwell_active = (hit >= 0);
    if (dwell_active) {
      dwell_start_time = chVTGetSystemTimeX();
    }
    if (oldId >= 0) {
      int oldIdx = regionIndexForId(oldId);
      if (oldIdx >= 0) {
        drawRegionStatic((uint8_t)oldIdx);
      }
    }
    if (dwell_target_id >= 0) {
      int newIdx = regionIndexForId(dwell_target_id);
      if (newIdx >= 0) {
        drawRegionStatic((uint8_t)newIdx);
      }
    }
  } else if (dwell_active) {
    int idx = regionIndexForId(dwell_target_id);
    if (idx >= 0) {
      systime_t elapsed = chTimeDiffX(dwell_start_time, chVTGetSystemTimeX());
      if (elapsed >= TIME_MS2I(DWELL_MS)) {
        int fired_id = dwell_target_id;
        drawRegionStatic((uint8_t)idx); /* clear the progress bar before acting */
        dwell_active = false;
        dwell_target_id = -1;
        fireSelection(fired_id);
      } else {
        drawDwellProgress((uint8_t)idx);
      }
    }
  }

  /* Live weight readout (2026-09-05): redraw Bilancia's live fields once
   * item_weight_grams[selected_item] moves by more than
   * BILANCIA_REDRAW_DEADBAND_G, so it tracks the real load-cell feed
   * automatically instead of only ever reflecting whatever it was on
   * screen entry - without redrawing on every single tick just from
   * measurement noise (see the deadband's own comment above). Calls
   * drawBilanciaDynamicFields() (2026-09-06, DEMO7 experiment), NOT the
   * full drawBilanciaInfo() - this path fires on every real weight change,
   * so keeping it to just the two fields that actually change is what
   * makes each of those redraws cheap/fast on the SPI bus, on top of the
   * EMA filter in weight_adc.c making them fire less often in the first
   * place. weight_redraw_fired still gates the cursor-repaint safety net
   * below: the partial redraw is still unclipped to the cursor's own box,
   * so if the cursor happens to sit over either of those two small areas
   * it still needs a forced repaint afterward. */
  bool weight_redraw_fired = false;
  if (state == MAC_STATE_BILANCIA && selected_item < item_count) {
    uint16_t current_grams = bilancia_get_weight_grams(selected_item);
    uint16_t diff = (current_grams > bilancia_last_drawn_grams)
                        ? (uint16_t)(current_grams - bilancia_last_drawn_grams)
                        : (uint16_t)(bilancia_last_drawn_grams - current_grams);
    if (diff >= BILANCIA_REDRAW_DEADBAND_G) {
      bilancia_last_drawn_grams = current_grams;
      drawBilanciaDynamicFields();
      weight_redraw_fired = true;
    }
  }

  /* Cursor was already restored (undrawn) above; repaint it now, last, at
   * its filtered position - redraw-based restore guarantees genuine
   * content. Normally skipped along with the restore when stationary (see
   * skip_cursor_redraw above), EXCEPT if the weight-triggered redraw just
   * above fired: that one is independent of cursor movement/dwell state
   * entirely (driven purely by the scale), so it can legitimately happen
   * on a tick where the cursor itself didn't move - and drawBilanciaInfo()
   * is not clipped to the cursor's box, so if the cursor happens to sit in
   * that redrawn area it would otherwise be silently painted over and
   * never repainted until the hand moves again. */
  if (valid && (!skip_cursor_redraw || weight_redraw_fired)) {
    saveAndDrawCursor(fx, fy);
  }

  chMtxUnlock(&gdisp_mutex);
}

void uiMacelleriaHandleGesture(gesture_event_t g) {
  chMtxLock(&gdisp_mutex);

  if (g == GESTURE_PRESENCE) {
    /* Suono di benvenuto (traccia 3) - solo se qualcuno si avvicina mentre
     * il dispositivo e' fermo sul menu principale; se un utente e' gia'
     * impegnato altrove non ha senso interromperlo con il saluto. Gestito
     * PRIMA dell'abort del dwell sotto - una presenza ambientale non deve
     * mai cancellare una selezione in corso di qualcun altro. */
    if (state == MAC_STATE_HOME) {
      mp3_play_track(SOUND_BENVENUTO_MENU);
    }
    chMtxUnlock(&gdisp_mutex);
    return;
  }

  /* Abort any active dwell cleanly so its progress bar is erased and its
   * dwell_target_id does not conflict with the incoming gesture. */
  if (dwell_active || dwell_target_id >= 0) {
    int oldId = dwell_target_id;
    dwell_active = false;
    dwell_target_id = -1;
    if (oldId >= 0) {
      int oldIdx = regionIndexForId(oldId);
      if (oldIdx >= 0) {
        drawRegionStatic((uint8_t)oldIdx);
      }
    }
  }

  if (g == GESTURE_HOLD) {
    if (state != MAC_STATE_HOME) {
      enterHome(); /* context re-selection, from anywhere */
    }
    chMtxUnlock(&gdisp_mutex);
    return;
  }

  switch (state) {
  case MAC_STATE_HOME:
    /* Gesture-driven alternative to dwell, so Home is testable via the B1
     * self-test button alone (no pointer needed): SWIPE cycles which
     * button is highlighted (0..HOME_ENTRY_COUNT-1), PUSH confirms. */
    if (g == GESTURE_SWIPE_LEFT || g == GESTURE_SWIPE_RIGHT) {
      home_gesture_nav_active = true;
      home_highlight = (uint8_t)((home_highlight + 1U) % HOME_ENTRY_COUNT);
      for (uint8_t i = 0; i < region_count; i++) {
        drawRegionStatic(i);
      }
    } else if (g == GESTURE_PUSH) {
      /* Prefer whatever the real pointer is currently over (2026-09-05 -
       * PUSH was confirming home_highlight unconditionally, which only
       * SWIPE ever moves; with a live pointer in play PUSH looked like it
       * "did nothing" because it kept firing the gesture-cycled tile
       * instead of the one actually being pointed at). Falls back to the
       * gesture highlight when there's no live pointer (self-test/SD2
       * keyboard testing, no cursor drawn at all). */
      int hit = cursor_drawn ? hitTest(cursor_last_x, cursor_last_y) : -1;
      if (hit >= 0) {
        fireSelection(hit);
      } else if (home_highlight < HOME_ENTRY_COUNT) {
        fireSelection(home_entries[home_highlight].id);
      }
    }
    break;

  case MAC_STATE_CLINICO:
    /* SWIPE_LEFT returns to parent (Home); SWIPE_RIGHT toggles cartella clinica <-> anamnesi;
     * SWIPE_UP cycles through patients (0..PATIENT_COUNT-1). Everything else no-op. */
    if (g == GESTURE_SWIPE_LEFT) {
      enterHome();
    } else if (g == GESTURE_SWIPE_RIGHT) {
      clinico_screen_idx = (uint8_t)((clinico_screen_idx + 1U) % 2U);
      drawClinicoScreen();
    } else if (g == GESTURE_SWIPE_UP) {
      current_patient_idx = (uint8_t)((current_patient_idx + 1U) % PATIENT_COUNT);
      drawClinicoScreen();
    }
    break;

  case MAC_STATE_CAROUSEL:
    /* SWIPE_LEFT returns to parent (Home); SWIPE_RIGHT advances the gesture-testing
     * highlight forward cyclically; PUSH confirms whichever tile is highlighted. */
    if (g == GESTURE_SWIPE_LEFT) {
      enterHome();
    } else if (g == GESTURE_SWIPE_RIGHT) {
      carousel_gesture_nav_active = true;
      uint8_t old_idx = carousel_cursor_idx;
      carousel_cursor_idx = (uint8_t)((carousel_cursor_idx + 1U) % item_count);
      drawRegionStatic(old_idx);
      drawRegionStatic(carousel_cursor_idx);
    } else if (g == GESTURE_PUSH) {
      /* Same fix as Home's PUSH above: prefer the tile the real pointer is
       * currently over rather than always the gesture-cycled one. */
      int hit = cursor_drawn ? hitTest(cursor_last_x, cursor_last_y) : -1;
      selected_item = (hit >= 0 && hit < (int)item_count) ? (uint8_t)hit : carousel_cursor_idx;
      enterBilancia();
    }
    break;

  case MAC_STATE_BILANCIA:
    /* SWIPE_LEFT returns to parent (Carousel); PUSH confirms whichever
     * region (CARRELLO/STAMPA/the price-weight block) the real pointer is
     * currently over - same pattern as Home/Carousel's PUSH fix above. */
    if (g == GESTURE_SWIPE_LEFT) {
      returnToCarousel();
    } else if (g == GESTURE_PUSH) {
      int hit = cursor_drawn ? hitTest(cursor_last_x, cursor_last_y) : -1;
      if (hit >= 0) {
        fireSelection(hit);
      }
    }
    break;

  case MAC_STATE_MOLTIPLICATORE:
    if (g == GESTURE_SWIPE_LEFT) {
      returnToBilanciaFromQty(); /* discard, keep the previous multiplier */
    }
    break;

  case MAC_STATE_RECEIPT:
    if (g == GESTURE_SWIPE_LEFT) {
      returnToCarousel();
    }
    break;

  default:
    break;
  }

  chMtxUnlock(&gdisp_mutex);
}

void carousel_update_items(const carousel_item_t *nuovi_item, uint8_t count) {
  uint8_t n;

  if (nuovi_item == NULL || count == 0U) {
    return;
  }
  n = (count > CAROUSEL_MAX_ITEMS) ? CAROUSEL_MAX_ITEMS : count;
  memcpy(items, nuovi_item, n * sizeof(carousel_item_t));
  item_count = n;
  for (uint8_t i = 0; i < n; i++) {
    item_weight_grams[i] = 0U; /* real value arrives via weight_adc (main.c) once weighed */
  }
  if (carousel_cursor_idx >= item_count) {
    carousel_cursor_idx = 0;
  }

  chMtxLock(&gdisp_mutex);
  if (state == MAC_STATE_CAROUSEL) {
    carousel_gesture_nav_active = false;
    dwell_active = false;
    dwell_target_id = -1;
    cursor_drawn = false;
    cursor_glide_reset(&cursor_glide);
    drawCarouselGrid();
  }
  chMtxUnlock(&gdisp_mutex);
}
