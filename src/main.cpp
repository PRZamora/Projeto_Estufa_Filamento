//Para codar no PlatformIO com a "interface" do Arduino
#include <Arduino.h>

// Para módulo de cartão SD e trabalho com arquivos
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// Para atualizar o horário pela internet e atuar sob as strings
#include <time.h> 
#include <WiFi.h>
#include <sys/time.h>
#include <string.h>

// Para operar o LCD
#include <LiquidCrystal.h>

// Para ler o sensor interno de temperatura
#ifdef __cplusplus
  extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
  }
#endif
uint8_t temprature_sens_read();

// Para o RGB Digital
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

// Definições do LED RGB Utilizado:
#define LED_PIN    32  // Pino de controle
#define LED_COUNT  1   // Quantidade de LEDs em série
#define BRIGHTNESS 30  // Brilho do LED (caso dê vontade de ajustar)
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

// Pinagem do LCD [12, 13 são controle SPI, resto é de dados]
LiquidCrystal lcd(12, 13, 33, 25, 26, 27);

// Char Arrays utilizadas na formatação do relógio para o LCD
const int lcd_line_max = 16; //Tamanho da linha no LCD
char sup_line[lcd_line_max] = {0};
char sub_line[lcd_line_max] = {0};

// Pino de controle do elemento de resistência/aquecedor
#define BURN 22

// Conexão do termistor externo
const int pinTermistor = 34;

// Parâmetros do termistor
const double beta = 3950.0;
const double r0 = 80000.0; 
const double t0 = 273.15 + 25.0; //25 Celsius em Kelvin
const double rx = r0 * exp(-beta/t0);

// Parâmetros do circuito
const double vcc = 5.0;
const double R = 220000.0;

// Numero de amostras na leitura do termistor
const int nAmostras = 10;

// Credenciais de WiFi coletados pelo programa:
char inf_ssid[64]     ;
char inf_password[64] ;

/*
 * Pinagem associada ao SD Card:
 *
 * SD Card |   ESP32   | Pinos
 *    D2       -         -
 *    D3       SS        IO5
 *    CMD      MOSI      IO23
 *    VSS      GND       GND
 *    VDD      3.3V      3.3V
 *    CLK      SCK       IO18
 *    VSS      GND       GND
 *    D0       MISO      IO19
 *    D1       -         -
 */

//Botão coneectado ao pino digital 15 do ESP32
int pushButton = 15;

long timezone = -4;     // Timezone com relação ao servidor udp utilizado
byte daysavetime = 1;   // Definição de horário de Verão
struct tm tmstruct;     // Estrutura que recebe dado temporal
char time_text[100];    // 'String' com o horário para impressões/SD
char temp_text[10];     // 'String' para Temperatura para impressões/SD
char full_text[110];    // 'String' para plot completo

char credentials[127];  // Credenciais considerando o máximo de 63 digitos para uma senha de WiFi e um SSID aleatório 
char iterable[7];       // Variável para ler via stream os dados de um arquivo, caracter por caracter.

int conectadoWifi = 0;  // Variável que define operação: opera conforme há ou não há conexão para certos casos.
int stage = 0;          // Define estágio sendo executado
int temp_var = 0;       // Variável que recebe ajustes de temp

time_t targetTime; // Variável que armazena o número de segundos desde unix (para meta de tempo)
time_t actualTime; // Variável que armazena o número de segundos desde unix (para atual, permitindo comparação)
time_t initTime;   // Variável que armazena o número de segundos desde unix (para inicial do programa, permitindo avaliar tempo desde inicialização)

// Função para visualizar os diretórios do cartão.
void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.print (file.name());
            time_t t= file.getLastWrite();
            struct tm * tmstruct = localtime(&t);
            Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n",(tmstruct->tm_year)+1900,( tmstruct->tm_mon)+1, tmstruct->tm_mday,tmstruct->tm_hour , tmstruct->tm_min, tmstruct->tm_sec);
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.print(file.size());
            time_t t= file.getLastWrite();
            struct tm * tmstruct = localtime(&t);
            Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n",(tmstruct->tm_year)+1900,( tmstruct->tm_mon)+1, tmstruct->tm_mday,tmstruct->tm_hour , tmstruct->tm_min, tmstruct->tm_sec);
        }
        file = root.openNextFile();
    }
}

