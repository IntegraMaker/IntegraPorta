
#include <SPI.h>
#include <MFRC522.h>


//Led on
const int PIN_LED_ON = 3;
const int PIN_OPEN_BUTTON = 2;
const int PIN_SPDT = 9;
int buttonStatus = 0;

//RC522 Controller
constexpr uint8_t RST_PIN = 5;
constexpr uint8_t SS_PIN = 10;

MFRC522 mrf (SS_PIN, RST_PIN);

//RFS

char *rfsid [] = {"FAD09680","3365E912", "FAE4747F"};
int rfsize = 3;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  while(!Serial);

  //RF522
  SPI.begin();
  mrf.PCD_Init();
  mrf.PCD_DumpVersionToSerial();

  //Port configuration
  pinMode(PIN_LED_ON, OUTPUT);
  pinMode(PIN_SPDT, OUTPUT);
  pinMode(PIN_OPEN_BUTTON, INPUT);
  //led off
  digitalWrite(PIN_LED_ON, HIGH);
  //SPDT off
  digitalWrite(PIN_SPDT, HIGH);
}

void openDoor(){

  //Open
  digitalWrite(PIN_SPDT, LOW);

  //Signal from open process
  for (int i = 0; i<=10; i++)
  {
    digitalWrite(PIN_LED_ON, HIGH);
    delay(100);
    digitalWrite(PIN_LED_ON, LOW);
    delay(100);
  }

  //Finalize
  digitalWrite(PIN_SPDT, HIGH);
  digitalWrite(PIN_LED_ON, HIGH);
}


void loop() {
  // put your main code here, to run repeatedly:
  buttonStatus = digitalRead(PIN_OPEN_BUTTON);
  //Serial.println(buttonStatus);
  if(buttonStatus == HIGH){
    Serial.println("Abrir");
    openDoor();
    
  }

  //Open by RF
    if (!mrf.PICC_IsNewCardPresent() || !mrf.PICC_ReadCardSerial()) //VERIFICA SE O CARTÃO PRESENTE NO LEITOR É DIFERENTE DO ÚLTIMO CARTÃO LIDO. CASO NÃO SEJA, FAZ
    return; //RETORNA PARA LER NOVAMENTE
 

  String rfcode = "";
  for (byte i = 0; i < 4; i++) {
    rfcode +=
    (mrf.uid.uidByte[i] < 0x10 ? "0" : "") +
    String(mrf.uid.uidByte[i], HEX);
  }

  rfcode.toUpperCase();

  Serial.print("Identificador (UID) da tag: "); //IMPRIME O TEXTO NA SERIAL
  Serial.println(rfcode); //IMPRIME NA SERIAL O UID DA TAG RFID
  mrf.PICC_HaltA(); //PARADA DA LEITURA DO CARTÃO
  mrf.PCD_StopCrypto1(); //PARADA DA CRIPTOGRAFIA NO PCD

  for (int i = 0; i < rfsize; i++) {
    if ( rfcode == rfsid[i]){
      Serial.println(rfcode);
      openDoor();
    }
  }
}
