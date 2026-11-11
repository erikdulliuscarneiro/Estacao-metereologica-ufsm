/*
  ==========================================================================
  ESTACAO METEOROLOGICA AUTOMATICA - VERSAO 4G  (revisao 6)
  LilyGO T-A7670G | internet pelo chip da VIVO | backup em CSV no MicroSD
  Leitura a cada 1 minuto.

  --------------------------------------------------------------------------
  O QUE MUDOU NESTA REVISAO
  --------------------------------------------------------------------------
  1) CHUVA - antes a coluna era a chuva do minuto, porque o contador era
     zerado a cada volta do loop. Agora ela ACUMULA o dia inteiro e zera
     na virada da meia-noite. O total fica guardado no cartao, entao se a
     placa reiniciar no meio do dia ela volta de onde parou.

  2) RELOGIO - antes a hora era acertada uma unica vez, no boot, e ia
     derivando. Agora ela e reacertada pela rede 4G a cada 6 horas. Isso e
     necessario porque o reset diario da chuva depende do relogio.

  3) HORA UNICA - a data e a hora agora sao geradas pela placa e enviadas
     ao Google. Antes a planilha usava o relogio do servidor e o cartao
     usava o da placa, e os dois arquivos nunca batiam.

  4) NIVEL DA AGUA - nova coluna, lida do GPIO 39.

  5) STATUS - seis colunas novas no fim, dizendo se cada sensor esta
     respondendo. No cartao tem ainda uma setima, dizendo se o envio para
     o Google funcionou naquele minuto.

  6) CORRIGIDO NA REVISAO 5 - os sensores estavam todos dando ERRO. Ao
     reorganizar o setup nas revisoes anteriores, a inicializacao do BMP280,
     do DHT22 e dos analogicos passou a acontecer ANTES de ligar o GPIO 12,
     que e quem alimenta os perifericos da placa. Agora esse pino e ligado
     na primeira linha do setup, antes de qualquer sensor.

  7) CORRIGIDO NA REVISAO 4 - o status do pluviometro e do anemometro
     acusava TRAVADO mesmo com o sensor funcionando. A verificacao olhava o
     pino por apenas 50 ms, e nesse intervalo ela podia cair inteira dentro
     da parte baixa de um pulso. Agora, se chegou pulso no minuto, responde
     OK na hora; so sem pulso nenhum e que ele testa o pino, por 400 ms.

  8) CORRIGIDO NA REVISAO 4 - o arquivo do cartao estava misturando formatos.
     O codigo so criava cabecalho quando o arquivo nao existia, entao um
     dados.csv deixado pelo firmware antigo ficava com cabecalho de 10
     colunas recebendo linhas de 18. Agora o arquivo tem nome novo
     (estacao.csv) e o codigo ainda confere o cabecalho: se achar um de
     outra versao, abre estacao1.csv, estacao2.csv e assim por diante.

  9) REVERTIDO NA REVISAO 6 - eu tinha colocado uma regra que impedia o
     envio quando o relogio estava sem hora valida. Isso nao existia no
     codigo original e so criava mais um motivo para nada chegar na
     planilha. Agora, se a rede estiver de pe, ele manda - com a hora que
     tiver.

 10) CORRIGIDO NA REVISAO 3 - o envio estava gravando a mesma leitura tres
     vezes na planilha. A causa era o codigo seguir o redirecionamento que o
     Apps Script devolve: quando ele responde 302 a linha JA foi gravada, e
     ir atras do destino fazia o script rodar de novo. Agora o 302 e tratado
     como sucesso e o envio para ali.

  A direcao do vento continua usando a tabela antiga, como combinado.
  A coluna "Direcao do vento" sempre traz um ponto cardeal, nunca ERRO.

  --------------------------------------------------------------------------
  ANTES DE SUBIR ESTE CODIGO
  --------------------------------------------------------------------------
  . O arquivo do cartao agora se chama estacao.csv, nome novo, para nao
    esbarrar em nenhum dados.csv que o codigo antigo tenha deixado la.
    Alem disso o codigo confere o cabecalho: se achar um estacao.csv de
    outra versao, abre um estacao1.csv em vez de misturar os formatos.
  . Cole o cabecalho novo na linha 1 da planilha (esta no fim deste arquivo).
  . Atualize o Apps Script (tambem esta no fim) e republique em
    Implantar > Gerenciar implantacoes > lapis > Nova versao.
    NUNCA "Nova implantacao", senao o endereco muda.

  --------------------------------------------------------------------------
  SENSORES
  --------------------------------------------------------------------------
   DHT22 AM2302   GPIO 32            temperatura do ar + umidade relativa
   BMP280         GPIO 21 (SDA)      pressao atmosferica + temperatura
                  GPIO 22 (SCL)
   Pluviometro    GPIO 18            PB10 - 0,25 mm por pulso
   Anemometro     GPIO 19            SVDV10 - fio VERMELHO no 19, PRETO no GND
   Biruta         GPIO 34            SVDV10 - 3V3 -> resistor 4,7k -> ponto A
                                     ATENCAO: alimentar pelo 3V3, NUNCA 5V
   Nivel da agua  GPIO 39            4-20 mA sobre resistor de 150 ohms
   MicroSD        GPIO 2, 13, 14, 15 (fixos da placa)
   Modem 4G       GPIO 26, 27, 4, 12, 5
  ==========================================================================
*/

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_BMP280.h>
#include <time.h>

