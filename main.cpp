#include <WiFi.h>
#include <PubSubClient.h>

// Configurações de wifi e mqtt
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";

WiFiClient espClient;
PubSubClient client(espClient);

// Definição dos pinos
const int PIR_PIN = 13;
const int LED_PIN = 12;
const int LDR_PIN = 34;

// mqtt 
const char* topic_sensors = "nelson_iot/sensores";
const char* topic_led_cmd = "nelson_iot/led_cmd";

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
  Serial.println("Endereço IP: ");
  Serial.println(WiFi.localIP());
}

// Função que vai recebe os comandos do node-red para controlar o led
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Mensagem recebida no tópico [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  // Controle do led baseado no comando recebido do node-red
  if (message == "ON") {
    digitalWrite(LED_PIN, HIGH);
  } else if (message == "OFF") {
    digitalWrite(LED_PIN, LOW);
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentando conexão MQTT...");
    
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

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Onde ocorre a leitura dos sensores
  int pirState = digitalRead(PIR_PIN);
  int ldrValue = analogRead(LDR_PIN);
  
  // Onde se cria o json
  String jsonPayload = "{";
  jsonPayload += "\"movimento\": " + String(pirState) + ",";
  jsonPayload += "\"luminosidade\": " + String(ldrValue);
  jsonPayload += "}";

  // Manda os dados a cada segundo 1000ms = 1s
  static unsigned long lastMsg = 0;
  unsigned long now = millis();
  if (now - lastMsg > 1000) {
    lastMsg = now;
    client.publish(topic_sensors, jsonPayload.c_str());
  }
}
