/*
   Atividade 2 IOT012

   Rafael Alessi Muntsch
   rafalessi87@gmail.com

   DOIT ESP32 DEVKIT V1

   Implementação de envio de dados para ThingSpeak no ESP32 e dados na tela OLED
*/

/*******************************************************************************
    Inclusões
*******************************************************************************/
#include "DHT.h"
#include "esp_wifi.h"
#include <Arduino.h>
#include <WiFi.h>
extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include <ArduinoJson.h>
#include <AsyncMqttClient.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>

#include <Fonts/FreeSerif9pt7b.h>

#include "ThingSpeak.h"

#include <Adafruit_BMP280.h>

#include "LittleFS.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

/*******************************************************************************
    Definições de constantes e variáveis globais
*******************************************************************************/
/* Display OLED SSD1306 */
#define OLED_WIDTH (128) // largura do display OLED (pixels)
#define OLED_HEIGHT (64) // altura do display OLED (pixels)
#define OLED_ADDRESS (0x3C) // endereço I²C do display
static Adafruit_SSD1306 display // objeto de controle do SSD1306
    (OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// Configurações para acesso à rede Wifi
const char* WIFI_SSID = "iot012-Rede WiFi AP"; // Apenas 2.4 GHz
const char* WIFI_PASSWORD = "0123456789";
WiFiClient client; // precisar declarar client para o ThingSpeak

// Cria objeto do Webserver na porta 80 (padrão HTTP)
AsyncWebServer server(80);

// Variáveis String para armazenar valores da página HTML de provisionamento
String g_ssid;
String g_password;
String g_thingspeak_channel;
String g_thingspeak_key;
String g_disp;

// Caminhos dos arquivos para salvar os valores das credenciais
const char* g_ssidPath = "/ssid.txt";
const char* g_passwordPath = "/password.txt";
const char* g_dispPath = "/disp.txt";
const char* g_thingspeak_channelPath = "/channel.txt";
const char* g_thingspeak_keyPath = "/key.txt";

// Sensor DHT22
#define DHT_READ (15)
#define DHT_TYPE DHT22
DHT dht(DHT_READ, DHT_TYPE);
float g_temperature;
float g_humidity;
float g_pressure;

// Sensor bmp280
// define device I2C address: 0x76 or 0x77 (0x77 is library default address)
#define BMP280_I2C_ADDRESS 0x76
// initialize Adafruit BMP280 library
Adafruit_BMP280 bmp280;

// Controle de temporização periódica
unsigned long g_previousMillis = 0;
const long g_interval = 30000;

/*******************************************************************************
    Implementação: Funções auxiliares
*******************************************************************************/
void littlefsInit()
{
    if (!LittleFS.begin(true)) {
        Serial.println("Erro ao montar o sistema de arquivos LittleFS");
        return;
    }
    Serial.println("Sistema de arquivos LittleFS montado com sucesso.");
}

// Lê arquivos com o LittleFS
String readFile(const char* path)
{
    Serial.printf("Lendo arquivo: %s\r\n", path);

    File file = LittleFS.open(path);
    if (!file || file.isDirectory()) {
        Serial.printf("\r\nfalha ao abrir o arquivo... %s", path);
        return String();
    }

    String fileContent;
    while (file.available()) {
        fileContent = file.readStringUntil('\n');
        break;
    }
    Serial.printf("Arquivo Lido: %s\r\n", path);
    return fileContent;
}

// Escreve arquivos com o LittleFS
void writeFile(const char* path, const char* message)
{
    Serial.printf("Escrevendo arquivo: %s\r\n", path);

    File file = LittleFS.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("\r\nfalha ao abrir o arquivo... %s", path);
        return;
    }
    if (file.print(message)) {
        Serial.printf("\r\narquivo %s editado.", path);
    } else {
        Serial.printf("\r\nescrita no arquivo %s falhou... ", path);
    }
}

// Callbacks para requisições de recursos do servidor
void serverOnGetRoot(AsyncWebServerRequest* request)
{
    request->send(LittleFS, "/index.html", "text/html");
}

void serverOnGetStyle(AsyncWebServerRequest* request)
{
    request->send(LittleFS, "/style.css", "text/css");
}

void serverOnGetFavicon(AsyncWebServerRequest* request)
{
    request->send(LittleFS, "/favicon.png", "image/png");
}

