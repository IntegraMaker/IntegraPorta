#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <SPI.h>
#include <MFRC522.h>
#include <vector>
#include "webpage.h"
#include <PubSubClient.h>

#define SS_PIN 4  //D2
#define RST_PIN 5 //D1
#define RELE_PIN D3
#define FILENAME "/Cadastro.txt"
#define FILELOG "/log.txt"


// ====== CONFIGURAÇÕES DO BROKER MQTT (Valores Padrão) ======
char mqtt_server[40] = "200.129.71.149";
char mqtt_port[6]    = "1883";
char mqtt_user[30]   = "iotsousa";
char mqtt_pass[30]   = "!IntegraMaker2025";
char tempo_leitura[10] = "1"; // Tempo em minutos

// ====== TÓPICOS MQTT (Valores Padrão) ======
char topic_volume[50] = "Sede/Integra/porta";

using namespace std;

String ssid = "IntegraMaker";
String password = "IntegraMaker2025";

// Configurações de IP fixo
IPAddress local_IP(192, 168, 1, 250);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0); // Máscara de sub-rede

const char* http_username = "admin";
const char* http_password = "!Integra1234";
const char* api_key = "api!Integra1234";

String info_data; //Informação sobre o usuario.Ex nome, cpf, etc.
String id_data;   //Id para o usuario.
int index_user_for_removal = -1;

String rfid_card = ""; //UID RFID obtido pelo Leitor
String sucess_msg = ""; 
String failure_msg = "";

// Cria um objeto  MFRC522.
MFRC522 mfrc522(SS_PIN, RST_PIN);   

// Cria um objeto AsyncWebServer que usará a porta 80
AsyncWebServer server(80);

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastReconnectAttempt = 0;

//Gravar dados 
//Inicializa o sistema de arquivos.
bool initFS() {
  if (!SPIFFS.begin()) {
    Serial.println("Erro ao abrir o sistema de arquivos");
    return false;
  }
  Serial.println("Sistema de arquivos carregado com sucesso!");
  return true;
}

//Lista todos os arquivos salvos na flash.
void listAllFiles() {
  String str = "";
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    str += dir.fileName();
    str += " / ";
    str += dir.fileSize();
    str += "\r\n";
  }
  Serial.print(str);
}

vector <String> readFile(String path) {
  vector <String> file_lines;
  String content;
  File myFile = SPIFFS.open(path.c_str(), "r");
  if (!myFile) {
    myFile.close();
    return {};
  }
  Serial.println("###################### - FILE- ############################");
  while (myFile.available()) {
    content = myFile.readStringUntil('\n');
    file_lines.push_back(content);
    Serial.println(content);
  }
  Serial.println("###########################################################");
  myFile.close();
  return file_lines;
}

//Faça a busca de um usuario pelo ID e pela INFO.
int findUser(vector <String> users_data, String id, String info) {
  String newID = "<td>" + id + "</td>";
  String newinfo = "<td>" + info + "</td>";

  for (int i = 0; i < users_data.size(); i++) {
    if (users_data[i].indexOf(newID) > 0 || users_data[i].indexOf(newinfo) > 0) {
      return i;
    }
  }
  return -1;
}

int findUID(vector <String> users_data, String id){
  String newID = "<td>" + id + "</td>";

   for (int i = 0; i < users_data.size(); i++) {
    if (users_data[i].indexOf(newID) > 0 ) {
      return i;
    }
  }
  return -1;
}
bool isAuthenticated(AsyncWebServerRequest *request) {
  return request->authenticate(http_username, http_password);
}

