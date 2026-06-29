#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>

// --- Configuración de Red ---
const char* ssid = "";
const char* password = "";
const char* serverName = "https://";

// --- Configuración de Pines ---
const int PIN_PIR = 27;       // Pin para el OUT del Sensor PIR
const int PIN_TFT_LED = 14;   // Pin seguro para el LED de la pantalla (Evita bucles)
const unsigned long TIEMPO_ENCENDIDO = 15000; // Tiempo de pantalla encendida (15 segundos)

// Variables para el control del PIR
volatile bool movimientoDetectado = false;
unsigned long ultimoMovimientoTiempo = 0;
bool pantallaEncendida = true;

TFT_eSPI tft = TFT_eSPI();
WiFiClientSecure client;

unsigned long ultimoTiempo = 0;
const long intervalo = 3000;

// Variables de control de estado
float cpuAnterior = -1.0;
float ramAnterior = -1.0;
float tempAnterior = -1.0;
bool servidorCaido = false;

// --- PÍXEL ART: CALAVERA CÓMICA (32x32 píxeles) ---
const unsigned char calavera_pixel_art[] PROGMEM = {
  0x00, 0x1F, 0xF8, 0x00,
  0x00, 0x7F, 0xFE, 0x00,
  0x00, 0xFF, 0xFF, 0x00,
  0x01, 0xFF, 0xFF, 0x80,
  0x03, 0xFF, 0xFF, 0xC0,
  0x07, 0xF0, 0x0F, 0xE0,
  0x0F, 0xE0, 0x07, 0xF0,
  0x0F, 0xC0, 0x03, 0xF0,
  0x1F, 0x80, 0x01, 0xF8,
  0x1F, 0x1F, 0xF8, 0xF8,
  0x3E, 0x3F, 0xFC, 0x7C,
  0x3E, 0x3F, 0xFC, 0x7C,
  0x3E, 0x30, 0x0C, 0x7C,
  0x3F, 0x30, 0x0C, 0xFC,
  0x1F, 0xF0, 0x0F, 0xF8,
  0x1F, 0xFF, 0xFF, 0xF8,
  0x0F, 0xFF, 0xFF, 0xF0,
  0x0F, 0x0F, 0xF0, 0xF0,
  0x07, 0x07, 0xE0, 0xE0,
  0x03, 0x80, 0x01, 0xC0,
  0x01, 0xDF, 0xFB, 0x80,
  0x00, 0xFF, 0xFF, 0x00,
  0x00, 0x7F, 0xFE, 0x00,
  0x00, 0x6D, 0xB6, 0x00,
  0x00, 0x6D, 0xB6, 0x00,
  0x00, 0x6D, 0xB6, 0x00,
  0x00, 0x7F, 0xFE, 0x00,
  0x00, 0x3F, 0xFC, 0x00,
  0x00, 0x1F, 0xF8, 0x00,
  0x00, 0x0F, 0xF0, 0x00,
  0x00, 0x03, 0xC0, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Función ISR para la interrupción del PIR
void IRAM_ATTR detectarMovimiento() {
  movimientoDetectado = true;
}

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  // Forzamos al ESP32 a encender el módem de radio WiFi si estaba apagado
  WiFi.mode(WIFI_STA);
  Serial.print("Conectando a WiFi...");
  WiFi.begin(ssid, password);
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print(".");
    intentos++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n¡WiFi Conectado!");
  }
}

