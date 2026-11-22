# Listino ESP8266 — test STM32 e piano di integrazione

## 1. Stato del lavoro

Il test pagina web → ESP8266 → STM32 cambiaprezzo è stato confermato funzionante dall'utente. Il menu grafico, il sensore ToF e la bilancia non sono ancora integrati con la ricezione dei nuovi prezzi. Questo documento descrive il sistema disponibile e i passi da implementare; non dichiara completata l'integrazione.

Il nuovo menu contiene quattro prodotti fissi: Manzo, Pollo, Maiale, Agnello. Lo sketch ESP già caricato può rimanere invariato per la prima integrazione nel display.

**Versioni da distinguere:**

- `Nuovo_menu_STM32`: aggiornamento basato sul main.c incollato dall'utente. Mantiene PC4 TX e PC5 RX; il thread ricevente è nel main.c e usa `price_parser.c`. Il debug stampa `[PREZZO]`.
- `Test_cambiaprezzo`: banco prova precedente con `price_link.c`, `price_link_start()` e `price_link_take()`. Stampa anche un esempio di costo per 500 g; quel peso non proviene dalla bilancia.
- La cartella originale su Desktop, alla lettura effettuata per questo documento, contiene ancora il main.c con `price_link_start()`. La copia importata e compilata in ChibiStudio può essere diversa. Verificare con Properties → Resource → Location quale cartella è effettivamente usata, prima di sostituire file o creare backup.

I due ricevitori usano lo stesso protocollo. Per la futura integrazione scegliere un solo thread che legga SD1: non avviare contemporaneamente il ricevitore nel main.c e quello in price_link.c, perché si dividerebbero i byte.

## 2. Cartelle e responsabilità


| Percorso sotto gruppo6 | Ruolo |
|---|---|
| `RT-STM32G474RE-NUCLEO64-ESP8266cambiaprezzo` | Progetto completo del banco prova STM32; contiene .project/.cproject, main.c, Makefile e cfg |
| `RT-STM32G474RE-NUCLEO64-ESP8266cambiaprezzo/Listino_Salumeria` | Sketch Arduino dell'ESP8266 |
| `menu-funzionante/RT-STM32G474RE-NUCLEO64-GFX01M2-DEMO6` | Progetto della STM32 con display: destinazione della futura integrazione |
| `menu-funzionante/cursorSensor` | Scheda sensore ToF che invia cursore e gesti |
| `bilanciaFinale` | Progetto autonomo di pesata HX711; collegamento dei pesi reali al menu da trattare separatamente |

`Nuovo_menu_STM32` è un pacchetto di aggiornamento, non un progetto ChibiStudio completo. I suoi main.c, Makefile e price_link vanno copiati nel progetto cambiaprezzo. Non copiare il suo main.c o Makefile sopra quelli del display: si perderebbero le inizializzazioni e i sorgenti del menu.

## 3. Comunicazione adottata

```text
Browser -- HTTP sulla rete Wi-Fi --> ESP8266
                  Salva: valida → memorizza LittleFS → trasmette UART
ESP8266 TX -- UART ASCII 38400 8N1 --> STM32 cambiaprezzo
STM32 -- USB ST-LINK / debug seriale --> monitor del computer
```

Il browser legge `GET /api/products` e invia `POST /api/save` come form URL-encoded. I campi sono `price_1`, `price_2`, `price_3`, `price_4`, espressi in euro/kg. Esempio di body HTTP:

```text
price_1=18.90&price_2=6.90&price_3=9.90&price_4=22.90
```

La UART fra le schede non trasporta HTTP né JSON: usa righe ASCII. Impostazioni: **38400 baud, 8 bit dati, nessuna parità, 1 stop bit, nessun controllo di flusso**, segnali logici a 3,3 V. L'invio è unidirezionale ESP → ST. Il collegamento Wi-Fi serve tra browser ed ESP; la STM32 riceve via filo.

### Collegamenti per il test funzionante

