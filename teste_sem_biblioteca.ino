// TESTE SEM A BIBLIOTECA QTRSensors -- só analogRead() puro do Arduino.
// Objetivo: se o "dominio" entre sensores ainda aparecer aqui, a causa
// NÃO pode ser um bug de lógica na biblioteca QTRSensors, porque ela
// nem está sendo usada neste sketch.

void setup()
{
  Serial.begin(9600);
  Serial.println(F("=== TESTE SEM BIBLIOTECA (analogRead puro) ==="));
  Serial.println(F("Nao ha calibracao, nao ha QTRSensors.h envolvido."));
  Serial.println(F("Valores brutos do ADC: 0 = branco (muita luz refletida)"));
  Serial.println(F("                        1023 = preto (pouca luz refletida)"));
  delay(1000);
}

void loop()
{
  // Leitura direta, uma por uma, sem passar por nenhuma biblioteca externa.
  int s0 = analogRead(A0);
  int s1 = analogRead(A1);
  int s2 = analogRead(A2);
  int s3 = analogRead(A3);
  int s4 = analogRead(A4);
  int s5 = analogRead(A5);

  Serial.print(F("A0:"));
  Serial.print(s0);
  Serial.print(F("  A1:"));
  Serial.print(s1);
  Serial.print(F("  A2:"));
  Serial.print(s2);
  Serial.print(F("  A3:"));
  Serial.print(s3);
  Serial.print(F("  A4:"));
  Serial.print(s4);
  Serial.print(F("  A5:"));
  Serial.println(s5);

  delay(200);
}