// ========================= CONFIGURACAO DO USUARIO =========================
#define APN_NOME   "zap.vivo.com.br"
#define APN_USER   "vivo"
#define APN_SENHA  "vivo"

const char* WEB_APP_URL =
  "https://script.google.com/macros/s/AKfycbzH0ZeukOoM7IOtT2VWq2O4hGv_XOM9PwpFP9eYX4RE5dS6EO78dMO8R7U7i6HbqZcC/exec";

const unsigned long INTERVALO_MS = 60000UL;   // 1 minuto
// ==========================================================================

// ---- Fatores de calibracao ----
const float MM_POR_PULSO   = 0.25;    // pluviometro PB10 (fabricante)
const float MS_POR_PULSO_S = 1.847;   // anemometro SVDV10 (raio 147 mm)

// ---- Sensor de nivel (laco de 4-20 mA sobre 150 ohms) ----
const float NIVEL_RESISTOR = 150.0;   // ohms
const float NIVEL_FAIXA_M  = 5.0;     // faixa do sensor, em metros
const float NIVEL_V_ZERO   = 0.5941;  // tensao medida com o sensor fora d'agua
const float NIVEL_V_CHEIO  = 3.0000;  // 20 mA x 150 ohms
const float NIVEL_MA_MIN   = 3.5;     // abaixo disso o laco esta aberto
const float NIVEL_MA_MAX   = 21.0;    // acima disso ha curto ou defeito

// ---- Faixa valida do ADC da biruta (as 8 posicoes vao de 218 a 1909) ----
const int BIRUTA_ADC_MIN = 150;
const int BIRUTA_ADC_MAX = 2400;

// ---- Tempo sem pulso a partir do qual o sensor fica suspeito ----
const unsigned long SEM_PULSO_VENTO_MS = 3UL * 3600UL * 1000UL;        // 3 h
const unsigned long SEM_PULSO_CHUVA_MS = 7UL * 24UL * 3600UL * 1000UL; // 7 dias

// ---- Ressincronizacao do relogio ----
const unsigned long RESINCRONIZA_MS = 6UL * 3600UL * 1000UL;           // 6 h

// ---- Pinos do modem ----
#define POWERON_PIN 12
#define PWRKEY_PIN   4
#define RESET_PIN    5
#define MODEM_RX    27
#define MODEM_TX    26

// ---- Pinos do cartao ----
#define SD_SCLK 14
#define SD_MISO  2
#define SD_MOSI 15
#define SD_CS   13

// ---- Pinos dos sensores ----
#define PIN_DHT         32
#define TIPO_DHT        DHT22
#define I2C_SDA         21
#define I2C_SCL         22
#define PIN_PLUVIOMETRO 18
#define PIN_ANEMOMETRO  19
#define PIN_DIRECAO     34
#define PIN_NIVEL       39

const char* ARQ_CHUVA  = "/chuva.txt";
String arquivoAtual = "/estacao.csv";  // definido no setup por escolherArquivo()

const char* CABECALHO =
  "Data,Hora,Temperatura do ar (C),Umidade relativa (%),Temperatura BMP280 (C),"
  "Pressao atmosferica (hPa),Precipitacao do dia (mm),Velocidade do vento (m/s),"
  "Direcao do vento (graus),Direcao do vento,Nivel da agua (m),"
  "Status DHT22,Status BMP280,Status pluviometro,Status anemometro,"
  "Status biruta,Status nivel,Envio";