//Adiciona um novo usuario ao sistema
bool addNewUser(String id, String data) {
  File myFile = SPIFFS.open(FILENAME, "a+");
  if (!myFile) {
    Serial.println("Erro ao abrir arquivo!");
    myFile.close();
    return false;
  } else {
    myFile.printf("<tr><td>%s</td><td>%s</td>\n", id.c_str(), data.c_str());
    Serial.println("Arquivo gravado");
  }
  myFile.close();
  return true;
}
//Remove um usuario do sistema
bool removeUser(int user_index) {
  vector <String> users_data = readFile(FILENAME);
  if (user_index == -1)//Caso usuário não exista retorne falso
    return false;

  File myFile = SPIFFS.open(FILENAME, "w");
  if (!myFile) {
    Serial.println("Erro ao abrir arquivo!");
    myFile.close();
    return false;
  } else {
    for (int i = 0; i < users_data.size(); i++) {
      if (i != user_index)
        myFile.println(users_data[i]);
    }
    Serial.println("Usuário removido");
  }
  myFile.close();
  return true;
}
//Esta função substitui trechos de paginas html marcadas entre %
String processor(const String& var) {
  String msg = "";
  if (var == "TABLE") {
    msg = "<table><tr><td>RFID Code</td><td>User Info</td><td>Delete</td></tr>";
    vector <String> lines = readFile(FILENAME);
    for (int i = 0; i < lines.size(); i++) {
      msg += lines[i];
      msg += "<td><a href=\"get?remove=" + String(i + 1) + "\"><button>Excluir</button></a></td></tr>"; //Adiciona um botão com um link para o indice do usuário na tabela
    }
    msg += "</table>";
  }
  else if (var == "SUCESS_MSG")
    msg = sucess_msg;
  else if (var == "FAILURE_MSG")
    msg = failure_msg;
  return msg;
}



//Final Gravar dados

void extrairDados(String linha, String &id, String &info) {
  int td1_start = linha.indexOf("<td>") + 4;
  int td1_end = linha.indexOf("</td>");
  id = linha.substring(td1_start, td1_end);
  id.trim(); // remove espaços extras

  int td2_start = linha.indexOf("<td>", td1_end) + 4;
  int td2_end = linha.indexOf("</td>", td2_start);
  info = linha.substring(td2_start, td2_end);
  info.trim();
}

//Parte WEB

void saveWiFiCredentials(String ssid, String pass) {
  File file = SPIFFS.open("/wifi.txt", "w");
  if (file) {
    file.println(ssid);
    file.println(pass);
    file.close();
  }
}

bool loadWiFiCredentials() {
  File file = SPIFFS.open("/wifi.txt", "r");
  if (!file) return false;

  ssid = file.readStringUntil('\n');
  ssid.trim();
  password = file.readStringUntil('\n');
  password.trim();
  file.close();

  return true;
}

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}


//Final Parte WEB

void salvarLog(String uid, String nome, String acao) {
  String datetime = getDateTime();
  File file = SPIFFS.open(FILELOG, "a+");
  if (!file) {
    Serial.println("Erro ao abrir log.txt");
    return;
  }

  String entrada = datetime + " - " + uid +  " - " + nome + " - " + acao;
  file.println(entrada);
  file.close();

  Serial.println("Log salvo: " + entrada);
}

void abrirPorta(){

  Serial.println("Abrir" +  getDateTime());
  digitalWrite(RELE_PIN,LOW);
  delay(1000);
  Serial.println("fechar");
  digitalWrite(RELE_PIN,HIGH);

}

String getDateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Data/Hora não disponível";
  }
  char buffer[30];
  strftime(buffer, 30, "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(buffer);
}

bool iniciado = true;

void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
  String messageTemp;
  for (unsigned int i = 0; i < length; i++) {
    messageTemp += (char)payload[i];
  }
  Serial.print("MQTT Recebido [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(messageTemp);

  if (String(topic) == String(topic_volume)) {
    if (messageTemp == "true") {
      abrirPorta();
      salvarLog("MQTT", "Broker", "Abertura via MQTT");
    }
  }
}

