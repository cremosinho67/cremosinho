#include <QTRSensors.h>

#define NUM_SENSORS 6
#define NUM_SAMPLES_PER_SENSOR 4
#define EMITTER_PIN 2

QTRSensors qtra;
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

const int VELOCIDADE = 150;   // Velocidade maxima (0-255): usada em linha reta e pelo motor "de fora" na curva
const int CENTRO = 2500;      // Posicao central esperada da linha (6 sensores => 0 a 5000)

// Fator de agressividade da curva (ajuste livre):
//  - proximo de 0.0  -> quase nao curva
//  - ate 0.5         -> motor interno desacelera de forma gradual ate 0 no erro maximo
float ganhoCurva = 0.5;

void setup()
{
  Serial.begin(9600);

  qtra.setTypeRC();
  qtra.setSensorPins((const uint8_t[]){A0, A1, A2, A3, A4, A5}, NUM_SENSORS);
  qtra.setSamplesPerSensor(NUM_SAMPLES_PER_SENSOR);
  qtra.setEmitterPin(EMITTER_PIN);

  pinMode(STBY, OUTPUT);
  pinMode(PWMA, OUTPUT);
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(PWMB, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.println("Calibrando sensores... Mexe o cremoso ai!");

  for (uint16_t i = 0; i < 400; i++) {
    qtra.calibrate();
    delay(20);
  }

  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("Solta o Cremoso!! :-[");

  digitalWrite(STBY, HIGH);

  delay(1000);
}

void loop()
{
  uint16_t position = qtra.readLineBlack(sensorValues);

  Serial.print(F("S0:"));
  Serial.print(sensorValues[0]);
  Serial.print(F(" S1:"));
  Serial.print(sensorValues[1]);
  Serial.print(F(" S2:"));
  Serial.print(sensorValues[2]);
  Serial.print(F(" S3:"));
  Serial.print(sensorValues[3]);
  Serial.print(F(" S4:"));
  Serial.print(sensorValues[4]);
  Serial.print(F(" S5:"));
  Serial.print(sensorValues[5]);

  int error = (int)position - CENTRO;

  int sensoresCentro =
      (sensorValues[1] +
       sensorValues[2] +
       sensorValues[3] +
       sensorValues[4]) / 4;

  int sensoPresExtremos =
      (sensorValues[0] +
       sensorValues[5]) / 2;

  Serial.print(F(" Posicao: "));
  Serial.print(position);
  Serial.print(F(" | Erro: "));
  Serial.println(error);

  if (sensoresCentro < 50 && sensoresExtremos < 500) {
    // Nenhum sensor esta enxergando a linha com intensidade (possivel falha de
    // leitura, cruzamento ou lacuna na linha): mantem o robo em frente.
    moverFrente();
  }
  else {
    // Controle proporcional continuo da curva, baseado apenas no erro atual.
    controlarCurva(error);
  }
}

void controlarCurva(int error) {
  // converte a escala que pode variar de -2500 a 2500 para -1 a 1, serve apenas para facilitar o 
  //trabalho, para não usarmos valores muito grandes
  float erroNormalizado = (float)error / (float)CENTRO;
  //constrain
  //prende um valor dentro de uma faixa: se valor for menor que o mínimo, 
  //ela devolve o mínimo; se for maior que o máximo, devolve o máximo; 
  //caso contrário, devolve o próprio valor sem alteração.

  erroNormalizado = constrain(erroNormalizado, -1.0, 1.0);

 // Calcula a correção da curva com base no erro.
 // Quanto maior o erro, maior será a correção.
 // A correção determina quanto um dos motores deve
 // diminuir sua velocidade para o robô fazer a curva.
 // Exemplo: correção = 50 → um motor continua em 150
 // e o outro passa para 100.
  int correcao = (int)(erroNormalizado * VELOCIDADE * 2.0 * ganhoCurva);

// Define a velocidade de cada motor de acordo com a correção.
// Se o erro for positivo, o Motor B(ESQQUERDO) diminui a velocidade
// e o Motor A continua na velocidade máxima.
// Se o erro for negativo, acontece o contrário.
// Assim, a diferença entre os motores faz o robô virar.
// se a correção for positiva diminui a velocidade do motor B(ESQUERDO)
// se for negativa diminui a velocidade do Motor A(DIREITO)
  int velocidadeA = VELOCIDADE + min(correcao, 0); //pega o menor valor
  int velocidadeB = VELOCIDADE - max(correcao, 0);//pega o maior valor

// Envia as velocidades calculadas para os motores.
  setMotorA(velocidadeA);
  setMotorB(velocidadeB);
}

// Controla o MOTOR DIREITO (Motor A).
// Recebe a velocidade que o motor deve ter.
// Valor positivo = motor gira para frente.
// Valor negativo = motor gira para trás.
// O valor da velocidade também define a força/rapidez do motor.
void setMotorA(int velocidadeA) {

  // Limita a velocidade entre -255 e 255.
  // O sinal indica a direção e o número indica a velocidade.
  velocidadeA = constrain(velocidadeA, -150, 150);

  // Define a direção do Motor A.
  if (velocidadeA >= 0) {

    // Motor A gira para frente.
    digitalWrite(AIN1, HIGH);
    digitalWrite(AIN2, LOW);

  } else {

    // Motor A gira para trás.
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, HIGH);
  }

  // Envia somente o valor da velocidade para o PWM.
  // abs() transforma um valor negativo em positivo,
  // pois o sentido já foi definido pelos AIN1 e AIN2.
  //então esse valor é para a potência que esse motor vai girar
  analogWrite(PWMA, abs(velocidadeA));
}