//Função para Verificar se existe determinado arquivo na raíz do cartão SD
int existsInRoot(fs::FS &fs, uint8_t levels, const char * filename){

    const char * dirname = "/"; //Definindo root
    Serial.printf("Acessando a root! \n\n");
    Serial.printf("Buscando: %s\n", filename);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("Falha no acesso à root!");
        return 0;
    }
    if(!root.isDirectory()){
        Serial.println("Diretório não aceito!");
        return 0;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.println("Diretório Atual: ");
            Serial.print(file.name());
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
              if(strcmp(file.name(), filename) == 0){
              Serial.println("\nArquivo Encontrado!");
              return 1;
            }
        }
        file = root.openNextFile();
    }
  return 0;  
}

//Função para escrever informações em arquivo no cartão SD
void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("Falha em abrir o arquivo");
        return;
    }
    if(file.print(message)){
        Serial.println("Procedimento de Escrita resultou em sucesso.");
    } else {
        Serial.println("Procedimento de Escrita falhou.");
    }
    file.close();
}

//Adiciona info em arquivos
void appendFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Appending to file: %s\n", path);

    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("Failed to open file for appending");
        return;
    }
    if(file.print(message)){
        Serial.println("Message appended");
    } else {
        Serial.println("Append failed");
    }
    file.close();
}

// Função para ler o arquivo com as credenciais e prepará-los para uso
int obtainWifiCredential(fs::FS &fs, const char * path){
    Serial.printf("Lendo arquivo: %s\n", path);

    File file = fs.open(path);
    if(!file){
        Serial.println("Falha na leitura do arquivo");
        return 0;
    }

    Serial.print("Leitura do Arquivo: ");
    while(file.available()){
        //Serial.write(file.read());
        
        sprintf(iterable, "%c", file.read());
        //Serial.println(iterable);
        strcat(credentials, iterable);
    }
    //Serial.print(credentials);
    Serial.printf("\nTamanho total das credenciais: %i", strlen(credentials));
    Serial.println();

    if(strlen(credentials) < 1){
      Serial.println("É possível que esteja vazio o arquivo com as configs do WiFi!");
      return 0;
    }
    else if (strcmp(credentials, "MinhaSSID\nMinhaSenha") == 0) {
      Serial.println("Provavelmente, o Arquivo foi gerado pelo dispositivo e não foi alterado");
      return 0;
    }
    else{
      Serial.println("Check básico ok, então coletemos as credenciais: ");
      char delimitador = '\n';
      int atingido = 0;

      for(int i = 0; i < strlen(credentials); i++){
        char coleta[10] ;
        sprintf(coleta, "%c", credentials[i]);
        if(credentials[i] == delimitador){
          Serial.println("\nDelimitador atingido!");
          atingido = 1;
        }
        else{
          if(atingido == 0){
            //Serial.print(credentials[i]);
            //Serial.print(coleta);
            strcat(inf_ssid, coleta);
            }
          else {
            //Serial.print(credentials[i]);
            //Serial.print(coleta);
            strcat(inf_password, coleta);
            }        
        } 
      }
    }
    file.close();
    inf_ssid[strlen(inf_ssid)-1] = '\0'; // Removendo garbage da quebra de linha
    Serial.printf("\nSSID: %s", inf_ssid);
    Serial.printf("\nTamanho:: %i\n", strlen(inf_ssid));
    Serial.printf("\nPswd: %s", inf_password);
    Serial.printf("\nTamanho: %i\n", strlen(inf_password));
    return 1;
}

// Função que registra o valor de temperatura atual no Termistor, segundo parâmetros informados anteriormente
double termistor(){

  int soma = 0;
  
  for (int i = 0; i < nAmostras; i++) {
    soma += analogRead(pinTermistor);
    //Serial.print(analogRead(pinTermistor));
    delay (20);
  }
 
  // Determina a resistência do termistor
  double v = (vcc*soma)/(nAmostras*4096.0);
  double rt = (vcc*R)/v - R;
 
  // Calcula a temperatura
  double t = beta / log(rt/rx);
  //Serial.println (t-273.15);
 
  // Dá um tempo entre leituras
  return (t-273.15);

}