void serverOnPost(AsyncWebServerRequest* request)
{
    int params = request->params();

    for (int i = 0; i < params; i++) {
        AsyncWebParameter* p = request->getParam(i);
        if (p->isPost()) {
            if (p->name() == "ssid") {
                g_ssid = p->value().c_str();
                Serial.print("SSID definido como ");
                Serial.println(g_ssid);

                // Escreve WIFI_SSID no arquivo
                writeFile(g_ssidPath, g_ssid.c_str());
            }
            if (p->name() == "password") {
                g_password = p->value().c_str();
                Serial.print("Senha definida como ");
                Serial.println(g_password);

                // Escreve WIFI_PASSWORD no arquivo
                writeFile(g_passwordPath, g_password.c_str());
            }
            if (p->name() == "disp") {
                g_disp = p->value().c_str();
                Serial.print("Dispositivo: ");
                Serial.println(g_disp);

                // Escreve disp no arquivo
                writeFile(g_dispPath, g_disp.c_str());
            }
            if (p->name() == "channel") {
                g_thingspeak_channel = p->value().c_str();
                Serial.print("Canal ThingSpeak: ");
                Serial.println(g_thingspeak_channel);

                // Escreve disp no arquivo
                writeFile(g_thingspeak_channelPath, g_thingspeak_channel.c_str());
            }
            if (p->name() == "key") {
                g_thingspeak_key = p->value().c_str();
                Serial.print("Key ThingSpeak: ");
                Serial.println(g_thingspeak_key);

                // Escreve disp no arquivo
                writeFile(g_thingspeak_keyPath, g_thingspeak_key.c_str());
            }
        }
    }
    // Após escrever no arquivo, envia mensagem de texto simples ao browser
    request->send(200, "text/plain", "Finalizado - o ESP32 vai reiniciar e se conectar ao seu AP definido.");

    // Reinicia o ESP32
    delay(2000);
    ESP.restart();
}

// Inicializa a conexão Wifi
bool initWiFi()
{
    // Se o valor de g_ssid for não-nulo, uma rede Wifi foi provida pela página do
    // servidor. Se for, o ESP32 iniciará em modo AP.
    if (g_ssid == "") {
        Serial.println("SSID indefinido (ainda não foi escrito no arquivo, ou a leitura falhou).");
        return false;
    }

    // Se há um SSID e PASSWORD salvos, conecta-se à esta rede.
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_ssid.c_str(), g_password.c_str());
    Serial.println("Conectando à Wifi...");

    unsigned long currentMillis = millis();
    g_previousMillis = currentMillis;

    while (WiFi.status() != WL_CONNECTED) {
        currentMillis = millis();
        if (currentMillis - g_previousMillis >= g_interval) {
            Serial.println("Falha em conectar.");
            return false;
        }
    }

    // Exibe o endereço IP local obtido
    Serial.println(WiFi.localIP());

    //  Iniciar ThingSpeak
    ThingSpeak.begin(client);
    Serial.println("ThingSpeak Iniciado.");

    return true;
}

// Realiza arredondamento dos valores do sensor (para 2 casas decimais)
double round2(double value)
{
    return (int)(value * 100 + 0.5) / 100.0;
}

// Lê temperatura (em Celsius) e umidade do sensor DHT22 e PRessao BMP280
esp_err_t sensorRead()
{
    g_temperature = dht.readTemperature();
    g_humidity = dht.readHumidity();
    g_pressure = bmp280.readPressure();

    // Verifica se alguma leitura falhou
    if (isnan(g_humidity) || isnan(g_temperature)) {
        Serial.printf("\r\n[sensorRead] Erro - leitura inválida...");
        return ESP_FAIL;
    } else {
        return ESP_OK;
    }
}

void sensorPublish()
{
    // Rotina para enviar para canal iot12 direto usando lib ThingSpeak
    // Envia dados à plataforma ThingSpeak. Cada dado dos sensores é setado em um campo (field) distinto.
    int errorCode;
    ThingSpeak.setField(1, g_temperature);
    ThingSpeak.setField(2, g_humidity);
    ThingSpeak.setField(3, g_pressure);
    // errorCode = ThingSpeak.writeFields((long)THINGSPEAK_CHANNEL_ID, THINGSPEAK_WRITE_API_KEY);
    errorCode = ThingSpeak.writeFields((long)g_thingspeak_channel.c_str(), g_thingspeak_key.c_str());
    if (errorCode != 200) {
        Serial.println("Erro ao atualizar os canais - código HTTP: " + String(errorCode));
    } else {
        Serial.printf("\r\n[sensorPublish] Dados publicado no ThingSpeak. Canal: %lu ", g_thingspeak_channel.c_str());
    }
}