| ESP8266 | STM32 NUCLEO-G474RE cambiaprezzo | Necessità |
|---|---|---|
| TX/TX0/GPIO1 | PC5, USART1 RX, SD1, AF7 | Necessario |
| GND | GND | Necessario, anche con due USB separate |
| RX/RX0/GPIO3 | PC4, USART1 TX, AF7 | Facoltativo: nessuna risposta trasmessa dal test |

PC5 è il nome GPIO, non il quinto contatto di un connettore. Identificare il pin sulla propria Nucleo. Se entrambe le schede di sviluppo sono alimentate dalle rispettive USB, non collegare tra loro 5V o 3V3. Le indicazioni VCC/CH_EN a 3,3 V si riferiscono a un modulo ESP nudo, non a un collegamento aggiuntivo richiesto per una scheda ESP già alimentata via USB.

Il debug del banco prova usa SD2/USART2, PA2 TX e PA3 RX verso ST-LINK, a 38400 8N1. Il LED verde del test è PA5. Il TX ESP è la Serial hardware, condivisa con programmazione e monitor seriale dell'ESP.

## 4. Formato dei dati UART

| ID | PLU, stringa | Nome esatto | Prezzo iniziale, centesimi/kg | Euro/kg |
|---|---|---|---:|---:|
| 1 | 0001 | Manzo | 1890 | 18,90 |
| 2 | 0002 | Pollo | 690 | 6,90 |
| 3 | 0003 | Maiale | 990 | 9,90 |
| 4 | 0004 | Agnello | 2290 | 22,90 |

I prezzi iniziali derivano dal menu grafico esistente. Gli ID sono numerici; i PLU sono stringhe e conservano gli zeri iniziali. Non confondere ID 1 con indice array 0.

Ogni pressione di Salva invia **tutto il listino**, anche se è stato modificato un solo prezzo, con queste otto righe nell'ordine indicato:

```text
PRODUCT_ADD:1:Manzo:1890
PRODUCT_CODE:1:0001
PRODUCT_ADD:2:Pollo:690
PRODUCT_CODE:2:0002
PRODUCT_ADD:3:Maiale:990
PRODUCT_CODE:3:0003
PRODUCT_ADD:4:Agnello:2290
PRODUCT_CODE:4:0004
```

Ogni riga emessa da Serial.println termina con CR LF (`\r\n`). Il parser ignora CR e completa la riga su LF. I campi sono separati da `:`. Non aggiungere virgolette, spazi, simboli di valuta o virgole decimali alle righe UART. I nomi e i codici devono corrispondere esattamente alle quattro voci.

Il prezzo UART è un intero positivo in **centesimi di euro per kg**, da 1 a 999999 compresi (0,01–9999,99 €/kg). Il server ESP accetta al massimo due decimali e converte prima dell'invio, evitando float per il prezzo. Tra le righe lo sketch esegue flush e una pausa di 100 ms: un invio dura circa 0,9 secondi. Non è un flusso periodico.

`PRODUCT_ADD` ha un nome storico: in questo protocollo trasporta il record completo, che sostituisce il prezzo del prodotto corrispondente. Non aggiunge prodotti arbitrari al nuovo menu.

All'avvio l'ESP può emettere anche `WEB_SERVER_READY` e `IP_ADDRESS:...`. Sono diagnostica, non prezzi. Il contatore `LISTINO OK #N` è locale alla STM32, non un numero di sequenza presente nel protocollo UART.

## 5. Salvataggio, validazione e limiti

1. L'ESP valida tutti e quattro i campi prima di modificare il listino.
2. Scrive il file temporaneo `/meat_prices_v1.tmp`, controlla i byte scritti e rilegge il contenuto.
3. Rinomina il temporaneo in `/meat_prices_v1.txt`. Il vecchio `/products.txt` del listino generico rimane separato.
4. Solo dopo il salvataggio riuscito invia le otto righe. Se la scrittura fallisce ripristina i valori precedenti in RAM e non trasmette.
5. La STM32 raccoglie i dati in una tabella provvisoria. Pubblica insieme i quattro prezzi solo dopo l'ultimo PRODUCT_CODE corretto.

