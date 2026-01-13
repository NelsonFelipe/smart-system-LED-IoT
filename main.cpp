#include <WiFi.h>
#include <PubSubClient.h>

// ============================================================
// CONFIGURACOES DE WI-FI E MQTT
// ============================================================
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";

WiFiClient espClient;
PubSubClient client(espClient);

// ============================================================
// DEFINICAO DOS PINOS
// ============================================================
const int PIR_PIN = 13;
const int LED_PIN = 12;
const int LDR_PIN = 34;

// ============================================================
// TOPICOS MQTT
// ============================================================
const char* topic_sensors = "nelson_iot/sensores";
const char* topic_led_cmd = "nelson_iot/led_cmd";
const char* topic_consumo = "nelson_iot/consumo";

// ============================================================
// VARIAVEIS PARA MONITORAMENTO DE CONSUMO DE ENERGIA
// ============================================================
const float POTENCIA_LAMPADA_WATTS = 9.0;
unsigned long tempoLedLigadoMs = 0;
unsigned long ultimoEstadoLedMs = 0;
bool ledEstaLigado = false;
float consumoAcumuladoKwh = 0.0;

const unsigned long INTERVALO_CONSUMO_MS = 60000;
unsigned long ultimaPublicacaoConsumoMs = 0;

// ============================================================
// FUNCOES AUXILIARES - CONSUMO DE ENERGIA
// ============================================================

void atualizarTempoLed() {
  unsigned long agora = millis();
  if (ledEstaLigado) {
    tempoLedLigadoMs += (agora - ultimoEstadoLedMs);
  }
  ultimoEstadoLedMs = agora;
}

void registrarLedLigado() {
  if (!ledEstaLigado) {
    atualizarTempoLed();
    ledEstaLigado = true;
    ultimoEstadoLedMs = millis();
    Serial.println("[ENERGIA] LED ligado - iniciando contagem");
  }
}

void registrarLedDesligado() {
  if (ledEstaLigado) {
    atualizarTempoLed();
    ledEstaLigado = false;
    Serial.println("[ENERGIA] LED desligado - contagem pausada");
  }
}

float calcularConsumoKwh() {
  if (ledEstaLigado) {
    atualizarTempoLed();
    ultimoEstadoLedMs = millis();
  }
  float tempoHoras = tempoLedLigadoMs / 3600000.0;
  float consumoKwh = (POTENCIA_LAMPADA_WATTS * tempoHoras) / 1000.0;
  return consumoKwh;
}

void publicarConsumo() {
  consumoAcumuladoKwh = calcularConsumoKwh();
  float consumoWh = consumoAcumuladoKwh * 1000.0;
  float tempoLigadoSegundos = tempoLedLigadoMs / 1000.0;
  float tempoLigadoMinutos = tempoLigadoSegundos / 60.0;
  
  Serial.println("========================================");
  Serial.println("[RELATORIO DE CONSUMO DE ENERGIA]");
  Serial.print("Tempo LED ligado: ");
  Serial.print(tempoLigadoMinutos, 2);
  Serial.println(" minutos");
  Serial.print("Potencia da lampada: ");
  Serial.print(POTENCIA_LAMPADA_WATTS, 0);
  Serial.println(" W");
  Serial.print("Consumo Atual: ");
  Serial.print(consumoWh, 4);
  Serial.print(" Wh (");
  Serial.print(consumoAcumuladoKwh, 6);
  Serial.println(" kWh)");
  Serial.println("========================================");
  
  String jsonConsumo = "{";
  jsonConsumo += "\"consumo_kwh\": " + String(consumoAcumuladoKwh, 6) + ",";
  jsonConsumo += "\"consumo_wh\": " + String(consumoWh, 4) + ",";
  jsonConsumo += "\"tempo_ligado_min\": " + String(tempoLigadoMinutos, 2) + ",";
  jsonConsumo += "\"potencia_w\": " + String(POTENCIA_LAMPADA_WATTS, 0) + ",";
  jsonConsumo += "\"led_estado\": \"" + String(ledEstaLigado ? "ON" : "OFF") + "\"";
  jsonConsumo += "}";
  
  client.publish(topic_consumo, jsonConsumo.c_str());
  Serial.print("[MQTT] Consumo publicado em: ");
  Serial.println(topic_consumo);
}

// ============================================================
// FUNCOES DE CONECTIVIDADE
// ============================================================

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando em ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi conectado");
  Serial.println("Endereco IP: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Mensagem recebida no topico [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  if (message == "ON") {
    digitalWrite(LED_PIN, HIGH);
    registrarLedLigado();
  } else if (message == "OFF") {
    digitalWrite(LED_PIN, LOW);
    registrarLedDesligado();
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentando conexao MQTT...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("conectado");
      client.subscribe(topic_led_cmd);
    } else {
      Serial.print("falha, rc=");
      Serial.print(client.state());
      Serial.println(" tentando novamente em 5 segundos");
      delay(5000);
    }
  }
}

// ============================================================
// SETUP E LOOP PRINCIPAL
// ============================================================

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);

  ultimoEstadoLedMs = millis();
  ultimaPublicacaoConsumoMs = millis();

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  Serial.println("========================================");
  Serial.println("Sistema de Iluminacao Inteligente");
  Serial.println("Monitoramento de Consumo Ativado");
  Serial.print("Potencia configurada: ");
  Serial.print(POTENCIA_LAMPADA_WATTS);
  Serial.println(" W");
  Serial.println("Relatorio de consumo a cada 1 minuto");
  Serial.println("========================================");
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();

  static unsigned long lastMsg = 0;
  if (now - lastMsg > 1000) {
    lastMsg = now;
    
    int pirState = digitalRead(PIR_PIN);
    int ldrValue = analogRead(LDR_PIN);
    
    String jsonPayload = "{";
    jsonPayload += "\"movimento\": " + String(pirState) + ",";
    jsonPayload += "\"luminosidade\": " + String(ldrValue);
    jsonPayload += "}";

    client.publish(topic_sensors, jsonPayload.c_str());
  }

  if (now - ultimaPublicacaoConsumoMs >= INTERVALO_CONSUMO_MS) {
    ultimaPublicacaoConsumoMs = now;
    publicarConsumo();
  }
}