// ==========================================================================
//  TABELA DA BIRUTA (a antiga, como combinado - sera refeita depois)
//  ADC medido: NO=218 O=264 SO=325 S=407 SE=532 L=720 NE=1081 N=1909
// ==========================================================================
struct DirVento { int adc_max; int graus; const char* nome; };
const DirVento DIRECOES[] = {
  {  241, 315, "NO" }, {  295, 270, "O"  }, {  366, 225, "SO" },
  {  470, 180, "S"  }, {  626, 135, "SE" }, {  900,  90, "L"  },
  { 1495,  45, "NE" }, { 4095,   0, "N"  }
};
const int N_DIRECOES = sizeof(DIRECOES) / sizeof(DIRECOES[0]);

DHT dht(PIN_DHT, TIPO_DHT);
Adafruit_BMP280 bmp;
bool  bmpOk = false, sdOk = false, redeOk = false;
uint8_t enderecoBmp = 0x76;

volatile unsigned long contaChuva = 0, contaVento = 0;
volatile unsigned long ultimoChuva = 0, ultimoVento = 0;
unsigned long marcoAnterior = 0;
unsigned long ultimaSincronizacao = 0;

// Acumulado de chuva do dia
float chuvaDia      = 0.0;
int   diaAcumulado  = -1;      // dia do mes a que o acumulado se refere

void IRAM_ATTR isrChuva() {
  unsigned long a = millis();
  if (a - ultimoChuva > 100) { contaChuva++; ultimoChuva = a; }
}
void IRAM_ATTR isrVento() {
  unsigned long a = millis();
  if (a - ultimoVento > 8) { contaVento++; ultimoVento = a; }
}

// ============================ COMANDOS AT =================================
String at(const char* cmd, unsigned long espera = 2000, bool mostrar = false) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  String r = "";
  unsigned long t0 = millis();
  while (millis() - t0 < espera) {
    while (Serial1.available()) r += (char)Serial1.read();
    if (r.indexOf("OK") >= 0 || r.indexOf("ERROR") >= 0) break;
  }
  r.trim();
  if (mostrar) {
    String s = r; s.replace("\r\n", " | ");
    Serial.printf("   %-34s -> %s\n", cmd, s.c_str());
  }
  return r;
}
String at(const String &cmd, unsigned long espera = 2000, bool mostrar = false) {
  return at(cmd.c_str(), espera, mostrar);
}

// ============================ LIGAR O MODEM ===============================
bool ligarModem() {
  pinMode(POWERON_PIN, OUTPUT);
  digitalWrite(POWERON_PIN, HIGH);
  delay(500);

  pinMode(RESET_PIN, OUTPUT);
  digitalWrite(RESET_PIN, LOW);  delay(100);
  digitalWrite(RESET_PIN, HIGH); delay(2600);
  digitalWrite(RESET_PIN, LOW);
  delay(500);

  Serial1.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  for (int tentativa = 1; tentativa <= 3; tentativa++) {
    Serial.printf("Ligando o modem (tentativa %d)", tentativa);
    pinMode(PWRKEY_PIN, OUTPUT);
    digitalWrite(PWRKEY_PIN, LOW);  delay(100);
    digitalWrite(PWRKEY_PIN, HIGH); delay(1000);   // pulso longo: obrigatorio
    digitalWrite(PWRKEY_PIN, LOW);
    delay(3000);

    for (int i = 0; i < 20; i++) {
      if (at("AT", 800).indexOf("OK") >= 0) {
        Serial.println("  ligado!");
        at("ATE0");
        return true;
      }
      Serial.print(".");
    }
    Serial.println("  sem resposta.");
  }
  return false;
}

// ========================= CONECTAR NA INTERNET ===========================
bool conectarRede() {
  Serial.println("Conferindo o chip...");
  if (at("AT+CPIN?", 3000).indexOf("READY") < 0) {
    Serial.println(">> Chip nao detectado. Confira o encaixe no slot.");
    return false;
  }

  Serial.println("Configurando o APN da Vivo...");
  at("AT+CGDCONT=1,\"IP\",\"" APN_NOME "\"");
  at("AT+CGAUTH=1,1,\"" APN_SENHA "\",\"" APN_USER "\"");

  Serial.println("Reiniciando o radio para aplicar o APN...");
  at("AT+CFUN=0", 10000);
  delay(2000);
  at("AT+CFUN=1", 10000);
  delay(5000);

  Serial.print("Registrando na rede");
  bool registrado = false;
  for (int i = 0; i < 40; i++) {
    String r = at("AT+CEREG?", 2000);
    if (r.indexOf(",1") > 0 || r.indexOf(",5") > 0) { registrado = true; break; }
    Serial.print(".");
    delay(2000);
  }
  if (!registrado) {
    Serial.println("\n>> Nao registrou na rede.");
    at("AT+CPSI?", 3000, true);
    return false;
  }
  Serial.println(" registrado!");
  at("AT+CPSI?", 3000, true);

  Serial.println("Abrindo a conexao de dados...");
  at("AT+NETOPEN", 20000);
  delay(2000);
  if (at("AT+IPADDR", 5000, true).indexOf("ERROR") >= 0) {
    Serial.println(">> Nao conseguiu IP.");
    return false;
  }

  at("AT+CSSLCFG=\"sslversion\",0,4");
  at("AT+CSSLCFG=\"authmode\",0,0");
  return true;
}

