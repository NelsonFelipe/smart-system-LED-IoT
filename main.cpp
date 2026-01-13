#include <WiFi.h>
#include <PubSubClient.h>

// --- Configurações Gerais ---
#define MODO_SIMULACAO_ATIVO 

// 1000ms = 1 minuto simulado (Dia de 24h = 24 minutos reais)
const int VELOCIDADE_SIMULACAO = 1000; 

// --- Configurações de Rede ---
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";

WiFiClient espClient;
PubSubClient client(espClient);

// --- Hardware e Tópicos MQTT ---
const int PIR_PIN = 13;
const int LED_PIN = 12;
const int LDR_PIN = 34;

const char* topic_sensors = "nelson_iot/sensores";
const char* topic_led_cmd = "nelson_iot/led_cmd";
const char* topic_consumo = "nelson_iot/consumo";

// --- Variáveis de Energia ---
const float POTENCIA_LAMPADA_WATTS = 9.0;
unsigned long tempoLedLigadoMs = 0;
unsigned long ultimoEstadoLedMs = 0;
bool ledEstaLigado = false;

// Acumuladores
float ultimoConsumoKwh = 0.0; 
float consumoAcumuladoKwh = 0.0;
float consumoDiarioKwh = 0.0; 

// --- Variáveis de Tempo Simulado ---
int horaSimulada = 0;
int minutoSimulado = 0;
unsigned long lastSimUpdate = 0;
bool mudouDeHora = false;

// --- Estrutura e Variáveis da Simulação ---
struct DadosSimulados {
  int movimento;
  int luminosidade;
};

int tempoRestanteMovimento = 0; // Memória de presença

// --- Lógica de Simulação de Ambiente ---
DadosSimulados gerarAmbienteSimulado() {
  unsigned long now = millis();
  
  // Controle do Tempo Acelerado
  if (now - lastSimUpdate >= VELOCIDADE_SIMULACAO) { 
    lastSimUpdate = now;
    minutoSimulado++;
    
    if (tempoRestanteMovimento > 0) tempoRestanteMovimento--;

    if (minutoSimulado >= 60) {
      minutoSimulado = 0;
      horaSimulada++;
      if (horaSimulada >= 24) horaSimulada = 0;
      mudouDeHora = true; // Gatilho para relatório
    }
  }

  int ldrSimulado = 0;
  int pirSimulado = 0;

  // Lógica LDR: Escuro à noite (18h-06h), Claro de dia
  if (horaSimulada >= 18 || horaSimulada < 6) {
    ldrSimulado = random(2500, 4095); 
  } else {
    ldrSimulado = random(0, 800);    
  }

  // Lógica PIR: Probabilidade baseada no horário
  if (tempoRestanteMovimento > 0) {
     pirSimulado = 1;
  } else {
    int chanceEntrar = 0;
    
    if (horaSimulada >= 18 && horaSimulada <= 23) chanceEntrar = 35; // Pico Noturno
    else if (horaSimulada >= 6 && horaSimulada < 18) chanceEntrar = 10; // Dia
    else chanceEntrar = 5;  // Madrugada

    if (random(0, 100) < chanceEntrar) {
      pirSimulado = 1;
      tempoRestanteMovimento = random(15, 45); 
    } else {
      pirSimulado = 0;
    }
  }

  return {pirSimulado, ldrSimulado};
}

// --- Funções Auxiliares de Energia ---
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
  }
}

void registrarLedDesligado() {
  if (ledEstaLigado) {
    atualizarTempoLed();
    ledEstaLigado = false;
  }
}

float calcularConsumoKwh() {
  if (ledEstaLigado) {
    atualizarTempoLed();
    ultimoEstadoLedMs = millis();
  }
  float tempoHoras = tempoLedLigadoMs / 3600000.0;
  return (POTENCIA_LAMPADA_WATTS * tempoHoras) / 1000.0;
}