LittleFS deve essere inizializzato e il layout flash deve prevedere spazio filesystem. Lo sketch non formatta automaticamente se il montaggio fallisce. Al riavvio ricarica i prezzi salvati; al primo avvio mostra i valori iniziali fino a Salva.

Il parser accetta ricezione byte per byte e righe frammentate. Scarta numeri fuori limite, nomi/ID/PLU errati, sequenze fuori ordine, righe oltre il buffer da 96 byte e caratteri di controllo non ammessi. Un intervallo oltre 2000 ms tra byte annulla la sequenza pendente quando arriva il byte successivo. Una nuova riga iniziale di Manzo permette di ripartire. Le righe vuote sono ignorate. Gli aggiornamenti incompleti non modificano il listino già pubblicato.

**Non ci sono ACK, checksum/CRC, autenticazione UART o ritrasmissione automatica.** Un errore che trasformi un prezzo in un altro numero valido non è rilevabile dal controllo sintattico. Il messaggio HTTP di trasmissione completata non certifica la ricezione ST. La conferma del test è LISTINO OK sul monitor STM32; il LED indica almeno una ricezione valida, non la presenza continua del collegamento.

La persistenza è sull'ESP. La tabella ST è in RAM: dopo un reset STM32 premere Salva di nuovo. Anche se l'ESP riparte con i prezzi salvati, attualmente non li reinvia automaticamente. Per sincronizzazione automatica futura servirà un'estensione esplicita del protocollo e dello sketch.

## 6. Ripetere il test autonomo

1. Conservare una copia dello sketch ESP e del progetto STM32 effettivamente compilato.
2. Importare il progetto completo cambiaprezzo in ChibiStudio con File → Import → General → Existing Projects into Workspace. Se è già importato, usare Refresh (F5).
3. Eseguire Clean e Build. Nel Makefile mantenere:

```makefile
export MSYS2_ARG_CONV_EXCL := *
```

Questa impostazione evita la conversione errata del percorso Windows del linker in `C;C:\...`. ChibiOS è richiesto nella posizione configurata dal Makefile. La versione aggiornata è stata compilata con GCC ARM 10.3.1; un avviso LTO sulle opzioni dei listing assembler non impedisce il link.

4. Programmare la sola NUCLEO-G474RE cambiaprezzo con il firmware risultante da quel progetto (`build/ch.elf` o HEX; un BIN usa base 0x08000000). Non usare il firmware di test sulla scheda del display.
5. Aprire la COM ST-LINK a 38400 8N1 senza controllo di flusso; resettare la ST per vedere il messaggio iniziale.
6. Avviare l'ESP, aprire la sua pagina e premere Salva. Verificare LISTINO OK, tutti i nomi, tutti i prezzi e LED verde.
7. Cambiare un prezzo e ripetere; riavviare l'ESP e verificare la persistenza nella pagina; riavviare la ST e ripremere Salva.

Nella versione Nuovo_menu_STM32 il riepilogo ha forma `[PREZZO] Manzo | PLU 0001 | 18,90 euro/kg`. La variante precedente mostra anche il totale di esempio a 500 g. Entrambe confermano gli stessi dati. L'utente ha confermato il funzionamento del test; i test software del parser coprono input validi, incompleti, ripetuti, errori, timeout e overflow. La futura integrazione sul display non è ancora stata collaudata.

## 7. Architettura prevista per il sistema completo — da implementare

```text
ESP8266 -- prezzi, UART1/SD1 38400 --> STM32 DISPLAY
SCHEDA ToF -- cursore/gesti, UART3/SD3 921600 --> STM32 DISPLAY
STM32 DISPLAY -- comandi audio, UART5/SD5 --> lettore MP3
Bilancia reale -- collegamento e protocollo da definire --> logica di pesata
```