// ===================== HORA VINDA DA REDE DA OPERADORA ====================
// Formato: +CCLK: "aa/MM/dd,hh:mm:ss+zz"  (zz em quartos de hora)
bool sincronizarHora() {
  String r = at("AT+CCLK?", 3000);
  int p = r.indexOf("+CCLK: \"");
  if (p < 0) return false;
  String s = r.substring(p + 8);

  int ano = s.substring(0, 2).toInt() + 2000;
  int mes = s.substring(3, 5).toInt();
  int dia = s.substring(6, 8).toInt();
  int hh  = s.substring(9, 11).toInt();
  int mm  = s.substring(12, 14).toInt();
  int ss  = s.substring(15, 17).toInt();
  int tz  = s.substring(18, 20).toInt();
  if (s.charAt(17) == '-') tz = -tz;

  if (ano < 2024 || mes < 1 || mes > 12 || dia < 1 || dia > 31) return false;

  struct tm t = {0};
  t.tm_year = ano - 1900; t.tm_mon = mes - 1; t.tm_mday = dia;
  t.tm_hour = hh; t.tm_min = mm; t.tm_sec = ss; t.tm_isdst = 0;

  time_t local_rede = mktime(&t);
  time_t utc        = local_rede - (time_t)tz * 15 * 60;
  time_t brasilia   = utc - 3 * 3600;

  struct timeval tv = { brasilia, 0 };
  settimeofday(&tv, NULL);

  struct tm* v = localtime(&brasilia);
  Serial.printf("Hora da rede: %02d/%02d/%04d %02d:%02d:%02d\n",
                v->tm_mday, v->tm_mon + 1, v->tm_year + 1900,
                v->tm_hour, v->tm_min, v->tm_sec);
  return true;
}

bool relogioValido() {
  time_t agora = time(NULL);
  struct tm* t = localtime(&agora);
  return (t->tm_year + 1900) >= 2024;
}

void pegarDataHora(char* data, char* hora) {
  time_t agora = time(NULL);
  struct tm* t = localtime(&agora);
  sprintf(data, "%02d/%02d/%04d", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
  sprintf(hora, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
}

// ================== ESCOLHE O ARQUIVO CERTO NO CARTAO =====================
// Um dados.csv de versao antiga tem cabecalho diferente. Gravar linhas novas
// nele deixa o arquivo com duas estruturas misturadas e sem como separar.
// Aqui procuramos um arquivo cujo cabecalho seja igual ao desta versao; se
// nao houver, criamos o proximo numerado.
String escolherArquivo() {
  for (int i = 0; i < 100; i++) {
    String nome = (i == 0) ? String("/estacao.csv")
                           : ("/estacao" + String(i) + ".csv");

    if (!SD.exists(nome)) {
      File f = SD.open(nome, FILE_WRITE);
      if (f) {
        f.println("sep=,");      // faz o Excel abrir em colunas
        f.println(CABECALHO);
        f.close();
        Serial.println("Arquivo criado: " + nome);
      }
      return nome;
    }

    File f = SD.open(nome, FILE_READ);
    if (!f) continue;
    String l1 = f.readStringUntil('\n'); l1.trim();
    String l2 = f.readStringUntil('\n'); l2.trim();
    f.close();
    String cab = l1.startsWith("sep=") ? l2 : l1;

    if (cab == String(CABECALHO)) {
      Serial.println("Continuando no arquivo: " + nome);
      return nome;
    }
    Serial.println("O arquivo " + nome + " e de outra versao, procurando outro...");
  }
  return "/estacao.csv";
}

// ===================== ACUMULADO DE CHUVA NO CARTAO =======================
// Guarda "dia,milimetros" num arquivinho separado. Serve para o total do
// dia nao se perder se a placa reiniciar.
void salvarChuva() {
  if (!sdOk) return;
  File f = SD.open(ARQ_CHUVA, FILE_WRITE);     // FILE_WRITE aqui trunca
  if (!f) return;
  f.printf("%d,%.2f\n", diaAcumulado, chuvaDia);
  f.close();
}

void carregarChuva() {
  if (!sdOk || !relogioValido()) return;
  if (!SD.exists(ARQ_CHUVA)) return;

  File f = SD.open(ARQ_CHUVA, FILE_READ);
  if (!f) return;
  String linha = f.readStringUntil('\n');
  f.close();

  int v = linha.indexOf(',');
  if (v < 0) return;
  int   dia = linha.substring(0, v).toInt();
  float mm  = linha.substring(v + 1).toFloat();

  // se o arquivo vier estranho, simplesmente comeca do zero
  if (dia < 1 || dia > 31 || mm < 0 || mm > 2000) {
    Serial.println("Arquivo de chuva ilegivel. Comecando do zero.");
    return;
  }

  time_t agora = time(NULL);
  struct tm* t = localtime(&agora);
  if (t->tm_mday == dia) {
    chuvaDia     = mm;
    diaAcumulado = dia;
    Serial.printf("Acumulado de chuva recuperado do cartao: %.2f mm\n", mm);
  } else {
    Serial.println("O acumulado salvo e de outro dia. Comecando do zero.");
  }
}

// ============================== AUXILIARES ================================
String num(float v, int casas) {
  if (isnan(v)) return "";
  return String(v, casas);
}

// Troca espaco por %20 para o valor nao quebrar a URL
String enc(const String &s) {
  String r = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == ' ') r += "%20";
    else          r += c;
  }
  return r;
}