void apagarWiFi() {
  Serial.println("Apagando radio WiFi para eliminar interferencias...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void dibujarInterfazDashboard() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("EC2 SERVER MONITOR", 10, 10);
  tft.drawFastHLine(10, 32, 300, TFT_BLUE);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("CPU:", 10, 55);
  tft.drawString("%", 170, 55);
  tft.drawRect(10, 80, 300, 15, TFT_WHITE);

  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString("RAM:", 10, 115);
  tft.drawString("%", 170, 115);
  tft.drawRect(10, 140, 300, 15, TFT_WHITE);

  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("TEMP:", 10, 175);
  tft.drawString("C", 170, 175);
  tft.drawRect(10, 200, 300, 15, TFT_WHITE);
}

void dibujarPantallaError() {
  tft.fillScreen(TFT_BLACK);
  tft.drawBitmap(144, 40, calavera_pixel_art, 32, 32, TFT_YELLOW);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawCentreString("F EN EL CHAT...", 160, 95, 1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawCentreString("Tu server paso a", 160, 130, 1);
  tft.drawCentreString("mejor vida o el script", 160, 155, 1);
  tft.drawCentreString("se pego un viaje.", 160, 180, 1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawCentreString("Reintentando conexion cada 3s...", 160, 215, 1);
}

void setup() {
  Serial.begin(115200);
  client.setInsecure(); // Permite conectar a HTTPS saltándose la verificación de certificado estático
  
  // Configuración de Hardware
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_TFT_LED, OUTPUT);
  digitalWrite(PIN_TFT_LED, HIGH); // Encendida al iniciar
  
  attachInterrupt(digitalPinToInterrupt(PIN_PIR), detectarMovimiento, RISING);
  
  tft.init();
  tft.setRotation(1);
  dibujarInterfazDashboard();
  conectarWiFi();
  ultimoMovimientoTiempo = millis();
}

void loop() {
  unsigned long tiempoActual = millis();

  // --- GESTIÓN DE ENERGÍA Y MOVIMIENTO (PIR) ---
  if (movimientoDetectado) {
    movimientoDetectado = false;
    // Filtro anti-rebote mejorado
    delay(50);
    if (digitalRead(PIN_PIR) == HIGH) {
      ultimoMovimientoTiempo = tiempoActual;
      if (!pantallaEncendida) {
        Serial.println("¡Movimiento real detectado! Encendiendo todo.");
        digitalWrite(PIN_TFT_LED, HIGH); // Enciende pantalla
        pantallaEncendida = true;
        
        // Forzar redibujado base inmediatamente resetando buffers
        cpuAnterior = -1.0; ramAnterior = -1.0; tempAnterior = -1.0;
        if (servidorCaido) dibujarPantallaError(); else dibujarInterfazDashboard();
        
        // Conectamos de nuevo al WiFi ya que la radio estaba apagada
        conectarWiFi();
      }
    }
  }

  // Apagar pantalla y WiFi tras inactividad (15 segundos)
  if (pantallaEncendida && (tiempoActual - ultimoMovimientoTiempo >= TIEMPO_ENCENDIDO)) {
    Serial.println("Sin movimiento. Entrando en modo de ahorro.");
    digitalWrite(PIN_TFT_LED, LOW); // Apaga el backlight de la pantalla
    pantallaEncendida = false;
    apagarWiFi(); // Desactiva por completo el transmisor WiFi para eliminar el ruido y consumo
  }

  // --- PETICIONES HTTP ---
  if (pantallaEncendida) {
    if (WiFi.status() != WL_CONNECTED) {
      conectarWiFi();
    }

    if (tiempoActual - ultimoTiempo >= intervalo) {
      ultimoTiempo = tiempoActual;

      if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(client, serverName);
        int httpResponseCode = http.GET();

        if (httpResponseCode == 200) {
          if (servidorCaido) {
            servidorCaido = false;
            cpuAnterior = -1.0; ramAnterior = -1.0; tempAnterior = -1.0;
            dibujarInterfazDashboard();
          }

          String payload = http.getString();
          StaticJsonDocument<200> doc;
          DeserializationError error = deserializeJson(doc, payload);

          if (!error) {
            float cpuActual = doc["cpu"];
            float ramActual = doc["ram"];
            float tempActual = doc["temp"];
            tft.setTextSize(2);
            int anchoMaximo = 296;

            // Barra CPU
            if (abs(cpuActual - cpuAnterior) > 0.5) {
              tft.setTextColor(TFT_CYAN, TFT_BLACK);
              tft.drawFloat(cpuActual, 1, 70, 55);
              int anchoNuevo = (int)(anchoMaximo * (cpuActual / 100.0));
              if (anchoNuevo > anchoMaximo) anchoNuevo = anchoMaximo;
              int anchoAnteriorBarra = (int)(anchoMaximo * (cpuAnterior / 100.0));
              
              if (anchoNuevo > anchoAnteriorBarra) {
                tft.fillRect(12 + anchoAnteriorBarra, 82, anchoNuevo - anchoAnteriorBarra, 11, TFT_GREEN);
              } else if (anchoNuevo < anchoAnteriorBarra) {
                tft.fillRect(12 + anchoNuevo, 82, anchoAnteriorBarra - anchoNuevo, 11, TFT_BLACK);
              }
              cpuAnterior = cpuActual;
            }

            // Barra RAM
            if (abs(ramActual - ramAnterior) > 0.5) {
              tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
              tft.drawFloat(ramActual, 1, 70, 115);
              int anchoNuevo = (int)(anchoMaximo * (ramActual / 100.0));
              if (anchoNuevo > anchoMaximo) anchoNuevo = anchoMaximo;
              int anchoAnteriorBarra = (int)(anchoMaximo * (ramAnterior / 100.0));
              
              if (anchoNuevo > anchoAnteriorBarra) {
                tft.fillRect(12 + anchoAnteriorBarra, 142, anchoNuevo - anchoAnteriorBarra, 11, TFT_ORANGE);
              } else if (anchoNuevo < anchoAnteriorBarra) {
                tft.fillRect(12 + anchoNuevo, 142, anchoAnteriorBarra - anchoNuevo, 11, TFT_BLACK);
              }
              ramAnterior = ramActual;
            }

            // Barra TEMP
            if (abs(tempActual - tempAnterior) > 0.5) {
              tft.setTextColor(TFT_RED, TFT_BLACK);
              tft.drawFloat(tempActual, 1, 70, 175);
              int anchoNuevo = (int)(anchoMaximo * (tempActual / 100.0));
              if (anchoNuevo > anchoMaximo) anchoNuevo = anchoMaximo;
              int anchoAnteriorBarra = (int)(anchoMaximo * (tempAnterior / 100.0));
              
              if (anchoNuevo > anchoAnteriorBarra) {
                tft.fillRect(12 + anchoAnteriorBarra, 202, anchoNuevo - anchoAnteriorBarra, 11, TFT_RED);
              } else if (anchoNuevo < anchoAnteriorBarra) {
                tft.fillRect(12 + anchoNuevo, 202, anchoAnteriorBarra - anchoNuevo, 11, TFT_BLACK);
              }
              tempAnterior = tempActual;
            }
          }
        }
        else {
          Serial.print("Servidor inaccesible. Código: ");
          Serial.println(httpResponseCode);
          if (!servidorCaido) {
            servidorCaido = true;
            dibujarPantallaError();
          }
        }
        http.end();
      }
    }
  }
}
