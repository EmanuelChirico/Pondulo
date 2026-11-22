#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <LittleFS.h>

// Prototipo di listino digitale per salumeria.
// L'ESP8266 crea un server web sulla porta 80 e conserva i prodotti in flash.

#define MAX_PRODUCTS 4
#define PRODUCTS_FILE "/meat_prices_v1.txt"
#define TEMP_FILE "/meat_prices_v1.tmp"
bool fileSystemReady = false;

ESP8266WebServer server(80);

struct Product {
  uint16_t id;
  String code;
  String name;
  uint32_t priceCents;
};

Product products[MAX_PRODUCTS];
size_t productCount = 0;
uint16_t nextProductId = 1;

const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Listino macelleria</title><style>
*{box-sizing:border-box}body{margin:0;background:#f5f1e8;color:#203328;font:18px Arial,sans-serif}
main{max-width:620px;margin:40px auto;padding:24px}h1{font-family:Georgia,serif}
label{display:flex;justify-content:space-between;align-items:center;gap:16px;background:white;padding:18px;margin:12px 0;border-radius:10px}
input{width:140px;padding:12px;font:inherit;border:1px solid #aaa;border-radius:6px}
button{width:100%;padding:16px;background:#173d30;color:white;border:0;border-radius:8px;font:inherit;cursor:pointer}
button:disabled{opacity:.5}#status{min-height:3em;line-height:1.5}.error{color:#a21c2e}
</style></head><body><main><h1>Listino macelleria</h1>
<p>Prezzi in euro al kg. Salva tutti i prezzi e inviali alla scheda ST.</p>
<form id="prices"><div id="fields"></div><button id="save" disabled>Salva</button></form>
<p id="status" role="status" aria-live="polite">Caricamento...</p></main><script>
const form=document.getElementById('prices'),button=document.getElementById('save'),status=document.getElementById('status');
let products=[];
function message(text,error=false){status.textContent=text;status.className=error?'error':'';}
async function load(){try{
 const response=await fetch('/api/products',{cache:'no-store'});
 if(!response.ok)throw Error('Caricamento non riuscito. Ricarica la pagina.');
 products=await response.json();
 const fields=document.getElementById('fields');fields.replaceChildren();
 products.forEach(p=>{const label=document.createElement('label');label.textContent=p.name;
 const input=document.createElement('input');input.name='price_'+p.id;input.type='number';input.min='0.01';input.max='9999.99';input.step='0.01';input.required=true;
 input.value=(p.price_cents/100).toFixed(2);input.setAttribute('aria-label','Prezzo al kg di '+p.name);label.append(input);fields.append(label);});
 button.disabled=false;message('Modifica i prezzi, poi premi Salva.');
}catch(error){message(error.message,true);}}
form.addEventListener('submit',async event=>{event.preventDefault();button.disabled=true;
 const body=new URLSearchParams(new FormData(form));
 form.querySelectorAll('input').forEach(input=>input.disabled=true);
 try{const response=await fetch('/api/save',{method:'POST',body});const text=await response.text();
 if(!response.ok)throw Error(text);message(text);
 }catch(error){message('Salvataggio non confermato: '+error.message+'. Puoi riprovare con Salva.',true);}
 finally{button.disabled=false;form.querySelectorAll('input').forEach(input=>input.disabled=false);}});
load();</script></body></html>
)rawliteral";

String jsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\r", "\\r");
  value.replace("\n", "\\n");
  return value;
}

bool parsePrice(const String& inputValue, uint32_t& cents) {
  String input = inputValue;
  input.trim();
  input.replace(',', '.');
  if (input.length() == 0) return false;

  int dotPosition = input.indexOf('.');
  String euros = dotPosition < 0 ? input : input.substring(0, dotPosition);
  String decimals = dotPosition < 0 ? "" : input.substring(dotPosition + 1);
  if (euros.length() == 0 || euros.length() > 4 || decimals.length() > 2) return false;

  for (size_t i = 0; i < euros.length(); i++) if (euros[i] < '0' || euros[i] > '9') return false;
  for (size_t i = 0; i < decimals.length(); i++) if (decimals[i] < '0' || decimals[i] > '9') return false;
  while (decimals.length() < 2) decimals += '0';

  uint32_t euroValue = euros.toInt();
  uint32_t decimalValue = decimals.toInt();
  if (euroValue > 9999) return false;
  cents = euroValue * 100UL + decimalValue;
  return cents > 0;
}

void addProductInMemory(const String& code, const String& name, uint32_t priceCents) {
  if (productCount >= MAX_PRODUCTS) return;
  products[productCount].id = nextProductId++;
  products[productCount].code = code;
  products[productCount].name = name;
  products[productCount].priceCents = priceCents;
  productCount++;
}

void loadDefaultProducts() {
  productCount = 0;
  nextProductId = 1;
  addProductInMemory("0001", "Manzo", 1890);
  addProductInMemory("0002", "Pollo", 690);
  addProductInMemory("0003", "Maiale", 990);
  addProductInMemory("0004", "Agnello", 2290);
}

// Write a temporary snapshot, verify it, then replace the previous list.
// The old /products.txt is intentionally preserved (different catalogue).
bool saveProducts() {
  if (!fileSystemReady) return false;
  String snapshot;
  for (size_t i = 0; i < MAX_PRODUCTS; ++i) {
    snapshot += String(products[i].priceCents) + "\n";
  }
  File file = LittleFS.open(TEMP_FILE, "w");
  if (!file) return false;
  size_t written = file.print(snapshot);
  file.flush();
  file.close();
  if (written != snapshot.length()) return false;
  File check = LittleFS.open(TEMP_FILE, "r");
  if (!check) return false;
  String stored = check.readString();
  check.close();
  if (stored != snapshot) return false;
  return LittleFS.rename(TEMP_FILE, PRODUCTS_FILE);
}

bool loadProducts() {
  File file = LittleFS.open(PRODUCTS_FILE, "r");
  if (!file) return false;
  uint32_t prices[MAX_PRODUCTS];
  for (size_t i = 0; i < MAX_PRODUCTS; ++i) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.length() > 6) { file.close(); return false; }
    for (size_t j = 0; j < line.length(); ++j) {
      if (line[j] < '0' || line[j] > '9') { file.close(); return false; }
    }
    prices[i] = line.toInt();
    if (prices[i] == 0 || prices[i] > 999999UL) { file.close(); return false; }
  }
  bool extraData = file.available();
  file.close();
  if (extraData) return false;
  for (size_t i = 0; i < MAX_PRODUCTS; ++i) products[i].priceCents = prices[i];
  return true;
}