// Inicialização do código. Executada antes de void loop, define informações necessárias ao procedimento, mas que precisam ser executadas apenas uma vez. 
void setup() {

  //Iniciando conexão serial com baud rate de 115200
  Serial.begin(115200);

  //Inicializando botão
  pinMode(pushButton, INPUT);
  pinMode(BURN, OUTPUT);

  //Inicializando RGB Digital
  #if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
    clock_prescale_set(clock_div_1);
  #endif
  strip.begin();                   // Initializa os LEDs
  strip.show();                    // Apaga qualquer LED aceso
  strip.setBrightness(BRIGHTNESS); // Ajusta o Brilho

  strip.setPixelColor(0, strip.Color(255,  255,  0)); //Deve permanecer nessa cor até o fim do Setup
  strip.show();

  // Inicializando LCD de 16x2:
  lcd.begin(16, 2);

  // Mensagem inicial.
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Inicializando...");
  delay(700); //Delay para que a mensagem seja apresentada. Puramente estético.

  // Tentativa de coletar SSID e pswd do SD Card
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Verificando SD ");
  delay(700); //Delay para que a mensagem seja apresentada. Puramente estético.

    // Montando cartão
    if(!SD.begin()){
        Serial.println("Card Mount Failed");
        lcd.setCursor(0, 1);
        lcd.print(" Falha: SD Card ");
        conectadoWifi = 0;
        return;
    }
    else{

      // Averiguando tipo e/ou se há mesmo cartão
      uint8_t cardType = SD.cardType();
      if(cardType == CARD_NONE){
          Serial.println("No SD card attached");
          lcd.setCursor(0, 1);
          lcd.print("SD Card Ausente ");
          return;
      }
      else {
          //Caso o cartão esteja presente, vamos coletar a SSID e pswd
          lcd.setCursor(0, 1);
          lcd.print("   SD Card Ok!  ");
          delay(700); //Delay para que a mensagem seja apresentada. Puramente estético.
          
          //É possível listar os diretórios a partir da raíz do SD:
          
          // Busquemos o diretório de interesse, verificando se existe:
          if(existsInRoot(SD, 0, "/keys.txt") == 1){
            Serial.println("Diretório Encontrado. Devemos coletar as credenciais...");

            // Precisamos obter essas credenciais, caso existam no arquivo:
            conectadoWifi = obtainWifiCredential(SD, "/keys.txt"); //Será operado com base nas credenciais disponíveis
          }
          else{
            Serial.println("Como não há o diretório, este será criado. Usuário, favor, preencher caso queira dados de hora corretos!");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Para conectar,  ");
            lcd.setCursor(0, 1);
            lcd.print("preencher SDCard");

            //Gerando arquivo
            writeFile(SD, "/keys.txt", "MinhaSSID\nMinhaSenha");
            conectadoWifi = 0; //Como não há credenciais, operará com datas que não condizem com a realidade

            delay(2000);
          }
      }

    }

    // Agora, obtidas as credenciais, temos que inicializar em 2 possíveis rotas: com e sem conexão. No caso de falhar a conexão WiFi inicial, devemos cair pra essa segunda rota também
    // Assim sendo, tentemos conectar ao WiFi primeiramente
    if(conectadoWifi == 1){
      Serial.println("\nComo temos credenciais, tentemos conexão: ");
      int retries = 100;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi:");
      lcd.setCursor(0, 1);
      lcd.print("Conectando...");

      WiFi.begin(inf_ssid, inf_password);
      for (int i = 0; i <= retries; i++){
        conectadoWifi = 0;
          if (WiFi.status() != WL_CONNECTED) {
            Serial.print(WiFi.status());
            delay(600);
          }
          else{
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("WiFi:");
            lcd.setCursor(0, 1);
            lcd.print("Conectado!");
            Serial.println("\nEndereço IP:"); 
            Serial.println(WiFi.localIP());
            conectadoWifi = 1;
            delay(700);
            break;
          }
      } 

    //Agora, podemos oficialmente partir para os caminhos separados! Vamos atualizar o horário:
      if(conectadoWifi == 1){
        int ano;
        tmstruct.tm_year = 0; 
        do {      
          configTime(3600*timezone, daysavetime*3600, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
          getLocalTime(&tmstruct, 5000);

          ano = (tmstruct.tm_year)+1900;
	        Serial.printf("\nHorário Atualizado: %d-%02d-%02d %02d:%02d:%02d\n",(tmstruct.tm_year)+1900,( tmstruct.tm_mon)+1, tmstruct.tm_mday,tmstruct.tm_hour , tmstruct.tm_min, tmstruct.tm_sec);
          if (ano > 2020){
            sprintf(time_text, "%d-%02d-%02d %02d:%02d", (tmstruct.tm_year)+1900, (tmstruct.tm_mon)+1, tmstruct.tm_mday,tmstruct.tm_hour, tmstruct.tm_min);  
          } 
        } while (ano < 2020);


        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Data Inicial:");
        lcd.setCursor(0, 1);
        lcd.print(time_text);

      }
      else{
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" Horarios");
        lcd.setCursor(0, 1);
        lcd.print(" Desatualizados ");     
      }
    }
  
  // Devemos, por fim, verificar se existe o arquivo onde serão registrados os dados de temperatura e umidade
  // Caso não exista, devemos criá-lo.
  if(existsInRoot(SD, 0, "/date_log.txt") == 0){
    Serial.println("\nDiretório não encontrado. Devemos criá-lo:");
    writeFile(SD, "/date_log.txt", "Time;ESPTemperature;Temperature;TargetTemp;Umidity;Mode\n");
  }


  Serial.println("\n\n[ALERTA] Entrando no código principal!\n\n");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("   Iniciando   ");
  lcd.setCursor(0, 1);
  lcd.print("     Estufa     ");
  getLocalTime(&tmstruct, 5000);
  initTime = mktime(&tmstruct); // Definindo momento inicial de operação da estufa
  strip.setPixelColor(0, strip.Color(255,  0,255)); //Sinaliza o fim do Setup
  strip.show();
  delay(1000);
}

