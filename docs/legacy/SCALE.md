# RT-STM32G474RE-NUCLEO64-CL3641BH-EX00

Firmware ChibiOS/ChibiStudio per Nucleo-G474RE: **bilancia da cucina**
che pesa con una cella di carico + amplificatore HX711 e mostra il peso
in grammi (formato `XXXXXX.YY`, sezione 2) su due display CL3641BH
(4 cifre 7 segmenti common-anode ciascuno, 8 cifre totali) pilotati
tramite il modulo RobotDyn "8-Digit LED Display 74HC595" (2x 74HC595 in
cascata + 2 display + stadio driver, tutto su un'unica basetta con 5
pin: 5V, GND, DIO, SCK, RCK).

Il driver del display (`userlib/{hc595,cl3641bh8}.{c,h}`) è nato prima
come demo standalone (da cui il nome del progetto, EX00) ed è
documentato a parte nella sezione 4; la logica di pesata vera e propria
(taratura, calibrazione, filtro, ciclo live/hold) vive in `main.c` ed è
descritta nella sezione 2.

## 1. Hardware

- **Board MCU**: ST Nucleo-64 STM32G474RE, ChibiOS/ChibiStudio, toolchain
  `arm-none-eabi-gcc`, `chibios2111`.
- **Display module**: RobotDyn "8-Digit LED Display 74HC595", 61x14mm,
  2x CL3641BH (0.36", common-anode, datasheet XLITX) + 2x 74HC595 + stadio
  driver per il digit-select, interfaccia a 5 pin.
- **Cella di carico + HX711**: amplificatore/ADC 24 bit, driver
  bit-banged (`userlib/hx711.{c,h}`), interfaccia a 2 pin (DT/DOUT,
  SCK) oltre ad alimentazione.

### Collegamento finale (attuale)

Fonte autorevole in caso di dubbio: header comment in `main.c` — il
cablaggio è cambiato più volte durante il debug raccontato in sezione 3.

| Modulo | Pin Nucleo | Funzione |
|---|---|---|
| Display 5V  | 5V  | alimentazione |
| Display GND | GND | massa |
| Display DIO | PB5 (Arduino D4) | SPI1 MOSI |
| Display SCK | PB3 (Arduino D3) | SPI1 SCK |
| Display RCK | PC0 (Arduino A5) | GPIO, latch (ST_CP) pulsato dopo ogni shift di 16 bit |
| HX711 VCC | 5V | alimentazione |
| HX711 GND | GND | massa |
| HX711 DT  | PB4 (Arduino D5) | ingresso dati (bit-banged) |
| HX711 SCK | PB10 (Arduino D6) | clock (bit-banged) |

Nessun altro collegamento è necessario per il display: entrambi i
display, i due 74HC595 e lo stadio driver del digit-select sono già
cablati insieme sul modulo.

### Protocollo (determinato empiricamente su questa unità)

- 16 bit shiftati MSB-first, un byte per ciascun 74HC595 in cascata.
- **Byte segmenti** (il chip più lontano dall'header nella cascata):
  active-LOW (bit=0 accende il segmento), ordine standard
  `bit0=A bit1=B bit2=C bit3=D bit4=E bit5=F bit6=G bit7=DP`.
- **Byte digit-select** (il chip collegato direttamente a DIO/SCK/RCK):
  active-HIGH (bit=1 accende quella cifra). Mapping bit→posizione fisica
  **non sequenziale**, rotazione ciclica di 4:

  | Bit output | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
  |---|---|---|---|---|---|---|---|---|
  | Posizione fisica (1=sinistra) | 5 | 6 | 7 | 8 | 1 | 2 | 3 | 4 |

  Cioè `digitBit[pos] = {4,5,6,7,0,1,2,3}` per `pos` 0..7 da sinistra a
  destra (vedi `userlib/src/cl3641bh8.c`).

**Attenzione**: questi valori (polarità, ordine bit del digit-select)
sono stati determinati testando fisicamente **questa unità specifica**
del modulo. Su un'altra unità (anche dello stesso modello) potrebbero
essere diversi — vedi la sezione 5 per rifare la calibrazione.

## 2. Logica di pesata (`main.c`)

Tutta la logica applicativa della bilancia (a differenza del driver del
display, generico e riusabile) vive in `main.c`, non in `userlib/`:
legge l'HX711 tramite `hx711.{c,h}` (driver bit-banged generico, nessuna
conoscenza della bilancia) e usa `cl3641bh8` solo per mostrare il
risultato.

**Calibrazione** (empirica, su questa unità fisica — da rifare se
cambiano cella di carico, scheda HX711 o cablaggio):

```
delta = 59240 count per 68.5g
centigrammi = (raw - tareRaw) * 6850 / 59240
```

Costanti in `main.c`: `CAL_KNOWN_CENTIGRAMS = 6850`,
`CAL_DELTA_COUNTS = 59240`. Per ricalibrare: appoggiare un peso noto,
leggere il valore raw dell'HX711 a vuoto e con il peso, calcolare il
delta e aggiornare le due costanti.

**Taratura (tare)**: la primissima misura *è* la taratura — si assume
la bilancia vuota all'avvio (come una bilancia da cucina reale): il suo
valore raw diventa il punto zero (`tareRaw`, media di 15 campioni su
~2s), quindi il primo risultato mostrato è sempre esattamente 0.00.
Nessuna costante fissa: così la deriva termica/di creep overnight della
cella di carico non sfalsa la lettura. Per tutta la durata di questa
sequenza (warmup del filtro + assestamento + media) e solo per quella,
sul display gira `cl3641bh8KnightriderUntil()` come indicatore "sto
lavorando" — un thread dedicato (`thdTareIndicator`), perché il thread
main è occupato a bloccarsi sulle letture HX711 per tutto questo tempo;
si ferma esattamente quando `tareRaw` è calcolato (il flag
`tareInProgress` è controllato ad ogni singolo passo dell'animazione,
non solo fra uno sweep e il successivo, quindi non sconfina oltre la
durata reale della taratura), e `chThdWait()` aspetta che l'indicatore
abbia davvero ripristinato il display prima di disegnare il primo 0.00
— altrimenti le due scritture rischierebbero di accavallarsi.

**Filtro**: ogni lettura raw dell'HX711 passa per una media mobile a 16
campioni (`hx711ReadFiltered()`, buffer circolare) prima di essere usata
per calcolare il peso.

**Ciclo live/hold**: la bilancia resta ferma sull'ultimo valore
mostrato (`holdValue`) finché il peso non cambia di più di ~1g
(`CHANGE_TOLERANCE_CG`, oggetto aggiunto/tolto). Quando cambia: 4.5s di
aggiornamento continuo ("live", cifre iniziali a zero visibili), poi
media di 15 campioni su ~2s e blocco su quel valore ("hold", cifre
iniziali spente invece che a zero — es. `68.50` invece di `000068.50`).
Valori negativi mostrano un segno meno subito prima del numero (nessun
problema per una bilancia da cucina: il peso non si avvicina mai alla
capacità massima delle 8 cifre).

**Auto-zero tracking**: la media dei 15 campioni di fine fase live è
calcolata sui valori **raw** dell'HX711 (`hx711ReadFiltered()`), non sul
peso già convertito — così, se il risultato si assesta entro ~1g dallo
zero (`AUTO_ZERO_THRESHOLD_CG = 100` centigrammi), la bilancia si ritara
automaticamente su quella media raw (`tareRaw = holdRawAvg`) e mostra
uno 0.00 esatto invece del rumore residuo del sensore — cancella anche
la deriva/creep accumulata dal boot, non solo l'ultima misura. Utile
quando un oggetto viene tolto e la bilancia torna vuota: niente bisogno
di rifare la taratura manualmente.

**Easter egg "Knight Rider"**: tenendo premuto il pulsante **B1** (PC13)
per `KNIGHTRIDER_LONGPRESS_MS` (default 1000ms, costanti tunabili in
cima a `main.c`) durante l'attesa di un cambio peso, parte
`cl3641bh8Knightrider()` — lo scanner "8" avanti-indietro come il
cruscotto di KITT in Supercar, `KNIGHTRIDER_SWEEPS` andate-e-ritorni
(default 3) a `KNIGHTRIDER_STEP_MS` per cifra (default 300ms). Controllato
solo in quel punto del loop (non durante taratura o fase live/hold) di
proposito: è l'unico momento in cui il thread principale non ha nulla
di time-critical da fare, quindi bloccarlo per l'effetto non disturba
il funzionamento della bilancia — un peso appoggiato *durante* l'effetto
viene notato solo alla fine, non prima.

## 3. Cosa non andava e come l'abbiamo trovato (driver display)

Riassunto in ordine cronologico dei problemi incontrati e delle relative
diagnosi/fix, utile per non ripetere gli stessi errori in futuro.

1. **Pin sbagliati (nessun segnale).** Il primo firmware pilotava
   SPI1 su D13/D11/D10, ma il cablaggio fisico reale era su D3/D4/D5.
   *Fix*: verificata la mappatura ufficiale ST Arduino-connector per la
   Nucleo-64 (D3=PB3, D4=PB5, D5=PB4) e passato temporaneamente a un
   driver bit-banged (GPIO software) su quei pin, dato che PB3/PB5 hanno
   funzioni SPI1 alternate invertite rispetto al bisogno (PB3=SCK-alt,
   PB5=MOSI-alt, mentre serviva il contrario). Alla fine si è deciso di
   ricablare fisicamente su D13/D11/D10 per usare l'SPI hardware "vero".

2. **Cifre bloccate, una più luminosa (stack overflow).** Con il driver
   bit-banged, il thread di refresh (`THD_WORKING_AREA`) era di soli 128
   byte. Le chiamate annidate del bit-banging (`hc595Write16` →
   `hc595ShiftByte` → delay), compilate senza ottimizzazioni (`-O0`),
   superavano lo stack disponibile. Il progetto ha `CH_DBG_ENABLE_STACK_CHECK
   FALSE`, quindi l'overflow non veniva rilevato: si manifestava come un
   blocco silenzioso a metà refresh (una cifra restava accesa,
   corrispondente all'ultimo stato scritto prima del crash).
   *Fix*: aumentato lo stack del thread di refresh a 512 byte (costa
   nulla su un chip con 128 KB di RAM).

3. **"Tutto acceso al massimo" (polarità digit-select sbagliata).** Un
   primo tentativo aveva impostato il digit-select come active-HIGH,
   basandosi su uno sketch Arduino trovato online per un modulo simile
   (che però conteneva un bug di indicizzazione array, quindi non del
   tutto affidabile). Con quella polarità, il codice mandava "7 bit alti
   su 8" invece di "1 bit alto su 8", accendendo quasi tutte le cifre
   contemporaneamente.
   *Fix*: invertita la polarità a active-LOW, coerente con un tipico
   stadio a transistor PNP (base tirata bassa = transistor acceso =
   cifra alimentata).

4. **Forme di segmenti incoerenti che si spostavano ciclicamente (byte
   segmenti/digit-select scambiati tra i due chip in cascata).** Il
   codice assumeva che il chip "vicino" all'header (che riceve l'ultimo
   byte shiftato) pilotasse i segmenti e quello "lontano" (cascata)
   pilotasse il digit-select. Su questo modulo è il contrario. Il
   sintomo era diagnostico ma non ovvio: il pattern segmenti (che nel
   codice cambia lentamente, 1 bit al secondo, durante il test di
   calibrazione) appariva "fisso" perché in realtà finiva sul registro
   di digit-select (che nel refresh normale cambia molto rapidamente,
   ~83 Hz, e la persistenza visiva lo fondeva in un'immagine statica);
   viceversa il digit-select (inteso a cambiare rapidamente per il
   multiplexing) finiva sui segmenti, e la sua lenta progressione (1 bit
   al secondo) appariva come "il segmento si sposta di cifra in cifra".
   *Fix*: scambiato l'ordine dei due byte nella chiamata a
   `hc595Write16()` (segmenti come `far_byte`, digit-select come
   `near_byte`).

5. **Dopo il fix del punto 4, un solo segmento si accendeva ma su TUTTE
   le cifre contemporaneamente.** La prova "tutto acceso" del punto 3
   era stata fatta con l'assegnazione chip-vicino/chip-lontano ancora
   sbagliata (prima del fix del punto 4): quindi quella deduzione sulla
   polarità andava rifatta con la mappatura corretta.
   *Fix*: reinvertita la polarità del digit-select su active-HIGH (la
   deduzione "PNP → active-low" era corretta in astratto, ma applicata
   al chip sbagliato).

6. **Ordine dei bit del digit-select non sequenziale.** Con tutto il
   resto corretto, il test `CALIBRATE_DIGIT_ORDER` ha mostrato che il
   bit di uscita 0 accende la **quinta** cifra fisica (non la prima), con
   una rotazione ciclica di 4 posizioni tra i bit e le cifre fisiche
   (probabilmente per come sono ordinati sul PCB i due gruppi di 4 cifre
   dei due display all'interno della catena dei due 74HC595).
   *Fix*: costruita la tabella `digitBit[] = {4,5,6,7,0,1,2,3}` (vedi
   sezione 1) da questi dati empirici.

7. **Ordine dei bit dei segmenti**: verificato con `CALIBRATE_SEGMENT_ORDER`
   che è quello standard (`bit0=A ... bit7=DP`) — nessuna correzione
   necessaria qui.

### Lezione generale

Quando si pilotano due registri a scorrimento in cascata su un modulo
già assemblato di cui non si conoscono gli schemi elettrici, **non date
per scontato**:
- quale dei due chip fisici riceve quale byte logico (vicino/lontano
  dall'header);
- la polarità (active-high/low) di segmenti e digit-select — possono
  essere diverse tra loro sullo stesso modulo;
- che l'ordine dei bit del digit-select segua l'ordine fisico delle
  cifre (spesso non è così, dipende dal routing del PCB).

L'unico modo affidabile per determinare questi valori è testarli
fisicamente uno alla volta (vedi sezione 5), non assumerli da sketch
trovati online per moduli "simili" (che possono riferirsi a una
revisione diversa della board, o contenere essi stessi bug).

## 4. Come programmare il display (libreria `cl3641bh8`)

Tutta la complessità (protocollo shift-register, polarità, ordine bit
non sequenziale, thread di refresh/multiplexing) è incapsulata in
`userlib/{hc595,cl3641bh8}.{c,h}`. `main.c` usa solo l'API di alto
livello:

```c
#include "ch.h"
#include "hal.h"
#include "cl3641bh8.h"

#define LINE_ARD_D13 PAL_LINE(GPIOA, 5U)  // SCK
#define LINE_ARD_D11 PAL_LINE(GPIOA, 7U)  // DIO / MOSI
#define LINE_ARD_D10 PAL_LINE(GPIOB, 6U)  // RCK (latch)

int main(void) {
  halInit();
  chSysInit();

  // Configura i pin, avvia l'SPI e il thread di refresh interno.
  cl3641bh8Start(&SPID1, LINE_ARD_D13, LINE_ARD_D11, LINE_ARD_D10);

  while (true) {
    cl3641bh8SetNumber(12345678);          // scrive un numero su tutte e 8 le cifre
    cl3641bh8SetDigit(7, 3, true);         // cifra 7 (ultima, a destra) = "3." con punto
    cl3641bh8SetDigit(0, CL3641BH8_BLANK, false); // spegne la prima cifra
    chThdSleepMilliseconds(500);
  }
}
```

API (`userlib/include/cl3641bh8.h`):

- `cl3641bh8Start(spip, sck_line, mosi_line, rck_line)` — da chiamare una
  volta dopo `halInit()`/`chSysInit()`. Configura i pin (AF SPI su
  sck/mosi, GPIO su rck), avvia l'SPI e lancia il thread di refresh in
  background: da questo punto il display si aggiorna da solo, non serve
  richiamare nulla periodicamente.
- `cl3641bh8SetDigit(pos, value, dp)` — imposta una singola cifra
  (`pos` 0=sinistra .. 7=destra), `value` 0-9 oppure `CL3641BH8_BLANK`
  per spegnerla, `dp` per il punto decimale.
- `cl3641bh8SetNumber(value)` — scrive un numero 0..99999999 su tutte le
  8 cifre, allineato a destra con zeri iniziali (non tocca i punti
  decimali, usare `cl3641bh8SetDigit` per quelli).
- `cl3641bh8SetNumberDp(value, dpPos)` — come sopra, ma accende anche il
  punto decimale della cifra `dpPos` (0=sinistra..7=destra) e spegne
  tutti gli altri. Esempio: `cl3641bh8SetNumberDp(12345678, 3)` mostra
  `1234.5678` (virgola dopo la 4ª cifra).
- `cl3641bh8Knightrider(sweeps, stepDelayMs)` — effetto "scanner" alla
  KITT di Supercar: un "occhio" largo `KNIGHTRIDER_EYE_WIDTH` cifre
  (default 4, `-||||-`) si sposta da sinistra a destra e ritorno, per
  `sweeps` andate-e-ritorni complete, con `stepDelayMs` millisecondi fra
  una posizione e la successiva (velocità tunabile). Le cifre
  dell'occhio si accendono con entrambi i segmenti verticali (alto e
  basso, nessun orizzontale — il glifo più "pieno" disponibile), le due
  cifre immediatamente adiacenti all'occhio mostrano un trattino `-`
  (solo segmento centrale), a fare da coda più debole — imita il bordo
  sfumato dello scanner di KITT senza bisogno di controllo PWM della
  luminosità. L'occhio resta sempre interamente visibile (mai tagliato
  ai bordi del display), solo i due trattini di coda possono sparire
  quando l'occhio è vicino a un'estremità. **Chiamata bloccante**: gira
  interamente sul thread chiamante (nessun thread nuovo), usando le
  stesse scritture sotto lock di tutte le altre funzioni qui sopra —
  sicura da chiamare da qualunque thread già usi questa API. Salva cosa
  c'è sullo schermo prima di partire e lo ripristina esattamente alla
  fine: chi la chiama non deve ridisegnare nulla dopo. Non chiamarla da
  un loop time-critical: blocca il chiamante per l'intera durata
  dell'effetto (circa `sweeps * 2*(8 - KNIGHTRIDER_EYE_WIDTH) *
  stepDelayMs` ms — con i default, 3 * 8 * 300ms ≈ 7.2s).
- `cl3641bh8KnightriderUntil(stopFlag, stepDelayMs)` — stesso effetto
  "-||||-", ma ripete gli sweep indefinitamente finché `*stopFlag` non
  diventa `true`, controllato ad ogni singolo passo (non solo fra uno
  sweep e il successivo) — pensata per indicatori "sto lavorando" di
  durata imprevedibile (es. la taratura al boot, vedi sezione 2):
  imposta il flag, avvia questa su un thread dedicato, poi azzera il
  flag e fai `chThdWait()` su quel thread appena il lavoro vero è
  finito, prima di ritoccare il display. Stesse garanzie di lock/salva-
  ripristina di `cl3641bh8Knightrider()`.

La demo in `main.c` mostra un contatore che avanza ogni 500 ms; premendo
il pulsante blu **B1** della Nucleo (PC13, già cablato sulla scheda, non
serve altro hardware) si alterna a runtime tra `cl3641bh8SetNumberDp()`
(virgola dopo la 4ª cifra, `XXXX.YYYY`) e `cl3641bh8SetNumber()` (senza
virgola, `XXXXYYYY`):

```c
bool showDp = true;
bool buttonWasPressed = false;
unsigned tick = 0;

while (true) {
  // B1 è active-LOW sulla Nucleo64 (a riposo HIGH via pull-up esterno
  // sulla scheda, premuto = GND); il poll ogni 50ms fa anche da debounce.
  bool buttonPressed = palReadLine(LINE_BUTTON) == PAL_LOW;
  if (buttonPressed && !buttonWasPressed) {
    showDp = !showDp;              // toggle ad ogni pressione
  }
  buttonWasPressed = buttonPressed;

  if (showDp) {
    cl3641bh8SetNumberDp(counter, 3);
  } else {
    cl3641bh8SetNumber(counter);
  }

  if (++tick >= 10) {              // il contatore avanza ogni 500ms
    tick = 0;                      // (10 x 50ms), non ad ogni poll
    counter = (counter + 1) % 100000000UL;
  }
  chThdSleepMilliseconds(50);
}
```

`LINE_BUTTON` è definito in `board.h` (ChibiOS) per PC13, già configurato
come ingresso all'avvio: non serve `palSetLineMode()` per usarlo.

Non serve conoscere né toccare `hc595.h` (il driver SPI generico di
basso livello per 2x 74HC595 in cascata) per usare il display: resta
comunque disponibile in `userlib/include/hc595.h` se in futuro serve
pilotare altro hardware basato su 74HC595 con un protocollo diverso.

## 5. Come rifare la calibrazione del display su un'altra unità

Se in futuro si usa un'altra unità dello stesso modulo (o un modulo
simile ma diverso) e i valori di `cl3641bh8.c` non funzionano, si può
rifare la procedura empirica usata qui, lavorando direttamente con
`hc595Write16()` (vedi `userlib/include/hc595.h`) invece che con
`cl3641bh8`:

1. **Verificare la polarità dei segmenti**: mandare un byte con un solo
   bit a 0 (e il resto a 1) come uno dei due byte del `hc595Write16`, e
   vedere se si accende un segmento (bit=0 → segmento acceso è il caso
   comune per display common-anode pilotati senza stadio invertente).
2. **Capire quale chip (near/far) pilota segmenti e quale digit-select**:
   mandare pattern diversi come `far_byte` e `near_byte` e osservare
   quale dei due produce variazioni sui segmenti e quale sulle cifre
   accese.
3. **Determinare la polarità del digit-select**: mandare un pattern
   one-hot (un solo bit a 1, resto 0) come byte digit-select e vedere se
   si accende una cifra sola (polarità corretta) o 7 su 8 (polarità
   invertita, provare il complemento).
4. **Determinare l'ordine bit↔posizione fisica del digit-select**: con
   segmento fissato (es. tutti accesi), ciclare un bit alla volta come
   digit-select (0x01, 0x02, 0x04, ... 0x80) e annotare quale posizione
   fisica (1=sinistra .. 8=destra) si accende per ciascun bit. Con quella
   sequenza si costruisce `digitBit[pos] = bit_che_accende_pos`
   (l'inversa della sequenza osservata).
5. **Determinare l'ordine bit↔segmento fisico**: con una cifra fissa
   accesa, ciclare un bit alla volta come byte segmenti e annotare quale
   segmento fisico (alto, alto-destro, basso-destro, basso, basso-sinistro,
   alto-sinistro, centrale, punto) si accende per ciascun bit; se non è
   l'ordine standard (`bit0=A...bit7=DP`), riordinare la tabella `font[]`
   o aggiungere un array di remap simile a `digitBit[]`.

Nel repository storico di questa sessione, il codice di calibrazione
usato (macro `CALIBRATE_DIGIT_ORDER` / `CALIBRATE_SEGMENT_ORDER` in
`main.c`, con test lenti da 1 secondo a step) è stato rimosso da
`main.c` dopo aver trovato i valori corretti, per tenere la demo finale
pulita — ma la stessa logica (vedi punti 3-5 sopra) si può riscrivere in
poche righe direttamente su `hc595Write16()` se serve rifarla.

## 6. Note di sviluppo (ChibiStudio / flashing da riga di comando)

Il progetto è pensato per essere aperto in ChibiStudio (Eclipse-based),
ma può anche essere compilato/flashato da terminale:

```bash
# CHIBIOS nel Makefile è un path relativo (../../chibios2111): se non
# esiste rispetto alla posizione del progetto, creare un symlink verso
# l'installazione reale (es. ~/ChibiStudio/chibios2111) prima di 'make'.
export PATH="$HOME/ChibiStudio/ChibiStudio-GCC11.3.1q3/bin:$PATH"
make -j4

# Flash via ST-Link/OpenOCD (board Nucleo, ST-Link integrato):
openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
  -c "program build/ch.elf verify reset exit"
```

Se ChibiStudio ha già una sessione OpenOCD aperta (in ascolto su
`localhost:4444`), non lanciarne una seconda: usare quella esistente via
`telnet localhost 4444` e i comandi `reset halt` / `program ... verify
reset exit`, altrimenti il secondo OpenOCD fallisce con "Error: open
failed" perché lo ST-Link è già occupato.