// Compatible with process_esp_message() in the STM32 receiver.
// Send complete records, so Save also works after a receiver reboot.
// No ACK is defined: transmission does not confirm reception by the ST.
void sendProductsToST() {
  for (size_t i = 0; i < MAX_PRODUCTS; ++i) {
    Serial.print("PRODUCT_ADD:"); Serial.print(products[i].id); Serial.print(':');
    Serial.print(products[i].name); Serial.print(':'); Serial.println(products[i].priceCents);
    Serial.flush();
    delay(100); // Receiver also writes verbose debug at 38400 baud.
    Serial.print("PRODUCT_CODE:"); Serial.print(products[i].id); Serial.print(':');
    Serial.println(products[i].code);
    Serial.flush();
    delay(100);
  }
}

void sendError(int statusCode, const String& message) {
  server.send(statusCode, "text/plain; charset=utf-8", message);
}

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", PAGE_HTML); }

void handleGetProducts() {
  String json = "[";
  json.reserve(1600);
  for (size_t i = 0; i < productCount; i++) {
    if (i > 0) json += ',';
    json += "{\"id\":" + String(products[i].id);
    json += ",\"code\":\"" + jsonEscape(products[i].code) + "\"";
    json += ",\"name\":\"" + jsonEscape(products[i].name) + "\"";
    json += ",\"price_cents\":" + String(products[i].priceCents) + "}";
  }
  json += ']';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", json);
}

void handleSavePrices() {
  uint32_t nextPrices[MAX_PRODUCTS];
  uint32_t previousPrices[MAX_PRODUCTS];
  // Validate the entire request before changing memory or flash.
  for (size_t i = 0; i < MAX_PRODUCTS; ++i) {
    String key = "price_" + String(products[i].id);
    if (!server.hasArg(key) || !parsePrice(server.arg(key), nextPrices[i])) {
      sendError(400, "Inserisci tutti e quattro i prezzi tra 0,01 e 9999,99 euro/kg.");
      return;
    }
    previousPrices[i] = products[i].priceCents;
  }
  for (size_t i = 0; i < MAX_PRODUCTS; ++i) products[i].priceCents = nextPrices[i];
  if (!saveProducts()) {
    for (size_t i = 0; i < MAX_PRODUCTS; ++i) products[i].priceCents = previousPrices[i];
    sendError(500, "Memoria non disponibile: prezzi non salvati e non inviati");
    return;
  }
  sendProductsToST();
  server.send(200, "text/plain; charset=utf-8", "Quattro prezzi salvati e trasmessi via seriale. Ricezione ST non confermata.");
}

void handleNotFound() { sendError(404, "Pagina non trovata."); }

void setup() {
  Serial.begin(38400);
  Serial.setTimeout(50);

  // Do not format the flash automatically if mounting fails.
  LittleFSConfig fsConfig;
  fsConfig.setAutoFormat(false);
  LittleFS.setConfig(fsConfig);
  fileSystemReady = LittleFS.begin();
  loadDefaultProducts();
  if (!fileSystemReady) Serial.println("Errore inizializzazione LittleFS");
  else if (LittleFS.exists(PRODUCTS_FILE) && !loadProducts())
    Serial.println("Errore listino salvato: visualizzati prezzi iniziali, premi Salva per sostituirlo");
  // First boot: defaults stay in RAM until the user explicitly saves.

  WiFiManager wifiManager;
  wifiManager.setDebugOutput(false);
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String accessPointName = "Salumeria-" + mac.substring(mac.length() - 6);

  if (!wifiManager.autoConnect(accessPointName.c_str())) {
    Serial.println("Connessione WiFi non riuscita");
    delay(3000);
    ESP.restart();
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/products", HTTP_GET, handleGetProducts);
  server.on("/api/save", HTTP_POST, handleSavePrices);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("WEB_SERVER_READY");
  Serial.print("IP_ADDRESS:");
  Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient();
  delay(2);
}