// Essa função permite navegar entre as funções criadas pelo desenvolvedor para o dispositivo
int escolha() {

  int count = 0;
  int waitTime = 15;
  int changed = 1;

    // Tela scrollará a cada clique, e o clique mantido em início + 15s será o selecionado:
  
  do{
    delay(10);
    count += 10;
    int buttonState = digitalRead(pushButton);

    if((buttonState == 0) & (stage < 5)){
      stage++;
      delay(200); //debounce
      count += 200;
      changed = 1;
    }
    else if ((buttonState == 0) & (stage == 5)){
      stage = 0;
      delay(200); //debounce
      count += 200;
      changed = 1;
    }

    if (changed == 1){
      lcd.clear();
      switch (stage) {
        case 0:
          lcd.setCursor(0, 0);
          lcd.print(" Permanecer em  ");
          lcd.setCursor(0, 1);
          lcd.print("     Espera     ");
          changed = 0;
          break;
        case 1:
          lcd.setCursor(0, 0);
          lcd.print("  Dehumidificar ");
          lcd.setCursor(0, 1);
          lcd.print(" Material: PLA  ");
          changed = 0;
          break;
        case 2:
          lcd.setCursor(0, 0);
          lcd.print("  Dehumidificar ");
          lcd.setCursor(0, 1);
          lcd.print(" Material: ABS  ");
          changed = 0;
          break;
        case 3:
          lcd.setCursor(0, 0);
          lcd.print("  Dehumidificar ");
          lcd.setCursor(0, 1);
          lcd.print(" Material: Nylon");
          changed = 0;
          break;
        case 4:
          lcd.setCursor(0, 0);
          lcd.print("     Ajustar    ");
          lcd.setCursor(0, 1);
          lcd.print("  Temperatura   ");
          changed = 0;
          break;
        case 5:
          lcd.setCursor(0, 0);
          lcd.print("    Utilizar    ");
          lcd.setCursor(0, 1);
          lcd.print("  como relogio  ");
          changed = 0;
          break;  
        default:
          lcd.setCursor(0, 0);
          lcd.print("  Developer.... ");
          lcd.setCursor(0, 1);
          lcd.print("Temos algum prob");
          changed = 0;
          break; //Bem, sempre bom deixar claro quando encontramos problemas
      }
    }
  } while(count < waitTime*1000);

  return stage;

}