bool reconnectMQTT() {
  Serial.print("Tentando conexao MQTT...");
  String clientId = "ESP8266Client-";
  clientId += String(random(0xffff), HEX);
  if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
    Serial.println("conectado");
    mqttClient.subscribe(topic_volume);
  } else {
    Serial.print("falhou, rc=");
    Serial.print(mqttClient.state());
    Serial.println();
  }
  return mqttClient.connected();
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  //Controle porta
  pinMode(RELE_PIN, OUTPUT);
  digitalWrite(RELE_PIN,HIGH);

  SPI.begin();
  mfrc522.PCD_Init();   // Inicia MFRC522

  //Controle de arquivo
  // Inicialize o SPIFFS
  if (!initFS())
    return;
  listAllFiles();
  readFile(FILENAME);
  FSInfo fs_info;
  SPIFFS.info(fs_info);
  Serial.printf("Total: %d bytes, Usado: %d bytes\n", fs_info.totalBytes, fs_info.usedBytes);

  // Conectando ao Wi-Fi
  Serial.println("Carregado senhas");
  loadWiFiCredentials();

    // Verifica se o arquivo existe e remove
  if (SPIFFS.exists(FILELOG)) {
    if (SPIFFS.remove(FILELOG)) {
      Serial.println("Arquivo apagado com sucesso.");
    } else {
      Serial.println("Falha ao apagar o arquivo.");
    }
  } else {
    Serial.println("Arquivo não encontrado.");
  }
  
 // Configura IP estático
  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Falha ao configurar IP estático");
  }

  ssid = "IntegraMaker";
  password = "IntegraMaker2025";

  WiFi.begin(ssid, password);
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Conectado com sucesso: " + WiFi.localIP().toString());
  }else {
    // Não conectou: iniciar modo AP
    Serial.println("Não conectado. Iniciando modo AP...");

    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP_Config", "12345678");
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP: ");
    Serial.println(IP);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      String html = "<h2>Configurar Wi-Fi</h2><form action='/save' method='POST'>";
      html += "SSID: <input name='ssid'><br>";
      html += "Senha: <input name='pass' type='password'><br>";
      html += "<input type='submit' value='Salvar e Reiniciar'>";
      html += "</form>";
      request->send(200, "text/html", html);
    });

    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
      String ssid = request->getParam("ssid", true)->value();
      String pass = request->getParam("pass", true)->value();

      saveWiFiCredentials(ssid, pass);
      request->send(200, "text/html", "<h2>Salvo! Reiniciando...</h2>");
      delay(2000);
      ESP.restart();
    });


    iniciado = false;
    server.begin();
    return;    

  }



  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");  // UTC-3 para o Brasil


  //Rotas.

  // Define rota "/"
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->redirect("/home");
  });

  // Define rota "/api/open"
  server.on("/api/open", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("key", true) && request->hasParam("nome", true)) {
      String key = request->getParam("key", true)->value();
      String nome = request->getParam("nome", true)->value();

      if (key == api_key) {
        abrirPorta();
        salvarLog("API", nome, "ENTRADA VIA API");
        request->send(200, "application/json", "{\"status\":\"ok\", \"message\":\"Porta aberta\"}");
      } else {
        request->send(403, "application/json", "{\"status\":\"erro\", \"message\":\"Chave inválida\"}");
      }
    } else {
      request->send(400, "application/json", "{\"status\":\"erro\", \"message\":\"Parâmetros ausentes (key, nome)\"}");
    }
  });

  server.on("/abrirporta", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!isAuthenticated(request)) return request->requestAuthentication();
    
    String nome = "Desconhecido";
    if (request->hasParam("nome", true)) {
      nome = request->getParam("nome", true)->value();
    }
    abrirPorta();
    salvarLog( "WEB.ADMIN",nome, "Porta aberta pelo botão da home");
    request->redirect("/home");
  });

  server.on("/home", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (!isAuthenticated(request)) return request->requestAuthentication();
    rfid_card = "";
    request->send_P(200, "text/html", home_html, processor);
  });

  server.on("/sucess", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (!isAuthenticated(request)) return request->requestAuthentication();
    request->send_P(200, "text/html", success_html, processor);
  });

  server.on("/warning", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (!isAuthenticated(request)) return request->requestAuthentication();
    request->send_P(200, "text/html", warning_html, processor);
  });

  server.on("/failure", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (!isAuthenticated(request)) return request->requestAuthentication();
    request->send_P(200, "text/html", failure_html, processor);
  });

  server.on("/deleteuser", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (!isAuthenticated(request)) return request->requestAuthentication();
    if (removeUser(index_user_for_removal)) {
      sucess_msg = "Usuário excluído do registro.";
      request->send_P(200, "text/html", success_html, processor);
    } else {
      failure_msg = "Erro ao excluir o usuário.";
      request->send_P(200, "text/html", failure_html, processor);
    }
  });

  server.on("/stylesheet.css", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/stylesheet.css", "text/css");
  });

  server.on("/rfid", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/plain", rfid_card.c_str());
  });

  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest * request) {
    vector <String> users_data = readFile(FILENAME);
    if (!isAuthenticated(request)) return request->requestAuthentication();
    if (request->hasParam("info")) {
      info_data = request->getParam("info")->value();
      info_data.toUpperCase();
      Serial.printf("info: %s\n", info_data.c_str());
    }
    if (request->hasParam("rfid")) {
      id_data = request->getParam("rfid")->value();
      Serial.printf("ID: %s\n", id_data.c_str());
    }
    if (request->hasParam("remove")) {
      String user_removed = request->getParam("remove")->value();
      Serial.printf("Remover o usuário da posição : %s\n", user_removed.c_str());
      index_user_for_removal = user_removed.toInt();
      index_user_for_removal -= 1;
      request->send(200, "text/html", warning_html);
      return;
    }
    if(id_data == "" || info_data == ""){
      failure_msg = "Informações de usuário estão incompletas.";
      request->send(200,  "text/html", failure_html, processor);
      return;
      }
    int user_index = findUser(users_data, id_data, info_data);
    if (user_index < 0) {
      Serial.println("Cadastrando novo usuário");
      addNewUser(id_data, info_data);
      sucess_msg = "Novo usuário cadastrado.";
      request->send(200, "text/html", success_html, processor);
    }
    else {
      Serial.printf("Usuário numero %d ja existe no banco de dados\n", user_index);
      failure_msg = "Ja existe um usuário cadastrado.";
      request->send(200,  "text/html", failure_html, processor);
    }
  });

  server.on("/logo.jpg", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "/logo.jpg", "image/jpg");
  });

  //LOG
  server.on("/log", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!isAuthenticated(request)) return request->requestAuthentication();
    String html = "<!DOCTYPE html><html><head><title>Log</title><meta charset='utf-8'>";
    html += "<style>body{font-family:Arial;}pre{background:#f4f4f4;padding:10px;}button{margin:10px;}</style></head><body>";
    html += "<h2>Dados do Log</h2><pre>";

    File logFile = SPIFFS.open(FILELOG, "r");
    if (!logFile) {
      html += "Erro ao abrir o log.";
    } else {
      while (logFile.available()) {
        html += logFile.readStringUntil('\n');
      }
      logFile.close();
    }

    html += "</pre>";
    html += "<a href='/download'><button>Download do Log</button></a>";
    html += "<a href='/clearlog'><button>Limpar Log</button></a>";
    html += "<br><a href='/home'>Voltar</a>";
    html += "</body></html>";

    request->send(200, "text/html", html);
  });

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!isAuthenticated(request)) return request->requestAuthentication();
    request->send(SPIFFS, FILELOG, "text/plain", true); // true = como download
  });

  server.on("/clearlog", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!isAuthenticated(request)) return request->requestAuthentication();

    // Corrigido para ler o arquivo de log
    vector<String> lines = readFile(FILELOG);

    File file = SPIFFS.open(FILELOG, "w");
    if (!file) {
      request->send(500, "text/plain", "Erro ao limpar o log.");
      return;
    }

    // Mantém apenas as últimas N linhas
    const int linhas_para_manter = 10;
    int start = max(0, (int)lines.size() - linhas_para_manter);
    for (int i = start; i < lines.size(); i++) {
      file.println(lines[i]);
    }

    file.close();
    request->redirect("/log");
  });

  server.onNotFound(notFound);
  // Inicia o serviço
  server.begin();

  mqttClient.setServer(mqtt_server, atoi(mqtt_port));
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  // put your main code here, to run repeatedly:
  if (iniciado == false){
    return;
  }

  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      if (reconnectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    mqttClient.loop();
  }

  // Procure por novos cartões.
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  //Faça a leitura do ID do cartão
  if (mfrc522.PICC_ReadCardSerial()) {
    Serial.print("UID da tag :");
    String rfid_data = "";
    for (uint8_t i = 0; i < mfrc522.uid.size; i++)
    {
      Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
      Serial.print(mfrc522.uid.uidByte[i], HEX);
      rfid_data.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
      rfid_data.concat(String(mfrc522.uid.uidByte[i], HEX));
    }
    Serial.println();
    rfid_data.toUpperCase();
    rfid_card = rfid_data;
  }

  vector <String> users_data = readFile(FILENAME);
  int user_index = findUID(users_data, rfid_card);
  if (user_index < 0) {
    Serial.printf("Usuário não existe no banco de dados\n", rfid_card);
    salvarLog(rfid_card , "Usuário não cadastrado", "Acesso negado");
  } else {
    abrirPorta();
    extrairDados(users_data[user_index], id_data, info_data);
    Serial.println(users_data[user_index]);
    salvarLog(id_data, info_data, "Acesso permitido");
   
  }
 
  Serial.println(rfid_card);
  delay(1000);

}