Il piano è spostare la ricezione prezzi direttamente sulla STM32 del display. La scheda cambiaprezzo rimane un banco prova di riserva; non è previsto, per questa prima integrazione, un ponte ST cambiaprezzo → ST display.

Le seriali hanno ruoli e velocità distinti. Il codice display già usa SD3 per `$XY`/`$GS`, SD5 per MP3 e SD2 per debug/tasti di prova. Non mescolare il protocollo prezzi con quello ToF. Nel display SD2 è avviata con configurazione predefinita: verificare il baud effettivo, senza assumere che sia quello del banco prova.

### Passo A — controllo hardware prima del cablaggio

Verificare sullo schema della Nucleo e degli shield che PC5 sia accessibile e libero. Nei sorgenti grafici esaminati USART1 è disabilitata e PC5 non è assegnato al display, ma questo non sostituisce il controllo del cablaggio fisico. Il display usa PA9 come LCD CS: non scegliere PA9 come TX USART1. Il LED PA5 del banco prova coincide con il clock SPI del display: **non copiare il controllo LED PA5 nel firmware grafico**.

Dopo avere preparato e caricato il firmware integrato, spostare ESP TX su PC5 della ST display e collegare GND comune. PC4 e il ritorno verso ESP RX non servono per il protocollo attuale. Mantenere intatti i collegamenti ToF e audio.

### Passo B — importare solo il modulo ricevente

- Aggiungere `price_parser.c/.h` al progetto display e al suo Makefile (CSRC e INCDIR).
- Scegliere il thread del main.c aggiornato oppure il wrapper `price_link.c/.h` disponibile nel vecchio test; importare solo le funzioni necessarie, non sostituire il main.c del display.
- In cfg/mcuconf.h abilitare `STM32_SERIAL_USE_USART1 TRUE`, con HAL_USE_SERIAL attivo. Configurare PC5 AF7 e avviare SD1 a 38400 8N1. Verificare che non esista un altro lettore SD1.
- Inizializzare parser e mutex prima di avviare il thread RX. Il thread deve svuotare la UART senza disegnare, aspettare audio o stampare lunghi log.
- Pubblicare una copia completa del listino e un indicatore di aggiornamento/versione sotto mutex. `price_link_take()` è disponibile nel wrapper precedente; nella versione basata sullo script utente questa funzione non esiste e va introdotta un'interfaccia equivalente.

### Passo C — aggiornare il menu in sicurezza

La funzione attuale `carousel_update_items()` in ui_macelleria.c è un punto di partenza, ma non va usata così com'è come ricevitore: copia i dati prima del mutex, reimposta i pesi e ridisegna solo il carosello.

Implementare una funzione UI dedicata all'aggiornamento prezzi, con questi criteri:

1. Acquisire il listino completo dal ricevitore nel ciclo UI, rilasciare il mutex del ricevitore, poi acquisire il mutex grafico. Evitare lock annidati e chiamate ricorsive allo stesso mutex.
2. Associare ID/PLU alla voce corretta; non affidarsi solo all'ordine corrente dei riquadri. Applicare i quattro prezzi insieme.
3. Conservare selezione, pesi, carrello e navigazione. Aggiornare carosello e prezzo/totale nella schermata di pesata se visibili; gestire il ridisegno del cursore senza lasciare artefatti.
4. Mantenere esplicito lo stato “listino non ancora ricevuto” dopo il boot. Prima di una vendita attendere Salva/sincronizzazione, anziché presentare i valori dimostrativi come prezzi aggiornati.

Politica proposta per la prima integrazione: i nuovi prezzi si applicano alle prossime aggiunte al carrello; le righe già aggiunte e gli scontrini mantengono il prezzo acquisito. Documentare questa scelta nell'interfaccia. Le modifiche locali tramite tastierino display, se mantenute, restano in RAM e vengono sovrascritte dal prossimo Salva ESP: il collegamento attuale non le risalva sull'ESP. Durante una modifica locale in corso, prevedere un'indicazione dell'arrivo di un aggiornamento remoto ed evitare una sovrascrittura silenziosa con il vecchio valore del tastierino.