/*******************************************************************************
    Implementação: Setup & Loop
*******************************************************************************/
void setup()
{
    // Log inicial da placa
    Serial.begin(115200);
    Serial.print("\r\n --- Exercicio Final iot12 ThingSpeak--- \n");

    // Inicia o sistema de arquivos
    littlefsInit();

    // Configura LED_BUILTIN (GPIO2) como pino de saída
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    // Carrega os valores lidos com o LittleFS
    g_ssid = readFile(g_ssidPath);
    Serial.println(g_ssid);
    g_password = readFile(g_passwordPath);
    Serial.println(g_password);
    g_disp = readFile(g_dispPath);
    Serial.println(g_disp);
    g_thingspeak_channel = readFile(g_thingspeak_channelPath);
    Serial.println(g_thingspeak_channel);
    g_thingspeak_key = readFile(g_thingspeak_keyPath);
    Serial.println(g_thingspeak_key);

    // iniciar display
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
    display.setTextColor(WHITE);
    display.clearDisplay();
    display.display();

    // Inicializa o sensor DHT11
    dht.begin();

    // Inicializa sensor bmp280
    bmp280.begin(BMP280_I2C_ADDRESS);

    // verificar se está conectado a um AP, senão cria um
    if (!initWiFi()) {
        // Seta o ESP32 para o modo AP
        WiFi.mode(WIFI_AP);
        WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);

        Serial.print("Access Point criado com endereço IP ");
        Serial.println(WiFi.softAPIP());

        // Callbacks da página principal do servidor de provisioning
        server.on("/", HTTP_GET, serverOnGetRoot);
        server.on("/style.css", HTTP_GET, serverOnGetStyle);
        server.on("/favicon.png", HTTP_GET, serverOnGetFavicon);

        // Ao clicar no botão "Enviar" para enviar as credenciais, o servidor receberá uma
        // requisição do tipo POST, tratada a seguir
        server.on("/", HTTP_POST, serverOnPost);

        // Como ainda não há credenciais para acessar a rede wifi,
        // Inicia o Webserver em modo AP
        server.begin();

        // Limpa a tela do display e mostra o nome do exemplo
        display.clearDisplay();

        // Mostra nome do dispositivo
        display.setCursor(0, 0);
        display.printf("Acesse a rede '%s'.\nUtilize a senha '%s'.\n", WIFI_SSID, WIFI_PASSWORD);
        // Atualiza tela do display OLED
        display.display();
    }
}

void loop()
{
    unsigned long currentMillis = millis();

    if (WiFi.status() == WL_CONNECTED && WiFi.getMode() == WIFI_MODE_STA) {
        // A cada "interval" ms, publica dados em tópicos adequados
        if (currentMillis - g_previousMillis >= g_interval) {

            g_previousMillis = currentMillis;
            // Lê dados do sensor e publica se a leitura não falhou
            if (sensorRead() == ESP_OK) {
                sensorPublish();
            }

            // Limpa a tela do display e mostra o nome do exemplo
            display.clearDisplay();

            // Mostra nome do dispositivo
            display.setCursor(0, 0);
            display.printf("%s", g_disp.c_str());

            // Mostra Temperatura no display
            display.drawRoundRect(0, 8, 126, 16, 6, WHITE);
            display.setCursor(4, 12);
            display.printf("Temperatura: %0.1fC", dht.readTemperature());

            // Mostra Humidade no display
            display.drawRoundRect(0, 26, 126, 16, 6, WHITE);
            display.setCursor(4, 30);
            display.printf("Umidade: %0.1f%", dht.readHumidity());

            // Mostra pressao no display
            display.drawRoundRect(0, 46, 126, 16, 6, WHITE);
            display.setCursor(4, 50);
            display.printf("Pressao: %0.1fhPa", bmp280.readPressure());

            // Atualiza tela do display OLED
            display.display();
        }
    } else {
        if (currentMillis - g_previousMillis >= g_interval) {

            g_previousMillis = currentMillis;
        }
        digitalWrite(LED_BUILTIN, HIGH);
        delay(1000);
        digitalWrite(LED_BUILTIN, LOW);
        delay(1000);
    }
}