// Função para permitir criar um valor de delta na temperatura pré-estabelecida, de até 40oC. Limitado para impedir derretimento de plásticos por mal uso.
int ajusteTemp(int tempvar) {

  int count = 0;
  int timeWait = 20;
  int add_or_sub = 0;
  int max = 40; //Segurança em primeiro lugar
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  Desvio Atual  ");
  lcd.setCursor(7, 1);
  lcd.print(tempvar);
  delay(1000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Azul: resfrio  ");
  lcd.setCursor(0, 1);
  lcd.print(" Rosa: aqueco   ");
  delay(1000);

  do {
    delay(100);
    count += 100;
    int buttonState = digitalRead(pushButton);
    // Tempo para adição (mais quente, aquecer):
    if (((count <= 5000) || ((count > 9999) & (count <= 15000))) & (buttonState == 0)){
      strip.setPixelColor(0, strip.Color(255,  0,  255));
      strip.show();
      add_or_sub = 1;
      break;
    }
    else if(((count <= 5000) || ((count > 9999) & (count <= 15000)))){
      strip.setPixelColor(0, strip.Color(255,  0,  255));
      strip.show();
    }
    // Tempo para subtracao (menos quente, resfriar):
    if (((count > 15000) || ((count > 5000) & (count < 10000))) & (buttonState == 0)){
      strip.setPixelColor(0, strip.Color(0,  255,  255));
      strip.show();
      add_or_sub = -1;
      break; 
    }
    else if (((count > 15000) || ((count > 5000) & (count < 10000)))){
      strip.setPixelColor(0, strip.Color(0,  255,  255));
      strip.show();
    }
  } while (count < timeWait*1000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Clique p alterar");
  lcd.setCursor(0, 1);
  lcd.printf("    %i     ", tempvar);

  int timeLimit = 10;
  count = 0;

  do {
    delay(100);
    count += 100;
    int buttonState = digitalRead(pushButton);
    if ((buttonState == 0) & (tempvar < max)) {
      tempvar = tempvar + add_or_sub;
      count = 0; //Nesse caso, estará considerando o tempo inativo
      delay(250);
      lcd.setCursor(0, 1);
      lcd.printf("    %i     ", tempvar);
    }
  }
  while (timeLimit*1000 > count);

  return tempvar;
}

void actOnBurn(int temp_target){

  double realTemp = termistor();

  if (realTemp < temp_target){
    digitalWrite(BURN, HIGH);
  }
  else {
    digitalWrite(BURN, LOW);
  }
}

//Escreve a operação no cartão SD
void writeCSV(float realTemp, int targetTemp, float humidade){

  // "Time;ESPTemperature;Temperature;TargetTemp;Umidity;Mode\n"
  getLocalTime(&tmstruct, 5000);
  char timeC[30];
  strftime(timeC, 30, "%F %T", &tmstruct);

  double ESPTemp = (temprature_sens_read() - 32) / 1.8;

  sprintf(full_text, "%s;%.2f;%.2f;%d;%.2f;%d\n", timeC, ESPTemp, realTemp, targetTemp, humidade, stage);

  Serial.print(full_text);
  appendFile(SD, "/date_log.txt", full_text);
}

//Pré-build do sensor de humidade. Não implementado por ausência do componente em mãos.
double humidade(){
  
  return 50.0;
}

// Principal função do projeto, deve se preocupar em estabilizar a temperatura no valor esperado;
void aquecer(int temp_base, int tempo_m, int temp_vari){

  int targetTemp = temp_base + temp_vari;
  time_t temp_seg = tempo_m * 60;

  double realTemp = termistor();
  double humid = humidade();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Aquecendo...  ");
  lcd.setCursor(0, 1);
  lcd.printf(" %.1fC / %dC ", realTemp, targetTemp);

  strip.setPixelColor(0, strip.Color(255,  0,  0));
  strip.show();

  delay(200); //debounce
  int buttonState = 1;

  getLocalTime(&tmstruct, 5000);

  time_t targetTime = mktime(&tmstruct);
  Serial.println(targetTime);
  targetTime = targetTime + temp_seg;
  Serial.println(targetTime);

  while ((actualTime < targetTime) & (buttonState == 1)){
    
    //Definindo momento atual:
    getLocalTime(&tmstruct, 5000);
    actualTime = mktime(&tmstruct);
    
    //Definindo e apresentando tempo restante:
    time_t difTime = targetTime - actualTime;
    Serial.println(difTime);
    struct tm *ptm = gmtime(&difTime);
    strftime(sub_line, lcd_line_max, "Rest:  %T ", ptm);
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print(sub_line); 

    for (int i = 0; i <= (1000/(nAmostras*20 + 1)); i++){
      buttonState = digitalRead(pushButton);
      actOnBurn(targetTemp);
      delay(1);
      if (buttonState == 0) {
        break;
      }
    } //Versão mais simples de código para não impedir a saída desse modo de operação

    realTemp = termistor();
    humid = humidade();
    lcd.setCursor(0, 1);
    lcd.printf("%.1fC / %dC |%.0f%% ", realTemp, targetTemp, humid);

    writeCSV(realTemp, targetTemp, humid);

    for (int i = 0; i <=  (1000/(nAmostras*20 + 1)); i++){
      buttonState = digitalRead(pushButton);
      actOnBurn(targetTemp);
      delay(1);
      if (buttonState == 0) {
        break;
      }
    } //Versão mais simples de código para não impedir a saída desse modo de operação

  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("   Finalizado   ");
  lcd.setCursor(0, 1);
  lcd.print("      UwU       ");

  digitalWrite(BURN, LOW);
  delay(2000);

}

// Função de Standby, porém demonstrando um relógio na tela. Feita para ser um extra, mesmo.
void clockMode(){
  delay(200); //debounce

  // Cor do modo: 'verde claro' 
  strip.setPixelColor(0, strip.Color( 115, 153,   0));
  strip.show();

  lcd.clear();
  int buttonState = digitalRead(pushButton);

  while(buttonState == 1){

    getLocalTime(&tmstruct, 5000);
    strftime(sup_line, lcd_line_max, "   %F   ", &tmstruct);
    strftime(sub_line, lcd_line_max, "   %A   ", &tmstruct);  
    
    lcd.setCursor(0, 0);
    lcd.print(sup_line);
    lcd.setCursor(0, 1);
    lcd.print(sub_line);

    for (int i = 0; i <= 2000; i++){
      buttonState = digitalRead(pushButton);
      delay(1);
      if (buttonState == 0) {
        break;
      }
    } //Versão mais simples de código para não impedir a saída desse modo de operação

    for (int j = 0; j < 5; j++){

      getLocalTime(&tmstruct, 5000);
      strftime(sub_line, lcd_line_max, "    %T     ", &tmstruct); 

      lcd.setCursor(0, 1);
      lcd.print(sub_line);

      for (int i = 0; i <= 1000; i++){
        buttonState = digitalRead(pushButton);
        delay(1);
        if (buttonState == 0) {
          break;
        }
      } //Versão mais simples de código para não impedir a saída desse modo de operação
    }
  }
  delay(1000);

}

// Função principal. Força a navegação por entre os modos de operação disponíveis, tal que permite o funcionamento e transição entre eles.
void loop() {

  int buttonState = digitalRead(pushButton);

  /*
    Stage 0: Tela Inicial          - 0C    - Indeterminado
    Stage 1: Aquecer PLA           - 45C   - 5h
    Stage 2: Aquecer ABS           - 80C   - 6h
    Stage 3: Aquecer Nylon         - 80C   - 5:30h
    Stage 4: Ajuste de Temperatura - Indf  - Indeterminado
    Stage 5: Web-Clock             - 0C    - Indeterminado
  */

 switch (stage) {

  case 0:
      Serial.println("Caso 0: Inicialização/Standby");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("  Press Start   ");
      lcd.setCursor(0, 1);
      lcd.print("      ^~^       ");
      strip.setPixelColor(0, strip.Color(  0,255,  0)); //Cor verde, indicando prontidão
      strip.show();

      do{
        buttonState = digitalRead(pushButton);
        delay(10);
      } while (buttonState == 1); //Nível 0 indica pressionado

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("    Escolha    ");
      lcd.setCursor(0, 1);
      lcd.print("    O modo!    ");
      delay(1000);

      stage = escolha();
      Serial.printf("Modo selecionado %d", stage);

    break;

  case 1:
      Serial.println("Caso 1: Aquecimento PLA");
      aquecer(45, 300, temp_var);
      stage = 0; // Ao finalizar, retornar para o estágio 0
    break;

  case 2:
      Serial.println("Caso 2: Aquecimento ABS");
      aquecer(80, 360, temp_var);
      stage = 0; // Ao finalizar, retornar para o estágio 0
    break;

  case 3:
      Serial.println("Caso 3: Aquecimento Nylon");
      aquecer(80, 330, temp_var);
      stage = 0; // Ao finalizar, retornar para o estágio 0
    break;

  case 4:
      Serial.println("Caso 4: Ajuste de Temperatura");
      temp_var = ajusteTemp(temp_var);
      Serial.println("Nova Temperatura de Ajuste:");
      Serial.print(temp_var);
      stage = 0; // Ao finalizar, retornar para o estágio 0
    break;

  case 5:
      Serial.println("Caso 5: Relógio Digital");
      clockMode();
      stage = 0; // Ao finalizar, retornar para o estágio 0
    break;  

  default:
      Serial.println("Caso X: Algo de errado não está certo!");
      stage = 0; // Ao finalizar, retornar para o estágio 0
    break;
}

  getLocalTime(&tmstruct, 5000);
  strip.setPixelColor(0, strip.Color(  0,  0,255)); //Pisca Azul quando alterna de modo
  strip.show(); 
  delay(100);

}