# PONDULO — uno dei tre progetti vincitori NISC 2026

**Team 6 · NeaPolis Innovation Summer Campus · ecosistema STMicroelectronics**

PONDULO è un prototipo embedded per usare un'interfaccia senza toccare lo schermo:
un sensore Time-of-Flight individua la mano, il firmware ne ricava posizione e
gesti e una seconda scheda aggiorna il cursore e le schermate.

La dimostrazione Macelleria collega selezione prodotto, pesatura reale, quantità,
carrello e scontrino a schermo. Un percorso Clinico mostra dati paziente dimostrativi.
Tre NUCLEO-G474RE dividono acquisizione ToF, interfaccia e pesatura; il software
usa C, ChibiOS e uGFX, con comunicazione UART e un collegamento analogico DAC/ADC
per il peso. Il modulo audio YX5300 fornisce feedback nelle azioni dell'interfaccia.

Un progetto separato su ESP8266 permette di cambiare il listino via browser,
salvarlo in LittleFS e inviarlo a una STM32 di prova. Non è ancora integrato nel
firmware del display. Le funzioni SD sono dimostrative e non accedono a una scheda
fisica; lo scontrino non viene stampato su carta.

Il risultato è di squadra: PONDULO è uno dei tre progetti vincitori del NISC 2026.
Consulta i [crediti](CREDITS.md) per il contesto dell’evento e del team.

Un ringraziamento a tutti i **collaboratori del Team 6** per il contributo al
progetto. I [contributori della repository](https://github.com/EmanuelChirico/Pondulo/graphs/contributors)
mostrano i contributi registrati in Git; il lavoro di squadra comprende anche
attività svolte al di fuori di GitHub.

- [README principale](../README.md): presentazione in inglese per GitHub.
- [Architettura](ARCHITECTURE.md), [hardware](HARDWARE.md), [schema cablaggi HD](assets/pondulo-wiring-diagram.png), [compilazione](BUILD.md).
- [Verifica](VALIDATION.md): otto eseguibili di test host superati.
- [Testi per curriculum e profilo](PORTFOLIO.md).