// Devolve o ADC medio e preenche graus e cardeal
int lerDirecaoVento(int &graus, String &cardeal) {
  long soma = 0;
  for (int i = 0; i < 20; i++) { soma += analogRead(PIN_DIRECAO); delay(3); }
  int adc = soma / 20;
  for (int i = 0; i < N_DIRECOES; i++) {
    if (adc <= DIRECOES[i].adc_max) {
      graus = DIRECOES[i].graus; cardeal = String(DIRECOES[i].nome);
      return adc;
    }
  }
  graus = 0; cardeal = "N";
  return adc;
}

// Le o nivel e devolve a corrente do laco em mA (para o diagnostico)
float lerNivel(float &metros) {
  long soma = 0;
  for (int i = 0; i < 20; i++) { soma += analogRead(PIN_NIVEL); delay(3); }
  float adc     = soma / 20.0;
  float tensao  = adc * 3.3 / 4095.0;
  float corrente_mA = (tensao / NIVEL_RESISTOR) * 1000.0;

  metros = (tensao - NIVEL_V_ZERO) / (NIVEL_V_CHEIO - NIVEL_V_ZERO) * NIVEL_FAIXA_M;
  if (metros < 0) metros = 0;
  return corrente_mA;
}

// O DHT depende de temporizacao precisa; os pulsos do anemometro e do
// pluviometro atrapalham a leitura e causam "nan".
void lerDHT(float &temp, float &umid) {
  detachInterrupt(digitalPinToInterrupt(PIN_PLUVIOMETRO));
  detachInterrupt(digitalPinToInterrupt(PIN_ANEMOMETRO));
  temp = NAN; umid = NAN;
  for (int i = 0; i < 3; i++) {
    temp = dht.readTemperature();
    umid = dht.readHumidity();
    if (!isnan(temp) && !isnan(umid)) break;
    delay(300);
  }
  attachInterrupt(digitalPinToInterrupt(PIN_PLUVIOMETRO), isrChuva, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_ANEMOMETRO),  isrVento, FALLING);
}

// ============================== DIAGNOSTICO ===============================
// O BMP280 fica no barramento I2C, entao da para perguntar se ele responde.
bool bmpResponde() {
  Wire.beginTransmission(enderecoBmp);
  return (Wire.endTransmission() == 0);
}