### Passo D — adeguare tipi e calcoli

Il menu attuale usa uint16_t per diversi prezzi, quindi arriva solo a 65535 centesimi/kg (655,35 €/kg), meno del massimo ESP. Portare a uint32_t il prezzo in carousel_item_t, righe carrello, parametri delle funzioni e variabili intermedie interessate. Verificare cast e limiti del tastierino; non limitarsi a cambiare l'header.

Per un peso in grammi interi:

```c
uint32_t totale_centesimi =
    (uint32_t)(((uint64_t)peso_grammi * prezzo_centesimi_al_kg) / 1000U);
```

Il prodotto intermedio a 64 bit evita overflow. Questa formula tronca la frazione di centesimo, come il calcolo attuale; mantenere una regola coerente in schermata, carrello e scontrino. Se si desidera arrotondare, definire e testare la regola prima di modificarla. Controllare anche la somma delle righe e la capienza delle stringhe visualizzate.

Il percorso Fruttivendolo usa dati SD e prezzi a 16 bit: non cambiarne involontariamente l'API durante l'adeguamento delle strutture condivise. L'integrazione della bilancia reale richiede di definire unità e protocollo del peso: non inviare i valori HX711 grezzi come grammi. Il collegamento prezzi non implementa da solo questa parte.

### Passo E — compilazione e collaudo integrato

Compilare il progetto display con le sue dipendenze ChibiOS e uGFX. Il Makefile cambiaprezzo non le sostituisce. Verificare budget di stack del thread UI e RX. Caricare sulla ST display soltanto l'ELF prodotto dal progetto display integrato.

| Prova | Risultato atteso |
|---|---|
| Salva con display sulla Home | Listino acquisito, navigazione invariata |
| Salva nel carosello | Quattro prezzi corretti, cursore funzionante |
| Salva nella pesata | Prezzo e totale aggiornati senza cambiare peso |
| Salva con articoli nel carrello | Righe già inserite conservate secondo la politica scelta |
| Aggiornamento durante tastierino prezzo | Conflitto gestito esplicitamente, nessuna sovrascrittura silenziosa |
| Invio incompleto o malformato | Ultimo listino completo conservato |
| Prezzo oltre 655,35 €/kg e fino al massimo | Nessun troncamento numerico o overflow |
| Reset della ST display | Stato non sincronizzato; Salva ripristina i prezzi |
| Uso contemporaneo ToF, audio e Salva | Nessuna perdita del cursore, blocco UI o conflitto UART |

## 8. Diagnosi rapida e ripristino

- Nessun debug: controllare COM ST-LINK, baud del firmware specifico e alimentazione; resettare la ST.
- Nessun LISTINO OK: premere Salva dopo il boot ST, verificare TX→PC5 e massa; controllare che l'ESP abbia confermato il salvataggio in flash.
- Errore LittleFS nella pagina: l'ESP non invia; verificare filesystem e layout flash prima di intervenire sul ricevitore.
- Prezzi ricevuti ma menu invariato: il banco prova e il display sono firmware distinti; manca ancora l'integrazione UI o è stato caricato il progetto sbagliato.
- Errore linker `C;C:\...`: verificare l'export MSYS2 nel Makefile effettivamente compilato.

Prima di integrare, archiviare sorgenti, configurazioni, Makefile, versione della toolchain e firmware di ciascuna scheda. In caso di regressione ripristinare il firmware display precedente e ricollegare ESP TX alla scheda cambiaprezzo per ripetere il test isolato.

Eventuali ACK, checksum, ritrasmissione, sincronizzazione automatica al boot e salvataggio dei prezzi modificati dal display sono estensioni future: richiedono aggiornamenti coordinati ESP/ST e non fanno parte del protocollo attuale.