// Controla o MOTOR ESQUERDO (Motor B).
// Recebe a velocidade que o motor deve ter.
// Valor positivo = motor gira para frente.
// Valor negativo = motor gira para trás.
void setMotorB(int velocidadeB) {

  // Limita a velocidade entre -255 e 255.
  velocidadeB = constrain(velocidadeB, -150, 150);

  // Define a direção do Motor B (ESQUERDO)
  if (velocidadeB >= 0) {

    // Motor B(ESQUERDO) gira para frente
    digitalWrite(BIN1, HIGH);
    digitalWrite(BIN2, LOW);

  } else {

    // Motor B(ESQUERDO) gira para trás
    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, HIGH);
  }

  // Envia a velocidade para o PWM.
  analogWrite(PWMB, abs(velocidadeB));
}

void moverFrente() {
  digitalWrite(AIN1, HIGH);
  digitalWrite(AIN2, LOW);
  analogWrite(PWMA, VELOCIDADE);

  digitalWrite(BIN1, HIGH);
  digitalWrite(BIN2, LOW);
  analogWrite(PWMB, VELOCIDADE);
}

// void moverTras() {
//   digitalWrite(AIN1, LOW);
//   digitalWrite(AIN2, HIGH);
//   analogWrite(PWMA, VELOCIDADE);

//   digitalWrite(BIN1, LOW);
//   digitalWrite(BIN2, HIGH);
//   analogWrite(PWMB, VELOCIDADE);
// }

// // Mantidas por compatibilidade / uso manual (testes, menus, etc.).
// // Nao sao mais chamadas pelo loop() principal, que agora usa controlarCurva().
// void virarDireita() {
//   digitalWrite(AIN1, HIGH);
//   digitalWrite(AIN2, LOW);
//   analogWrite(PWMA, VELOCIDADE);

//   digitalWrite(BIN1, LOW);
//   digitalWrite(BIN2, HIGH);
//   analogWrite(PWMB, 90);
// }

// void virarEsquerda() {
//   digitalWrite(BIN1, HIGH);
//   digitalWrite(BIN2, LOW);
//   analogWrite(PWMB, VELOCIDADE);

//   digitalWrite(AIN1, LOW);
//   digitalWrite(AIN2, HIGH);
//   analogWrite(PWMA, 90);
// }

// void pararMotores() {
//   analogWrite(PWMA, 0);
//   analogWrite(PWMB, 0);
// }