// Sensores de pulso: zero pulso e leitura legitima (nao choveu, nao ventou),
// entao nao da para acusar ERRO. O que da para detectar e o contato preso
// em nivel baixo, que ai sim e defeito.
String statusPulso(int pino, unsigned long pulsos,
                   unsigned long ultimoPulso, unsigned long limite) {
  // Se chegou pulso neste minuto, o sensor esta trabalhando. Responde na
  // hora, sem olhar o pino - era aqui que dava o falso TRAVADO.
  if (pulsos > 0) return "OK";

  // Sem pulso nenhum: ai sim vale olhar o pino. A janela precisa ser bem
  // maior que o periodo de um pulso, senao ela cai dentro da parte baixa
  // e acusa travado sem ser.
  bool viuAlto = false;
  for (int i = 0; i < 40; i++) {
    if (digitalRead(pino) == HIGH) { viuAlto = true; break; }
    delay(10);
  }
  if (!viuAlto) return "TRAVADO";

  if (ultimoPulso == 0) {                      // nunca veio pulso desde o boot
    return (millis() > limite) ? "SEM PULSO" : "OK";
  }
  return (millis() - ultimoPulso > limite) ? "SEM PULSO" : "OK";
}

// ============================== ENVIO =====================================
// (nao usada desde a revisao 3 - mantida caso precise depurar cabecalhos)
String acharLocation(const String &cabecalhos) {
  int p = cabecalhos.indexOf("Location:");
  if (p < 0) p = cabecalhos.indexOf("location:");
  if (p < 0) return "";
  int fim = cabecalhos.indexOf('\n', p);
  if (fim < 0) fim = cabecalhos.length();
  String loc = cabecalhos.substring(p + 9, fim);
  loc.trim();
  return loc;
}

int codigoDaAcao(const String &resposta) {
  int p = resposta.indexOf("+HTTPACTION:");
  if (p < 0) return -1;
  int v1 = resposta.indexOf(',', p);
  int v2 = resposta.indexOf(',', v1 + 1);
  if (v1 < 0 || v2 < 0) return -1;
  return resposta.substring(v1 + 1, v2).toInt();
}

// O Apps Script responde 302 e redireciona para script.googleusercontent.com.
// O modem NAO segue o redirecionamento sozinho, entao fazemos na mao.
bool enviarSheets(const String &data, const String &hora,
                  float tAr, float umid, float tBmp, float press,
                  float chuva, float vento, int graus, const String &cardeal,
                  float nivel, bool nivelOk,
                  const String &stDht, const String &stBmp,
                  const String &stPluv, const String &stAnem,
                  const String &stBir, const String &stNiv) {

  String url = String(WEB_APP_URL) +
               "?data="     + data +
               "&hora="     + hora +
               "&tempdht="  + num(tAr, 1) +
               "&umidade="  + num(umid, 1) +
               "&tempbmp="  + num(tBmp, 1) +
               "&pressao="  + num(press, 1) +
               "&chuva="    + num(chuva, 2) +
               "&vento="    + num(vento, 2) +
               "&graus="    + String(graus) +
               "&direcao="  + cardeal +
               "&nivel="    + (nivelOk ? num(nivel, 3) : String("")) +
               "&stdht="    + enc(stDht) +
               "&stbmp="    + enc(stBmp) +
               "&stpluv="   + enc(stPluv) +
               "&stanem="   + enc(stAnem) +
               "&stbir="    + enc(stBir) +
               "&stniv="    + enc(stNiv);

  at("AT+HTTPTERM", 2000);
  at("AT+HTTPINIT", 5000);

  for (int salto = 0; salto < 3; salto++) {
    at("AT+HTTPPARA=\"URL\",\"" + url + "\"", 5000);
    String r = at("AT+HTTPACTION=0", 30000);

    unsigned long t0 = millis();
    while (millis() - t0 < 20000 && r.indexOf("+HTTPACTION:") < 0) {
      while (Serial1.available()) r += (char)Serial1.read();
      delay(50);
    }

    int codigo = codigoDaAcao(r);
    Serial.printf("   HTTP %d\n", codigo);

    // ATENCAO - nao seguir o redirecionamento.
    // Quando o Apps Script responde 302, a linha JA FOI GRAVADA na planilha.
    // O 302 so aponta para a pagina que mostra o texto de resposta. Se a
    // gente for atras dele, o endereco de destino chama o script outra vez
    // e a mesma leitura entra duplicada (ou triplicada, se o laco repetir).
    // Era exatamente o que estava acontecendo.
    if (codigo == 200 || codigo == 301 || codigo == 302 ||
        codigo == 303 || codigo == 307) {
      at("AT+HTTPTERM", 3000);
      return true;
    }

    Serial.printf("   Falhou (codigo %d).\n", codigo);
    break;
  }

  at("AT+HTTPTERM", 3000);
  return false;
}

