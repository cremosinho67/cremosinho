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

void setup() {
  pinMode(STBY, OUTPUT);
  pinMode(PWMA, OUTPUT);
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(PWMB, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);

  digitalWrite(STBY, HIGH); // tira a ponte H do standby
}

void loop() {
  moverFrente();
  delay(3000);

  pararMotores();
  delay(2000);

  moverTras();
  delay(3000);

  pararMotores();
  delay(2000);
}

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