#include <QTRSensors.h>

// Quantos sensores há
#define NUM_SENSORS            4
// Quantas leituras os sensores irão fazer para fazer uma média e retornar um valor
#define NUM_SAMPLES_PER_SENSOR  4
// Pino digital que controla os LED'S 
#define EMITTER_PIN             2

QTRSensors qtra;
// Serve pra passar todos os valores do sensor de uma vez
uint16_t sensorValues[NUM_SENSORS];

#define STBY 9

// Motor A - Direito
const int PWMA = 13;
const int AIN1 = 11;
const int AIN2 = 12;

// Motor B - Esquerdo
const int PWMB = 5;
const int BIN1 = 8;
const int BIN2 = 7;

const int VELOCIDADE = 150; // 0 a 255

int mediasensoresesquerda = 0;
int mediasensoresdireita = 0;

void setup()
{
  Serial.begin(9600);

  // Define o tipo dos sensores como analógicos
  qtra.setTypeAnalog();
  // Inicializa os sensores, definindo sua posição no array e quantidade
  qtra.setSensorPins((const uint8_t[]){ A1, A2, A3, A4}, NUM_SENSORS);
  // Inicializa a quantidade de leituras
  qtra.setSamplesPerSensor(NUM_SAMPLES_PER_SENSOR);
  //
  qtra.setEmitterPin(EMITTER_PIN);

  // Coloca os pinos como SAÍDA/ENTRADA (Neste caso, somente entrada)
  pinMode(STBY, OUTPUT);
  pinMode(PWMA, OUTPUT);
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(PWMB, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);

  digitalWrite(STBY, HIGH); // Tira a ponte H do standby

  delay(1000);
}

void loop()
{
  // Pega o valor dos sensores (Utilizando uma funçaõ da própria biblioteca)
  qtra.read(sensorValues);

  // Teste de sensores, para descobrir quais estão funcionando e sua posição no array e Hardware
  Serial.print(F("S0:"));
  Serial.print(sensorValues[0]); // Terceiro sensor (da esqu. para dire. olhando o lado inferior)
  
  Serial.print(F("  S1:"));
  Serial.print(sensorValues[1]); // Sexto sensor
  
  Serial.print(F("  S2:"));
  Serial.print(sensorValues[2]); // Quinto sensor
  
  Serial.print(F("  S3:"));
  Serial.print(sensorValues[3]); // Quarto sensor
  
  // Serial.print(F("  S4:"));
  // Serial.print(sensorValues[4]);
  // Serial.print(F("  S5:"));
  // Serial.println(sensorValues[5]);

  mediasensoresesquerda = (sensorValues[0] + sensorValues[3])/2;
  mediasensoresdireita = (sensorValues[1] + sensorValues[2])/2;

  // Este IF serve para testarmos o ALBEDO dos sensores. 
  // Verificamos se a luz que chega nos sensores é alta (menor valor) ou baixa (Maior valor).
  // Os sensores retornam um valor entre 0 a 1023, quanto mais luz ele reflete, menor será o valor retornado.
  // Há 90% de certeza nas afirmações acima.
   if (mediasensoresesquerda < 200) {
    Serial.print(F("Media (if) esquerda"));
    Serial.print(mediasensoresesquerda);
    if(mediasensoresdireita < 200){

      Serial.print(F("Media (if) direita"));
      Serial.print(mediasensoresdireita);
      moverFrente();

    }else{

      Serial.print(F("Media (else) direita"));
      Serial.print(mediasensoresdireita);
      moverTras();

    }
    
  }else {

    Serial.print(F("Media (else) esquerda"));
    Serial.print(mediasensoresesquerda);

  }

  delay(200);
}

  // AIN1      	AIN2   	O que acontece
  // HIGH	      LOW	    Gira num sentido (Frente)
  // LOW	      HIGH	  Gira no sentido oposto (Trás)
  // LOW	      LOW	    Motor "solto" (roda livre, sem força nenhuma — para por inércia/atrito)
  // Seria interessante talvez utilizar o LOW LOW e/ou o HIGH HIGH em momentos de curva?
  // HIGH	      HIGH	  Freio curto ("short brake")

void moverFrente() {
  // Motor Direito (A) para frente
  digitalWrite(AIN1, HIGH);
  digitalWrite(AIN2, LOW);
  analogWrite(PWMA, VELOCIDADE);

  // Motor Esquerdo (B) para frente
  digitalWrite(BIN1, HIGH);
  digitalWrite(BIN2, LOW);
  analogWrite(PWMB, VELOCIDADE);
}

void moverTras() {
  // Motor Direito (A) para trás
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, HIGH);
  analogWrite(PWMA, VELOCIDADE);

  // Motor Esquerdo (B) para trás
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, HIGH);
  analogWrite(PWMB, VELOCIDADE);
}

void pararMotores() {
  analogWrite(PWMA, 0);
  analogWrite(PWMB, 0);
}