// ================================= SETUP ==================================
void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n===== ESTACAO METEOROLOGICA 4G - revisao 6 =====\n");

  // ---- ALIMENTACAO DOS PERIFERICOS - TEM QUE SER A PRIMEIRA COISA ----
  // O GPIO 12 liga a alimentacao dos perifericos da placa: cartao microSD e
  // o 3V3 que vai para os sensores. Ate a revisao 4 isso so acontecia la
  // dentro de ligarModem(), que roda depois - entao o BMP280, o DHT22, a
  // biruta e o sensor de nivel eram inicializados ainda sem alimentacao e
  // davam ERRO. No codigo antigo o modem era ligado antes dos sensores, e
  // por isso o problema nao aparecia.
  pinMode(POWERON_PIN, OUTPUT);
  digitalWrite(POWERON_PIN, HIGH);
  delay(500);

  // Sensores de pulso
  pinMode(PIN_PLUVIOMETRO, INPUT_PULLUP);
  pinMode(PIN_ANEMOMETRO,  INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_PLUVIOMETRO), isrChuva, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_ANEMOMETRO),  isrVento, FALLING);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_DIRECAO, ADC_11db);
  analogSetPinAttenuation(PIN_NIVEL,   ADC_11db);
  dht.begin();

  // BMP280
  Wire.begin(I2C_SDA, I2C_SCL);
  if (bmp.begin(0x76))      { bmpOk = true; enderecoBmp = 0x76; }
  else if (bmp.begin(0x77)) { bmpOk = true; enderecoBmp = 0x77; }
  if (bmpOk) {
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL, Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16, Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
    Serial.printf("BMP280: OK (endereco 0x%02X)\n", enderecoBmp);
  } else {
    Serial.println("BMP280: nao encontrado (confira SDA=21, SCL=22 e o 3V3).");
  }

  // MicroSD
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS)) {
    sdOk = true;
    Serial.println("MicroSD: OK");
    arquivoAtual = escolherArquivo();
  } else {
    Serial.println("MicroSD: falha (cartao ausente ou nao formatado em FAT32).");
  }

  // Modem, internet e relogio
  if (ligarModem()) {
    redeOk = conectarRede();
    if (redeOk && sincronizarHora()) ultimaSincronizacao = millis();
  } else {
    Serial.println(">> O modem nao ligou. Verifique a ALIMENTACAO.");
    Serial.println("   A estacao segue gravando so no cartao.");
  }

  if (!relogioValido())
    Serial.println(">> ATENCAO: relogio sem hora valida. O acumulado do dia "
                   "so comeca a valer depois que a hora for acertada.");

  // Acumulado de chuva (precisa do cartao e do relogio ja prontos)
  carregarChuva();

  Serial.println("\nSetup concluido.\n");
  marcoAnterior = millis();
}