// --- Relatório e Envio MQTT ---
void publicarRelatorioHorario() {
  // 1. Cálculos de Energia
  consumoAcumuladoKwh = calcularConsumoKwh();
  
  float consumoNestaHoraKwh = consumoAcumuladoKwh - ultimoConsumoKwh;
  float consumoNestaHoraWh = consumoNestaHoraKwh * 1000.0;
  
  ultimoConsumoKwh = consumoAcumuladoKwh;
  consumoDiarioKwh += consumoNestaHoraKwh;

  // 2. Preparação de Dados
  int horasPassadas = (horaSimulada == 0) ? 24 : horaSimulada;
  float mediaHorariaWh = (consumoDiarioKwh * 1000.0) / horasPassadas;
  float totalDiaWh = consumoDiarioKwh * 1000.0;
  
  float tempoLigadoMinutos = (tempoLedLigadoMs / 1000.0) / 60.0;
  
  // Debug Serial
  Serial.printf("\n[RELATORIO %02d:00] Hora: %.4f Wh | Total Dia: %.4f Wh\n", 
                horaSimulada == 0 ? 24 : horaSimulada, consumoNestaHoraWh, totalDiaWh);

  // 3. Envio MQTT (Antes do Reset)
  String jsonConsumo = "{";
  jsonConsumo += "\"consumo_kwh\": " + String(consumoAcumuladoKwh, 6) + ",";
  jsonConsumo += "\"consumo_wh\": " + String(consumoNestaHoraWh, 4) + ","; 
  jsonConsumo += "\"media_dia_wh\": " + String(mediaHorariaWh, 4) + ",";
  jsonConsumo += "\"total_dia_wh\": " + String(totalDiaWh, 4) + ",";
  jsonConsumo += "\"tempo_ligado_min\": " + String(tempoLigadoMinutos, 2) + ",";
  jsonConsumo += "\"led_estado\": \"" + String(ledEstaLigado ? "ON" : "OFF") + "\"";
  jsonConsumo += "}";
  
  client.publish(topic_consumo, jsonConsumo.c_str());

  // 4. Fechamento do Dia
  if (horaSimulada == 0) {
    Serial.println(">>> FECHAMENTO DE DIA: Zerando contadores... <<<");
    consumoDiarioKwh = 0.0; 
  }
}

// --- Gerenciamento de Rede (Robusto para Wokwi) ---
void setup_wifi() {
  delay(10);
  Serial.print("\nConectando WiFi: ");
  Serial.println(ssid);

  WiFi.disconnect(); // Limpa conexões anteriores
  delay(100);
  
  // Canal 6 forçado para estabilidade no simulador
  WiFi.begin(ssid, password, 6);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 40) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Conectado! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nFALHA WIFI: Reiniciando ESP...");
    delay(2000);
    ESP.restart();
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  if (String(topic) == topic_led_cmd) {
      if (message == "ON") {
        digitalWrite(LED_PIN, HIGH);
        registrarLedLigado();
      } else if (message == "OFF") {
        digitalWrite(LED_PIN, LOW);
        registrarLedDesligado();
      }
  }
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP32Sim-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      client.subscribe(topic_led_cmd);
    } else {
      delay(5000);
    }
  }
}

// --- Setup e Loop Principal ---
void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  randomSeed(analogRead(0));
  Serial.println("=== SISTEMA INICIADO (Escala 1min = 1seg) ===");
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long now = millis();
  static unsigned long lastMsg = 0;

  // Publicação de Sensores (Intervalo definido pela simulação)
  if (now - lastMsg > VELOCIDADE_SIMULACAO) { 
    lastMsg = now;
    
    int pirValue = 0;
    int ldrValue = 0;

    #ifdef MODO_SIMULACAO_ATIVO
      DadosSimulados dados = gerarAmbienteSimulado();
      pirValue = dados.movimento;
      ldrValue = dados.luminosidade;
    #else
      pirValue = digitalRead(PIR_PIN);
      ldrValue = analogRead(LDR_PIN);
    #endif
    
    String jsonPayload = "{\"movimento\": " + String(pirValue) + 
                         ", \"luminosidade\": " + String(ldrValue) + "}";
    client.publish(topic_sensors, jsonPayload.c_str());
  }

  // Relatório de Consumo (Trigger na virada de hora)
  if (mudouDeHora) {
    mudouDeHora = false;
    publicarRelatorioHorario();
  }
}