// ================================== LOOP ==================================
void loop() {
  if (millis() - marcoAnterior < INTERVALO_MS) return;

  unsigned long agora = millis();
  float segundos = (agora - marcoAnterior) / 1000.0;
  marcoAnterior = agora;

  // ---- 0) reacerta o relogio de tempos em tempos ----
  if (redeOk && (millis() - ultimaSincronizacao > RESINCRONIZA_MS ||
                 !relogioValido())) {
    if (sincronizarHora()) ultimaSincronizacao = millis();
  }

  // ---- 1) pega os contadores de pulso ----
  noInterrupts();
  unsigned long pulsosChuva = contaChuva; contaChuva = 0;
  unsigned long pulsosVento = contaVento; contaVento = 0;
  unsigned long ultChuva = ultimoChuva;
  unsigned long ultVento = ultimoVento;
  interrupts();

  // ---- 2) chuva: acumula no dia e zera na virada ----
  float chuvaMinuto = pulsosChuva * MM_POR_PULSO;
  bool  precisaSalvar = false;

  if (relogioValido()) {
    time_t t_agora = time(NULL);
    struct tm* t = localtime(&t_agora);
    if (diaAcumulado == -1) {
      diaAcumulado = t->tm_mday;
    } else if (t->tm_mday != diaAcumulado) {
      Serial.printf("Virou o dia. Chuva do dia anterior: %.2f mm\n", chuvaDia);
      chuvaDia     = 0.0;
      diaAcumulado = t->tm_mday;
      precisaSalvar = true;
    }
  }
  if (chuvaMinuto > 0) { chuvaDia += chuvaMinuto; precisaSalvar = true; }
  if (precisaSalvar) salvarChuva();

  // ---- 3) demais sensores ----
  float tAr, umid;
  lerDHT(tAr, umid);

  float tBmp = NAN, press = NAN;
  if (bmpOk) { tBmp = bmp.readTemperature(); press = bmp.readPressure() / 100.0; }

  float vento = (segundos > 0) ? (pulsosVento / segundos) * MS_POR_PULSO_S : 0;

  int graus; String cardeal;
  int adcBiruta = lerDirecaoVento(graus, cardeal);

  float nivel;
  float correnteNivel = lerNivel(nivel);
  bool  nivelOk = (correnteNivel >= NIVEL_MA_MIN && correnteNivel <= NIVEL_MA_MAX);

  // ---- 4) diagnostico ----
  String stDht  = (isnan(tAr) || isnan(umid)) ? "ERRO" : "OK";
  String stBmp  = (bmpOk && bmpResponde()) ? "OK" : "ERRO";
  String stBir  = (adcBiruta >= BIRUTA_ADC_MIN && adcBiruta <= BIRUTA_ADC_MAX)
                  ? "OK" : "ERRO";
  String stNiv  = nivelOk ? "OK" : "ERRO";
  String stPluv = statusPulso(PIN_PLUVIOMETRO, pulsosChuva, ultChuva, SEM_PULSO_CHUVA_MS);
  String stAnem = statusPulso(PIN_ANEMOMETRO,  pulsosVento, ultVento, SEM_PULSO_VENTO_MS);

  char data[12], hora[12];
  pegarDataHora(data, hora);

  Serial.printf("%s %s | Ar %.1f C %.1f %% | BMP %.1f C %.1f hPa | "
                "Chuva do dia %.2f mm | Vento %.2f m/s | Dir %d (%s, adc %d) | "
                "Nivel %.3f m (%.2f mA)\n",
                data, hora, tAr, umid, tBmp, press, chuvaDia, vento,
                graus, cardeal.c_str(), adcBiruta, nivel, correnteNivel);
  Serial.printf("   status  DHT %s | BMP %s | Pluv %s | Anem %s | Biruta %s | Nivel %s\n",
                stDht.c_str(), stBmp.c_str(), stPluv.c_str(),
                stAnem.c_str(), stBir.c_str(), stNiv.c_str());

  // ---- 5) Google Sheets pelo 4G ----
  bool envioOk = false;
  if (redeOk) {
    envioOk = enviarSheets(String(data), String(hora), tAr, umid, tBmp, press,
                           chuvaDia, vento, graus, cardeal, nivel, nivelOk,
                           stDht, stBmp, stPluv, stAnem, stBir, stNiv);
    if (envioOk) {
      Serial.println("Enviado ao Google Sheets.");
    } else {
      Serial.println("Nao enviou (o dado continua salvo no cartao).");
      if (at("AT+IPADDR", 3000).indexOf("ERROR") >= 0) {
        Serial.println("Reabrindo a conexao de dados...");
        at("AT+NETOPEN", 20000);
      }
    }
  }

  // ---- 6) backup no MicroSD ----
  if (sdOk) {
    String linha = String(data) + "," + String(hora) + "," +
                   num(tAr, 1)   + "," + num(umid, 1)  + "," +
                   num(tBmp, 1)  + "," + num(press, 1) + "," +
                   num(chuvaDia, 2) + "," + num(vento, 2) + "," +
                   String(graus) + "," + cardeal + "," +
                   (nivelOk ? num(nivel, 3) : String("")) + "," +
                   stDht + "," + stBmp + "," + stPluv + "," +
                   stAnem + "," + stBir + "," + stNiv + "," +
                   (envioOk ? "OK" : "FALHOU");

    File f = SD.open(arquivoAtual, FILE_APPEND);
    if (f) { f.println(linha); f.close(); Serial.println("Salvo no cartao."); }
    else   { Serial.println("Erro ao gravar no cartao."); }
  }

  Serial.println("---");
}

/*
  ==========================================================================
  CABECALHO PARA COLAR NA LINHA 1 DA PLANILHA
  (esta separado por tabulacao: cole na celula A1 que ele espalha sozinho)
  ==========================================================================

Data	Hora	Temperatura do ar (C)	Umidade relativa (%)	Temperatura BMP280 (C)	Pressao atmosferica (hPa)	Precipitacao do dia (mm)	Velocidade do vento (m/s)	Direcao do vento (graus)	Direcao do vento	Nivel da agua (m)	Status DHT22	Status BMP280	Status pluviometro	Status anemometro	Status biruta	Status nivel

  ==========================================================================
*/
