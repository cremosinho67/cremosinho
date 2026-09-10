/*
 * Robo Segue-Linha - v6.2: versao enxuta, baseada direto no v6.1.
 *
 * FLUXO (igual ao que voce desenhou):
 *   [SETUP]    calibracao (igual ao v6.1)
 *   [LOOP]     leitura -> agrupamento -> registro no historico ->
 *              identificar desafio -> tratar desafio
 *
 * DIFERENCA PARA A VERSAO ANTERIOR (mais complexa): aqui NAO existe uma
 * maquina de estados persistente entre chamadas de loop(). Gap e curva
 * acentuada sao tratados dentro de um while(true) local, que so devolve
 * o controle pro loop() principal quando o desafio termina (ou estoura
 * o tempo limite de seguranca). Fora de um desafio, cada volta do
 * loop() e so um ciclo normal de controlarCurva().
 */

#include <QTRSensors.h>

// ============================================================
//  HARDWARE (igual ao v6.1)
// ============================================================
#define NUM_SENSORS 6
#define NUM_SAMPLES_PER_SENSOR 2
#define EMITTER_PIN 2

QTRSensors qtra;
uint16_t sensorValues[NUM_SENSORS];

#define STBY 9

const int PWMA = 13; // Motor A - Direito
const int AIN1 = 11;
const int AIN2 = 12;

const int PWMB = 5;   // Motor B - Esquerdo
const int BIN1 = 8;
const int BIN2 = 7;

const int VELOCIDADE = 150;
const int CENTRO = 2500;

float perdaMotorFronteiroCurva = 2.0;
float ganhoMotorOpostoCurva = 1; // ORIGINAL: 1

// ============================================================
//  HISTORICO (leve: so os valores AGRUPADOS, nao os 6 sensores crus)
// ============================================================

// Uma leitura ja "resumida". Guardamos so o que os detectores precisam,
// em vez dos 6 sensores + classificacoes -> bem mais barato em RAM.
struct LeituraAgrupada {
  int16_t erro;          // posicao - CENTRO
  int16_t centroMedia;   // media de S2 e S3 (sensores centrais)
  int16_t diffMedios;    // S4 - S1 (positivo = mais preto do lado direito)
  int16_t diffExtremos;  // S5 - S0 (positivo = mais preto do lado direito)
};

// Discussao rapida sobre "array de valores vs array de objetos":
// com um historico tao curto (poucas leituras) e uma struct tao pequena
// (4 x int16_t = 8 bytes), a diferenca de desempenho/memoria entre as
// duas abordagens e irrelevante no AVR (nao ha cache pra se preocupar).
// Um array de structs (como abaixo) ganha em legibilidade: cada posicao
// do historico e "uma leitura completa", nao 4 arrays soltos que voce
// precisa manter sincronizados na mao. Por isso optei por isso aqui.
// Pensa nisto como uma caixa com 5 gavetas numeradas 0,1,2,3,4.
// Cada gaveta guarda 1 leitura (uma "LeituraAgrupada" inteira).
#define TAMANHO_HISTORICO 5
LeituraAgrupada historico[TAMANHO_HISTORICO];

// indiceHistorico = o "dedo" apontando pra PROXIMA gaveta vazia a usar.
// Comeca em 0. Depois de cada leitura guardada, o dedo anda 1 casa.
uint8_t indiceHistorico = 0;

// totalRegistrado = quantas gavetas ja tem alguma coisa dentro (para de
// contar em TAMANHO_HISTORICO, quando a caixa inteira ja foi preenchida
// pelo menos uma vez). So serve pra sabermos se ja da pra confiar no
// que tem guardado, ou se o robo acabou de ligar e a caixa ainda esta
// pela metade / vazia.
uint8_t totalRegistrado = 0;

// Guarda uma leitura nova na gaveta apontada pelo dedo.
void registrarHistorico(const LeituraAgrupada &l) {
  // 1) Guarda a leitura na gaveta atual.
  historico[indiceHistorico] = l;

  // 2) Anda o dedo 1 casa pra frente.
  //    O "% TAMANHO_HISTORICO" e o truque pra fazer o dedo voltar pra 0
  //    quando ele passar da ultima gaveta (numero 4) - tipo um rodizio
  //    de sushi: quando a esteira acaba, ela reaparece do outro lado.
  //    Sem esse truque, o dedo ia tentar apontar pra "gaveta 5", que
  //    nao existe (a caixa so tem 0 a 4).
  indiceHistorico = (indiceHistorico + 1) % TAMANHO_HISTORICO;

  // 3) So aumenta a contagem enquanto a caixa ainda nao encheu de vez.
  //    Depois que encher, cada leitura nova SUBSTITUI a mais velha,
  //    entao o total de gavetas ocupadas nao muda mais (fica em 5).
  if (totalRegistrado < TAMANHO_HISTORICO) totalRegistrado++;
}

// Pergunta: "o que estava guardado ha 'n' leituras atras?"
//   n = 0 -> a leitura mais recente que ja foi guardada
//   n = 1 -> a leitura guardada IMEDIATAMENTE ANTES dessa
//   n = 2 -> duas leituras atras, e assim por diante
const LeituraAgrupada& historicoAnterior(uint8_t n) {
  // O dedo (indiceHistorico) aponta pra PROXIMA gaveta vazia, entao a
  // ULTIMA gaveta preenchida e sempre "um passo atras do dedo":
  // indiceHistorico - 1. Pra achar "n leituras atras", andamos mais
  // "n" casas pra tras a partir dali.
  int16_t idx = (int16_t)indiceHistorico - 1 - (int16_t)n;

  // Se andarmos pra tras demais, o numero fica negativo (ex.: -1,-2).
  // Isso significa que "estouramos" o inicio da caixa - entao damos a
  // volta e continuamos contando a partir da ULTIMA gaveta (numero 4).
  // E o mesmo rodizio de sushi do registrarHistorico(), so que andando
  // pro lado contrario.
  while (idx < 0) idx += TAMANHO_HISTORICO;

  return historico[idx];
}

// ============================================================
//  LIMIARES (pontos de partida - ajuste na pista real)
// ============================================================
const int16_t LIMIAR_CENTRO_FRACO = 100;   // abaixo disso, o centro nao ve linha (mesmo valor do v6.1)
const int16_t LIMIAR_DIFF_FRACO   = 600;  // abaixo disso, extremos nao indicam curva (mesmo valor do v6.1)
const int16_t LIMIAR_DIFF_CURVA   = 700;  // acima disso, um extremo esta bem mais escuro que o outro

const unsigned long TIMEOUT_GAP_MS   = 500; // seguranca: nao ficar "as cegas" indefinidamente
const unsigned long TIMEOUT_CURVA_MS = 900; // seguranca: nao girar pra sempre se a linha sumir de vez
const int VELOCIDADE_GIRO = 100; // ORIGINAL: 150


void setup() {
  Serial.begin(9600);

  qtra.setTypeAnalog();
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

  Serial.println(F("Calibrando sensores... Mexe o cremoso ai!"));
  for (uint16_t i = 0; i < 200; i++) {
    qtra.calibrate();
    delay(20);
  }

  digitalWrite(LED_BUILTIN, LOW);
  Serial.println(F("Solta o Cremoso!! :-]"));

  digitalWrite(STBY, HIGH);
  delay(1000);
}

void loop() {
  // >> LEITURA + AGRUPAMENTO
  LeituraAgrupada atual = lerEAgrupar();

  // >> REGISTRO
  registrarHistorico(atual);

  // >> IDENTIFICAR + TRATAR DESAFIO
  // So chamamos identificarX() quando ja existe pelo menos 1 leitura
  // anterior no historico (elas comparam atual com anterior por dentro).
  if (totalRegistrado >= 2 && identificarGap(atual)) {
    tratarGap();
  } else if (totalRegistrado >= 2 && identificarCurvaAcentuada(atual)) {
    tratarCurvaAcentuada(atual.erro >= 0 ? 1 : -1);
  } else {
    controlarCurva(atual.erro); // nenhum desafio: segue linha normalmente
  }

  imprimirTelemetria(atual);
}


// ============================================================
//  LEITURA + AGRUPAMENTO
// ============================================================

LeituraAgrupada lerEAgrupar() {
  uint16_t position = qtra.readLineBlack(sensorValues);

  LeituraAgrupada l;
  l.erro         = (int16_t)position - CENTRO;
  l.centroMedia  = ((int16_t)sensorValues[2] + (int16_t)sensorValues[3]) / 2;
  l.diffMedios   = (int16_t)sensorValues[4] - (int16_t)sensorValues[1];
  l.diffExtremos = (int16_t)sensorValues[5] - (int16_t)sensorValues[0];

  return l;
}


// ============================================================
//  IDENTIFICACAO DE DESAFIOS
//  (chamadas todo ciclo -> so comparacoes inteiras, nada de float/loop
//  aqui dentro. O "anti-flicker" e o fato de exigir que o padrao já
//  estivesse presente na leitura anterior tambem, nao so na atual.)
// ============================================================

bool identificarGap(const LeituraAgrupada &atual) {
  const LeituraAgrupada &anterior = historicoAnterior(1);

  bool semLinhaAgora    = (atual.centroMedia    < LIMIAR_CENTRO_FRACO) &&
                          (abs(atual.diffExtremos)    < LIMIAR_DIFF_FRACO);
  bool semLinhaAnterior = (anterior.centroMedia < LIMIAR_CENTRO_FRACO) &&
                          (abs(anterior.diffExtremos) < LIMIAR_DIFF_FRACO);

  return semLinhaAgora && semLinhaAnterior;
}

bool identificarCurvaAcentuada(const LeituraAgrupada &atual) {
  const LeituraAgrupada &anterior = historicoAnterior(1);

  bool curvaAgora    = abs(atual.diffExtremos)    > LIMIAR_DIFF_CURVA;
  bool curvaAnterior = abs(anterior.diffExtremos) > LIMIAR_DIFF_CURVA;
  bool mesmoLado     = (atual.diffExtremos >= 0) == (anterior.diffExtremos >= 0);

  return curvaAgora && curvaAnterior && mesmoLado;
}

// diffMedios (S1/S4) esta sendo calculado e guardado no historico mas
// nenhum identificador usa ele ainda - deixei pronto pra quando voces
// forem atacar interseccoes ou precisarem de um sinal mais "fino" que os
// extremos (ex.: comecar a perceber a curva mais cedo). So usar
// historicoAnterior(n).diffMedios dentro de um novo identificarX().


// ============================================================
//  TRATAMENTO DE DESAFIOS (loop bloqueante ate resolver ou estourar o timeout)
// ============================================================

void tratarGap() {
  Serial.println(F(">> entrou em GAP"));
  unsigned long inicio = millis();

  while (true) {
    LeituraAgrupada l = lerEAgrupar();
    registrarHistorico(l);
    imprimirTelemetria(l);

    bool linhaReapareceu = (l.centroMedia >= LIMIAR_CENTRO_FRACO) ||
                           (abs(l.diffExtremos) >= LIMIAR_DIFF_FRACO);
    if (linhaReapareceu) break;
    if (millis() - inicio > TIMEOUT_GAP_MS) break; // rede de seguranca

    // TODO: trocar por zigue-zague se seguir reto nao for suficiente na pratica
    moverFrente();
  }

  Serial.println(F(">> saiu de GAP"));
}

void tratarCurvaAcentuada(int direcao) {
  Serial.println(F(">> entrou em CURVA_ACENTUADA"));
  unsigned long inicio = millis();

  while (true) {
    LeituraAgrupada l = lerEAgrupar();
    registrarHistorico(l);
    imprimirTelemetria(l);

    // Sai quando os extremos "limpam" (deixam de ver preto) e o centro
    // volta a enxergar a linha.
    bool extremosMediosLimpos = (abs(l.diffExtremos) < LIMIAR_DIFF_CURVA) &&
                          (l.centroMedia >= LIMIAR_CENTRO_FRACO) &&
                          (abs(l.diffMedios) < LIMIAR_DIFF_CURVA);
    
    if (extremosMediosLimpos) break;
    if (millis() - inicio > TIMEOUT_CURVA_MS) break; // rede de seguranca

    girarNoEixo(direcao, VELOCIDADE_GIRO);
  }

  Serial.println(F(">> saiu de CURVA_ACENTUADA"));
}


// ============================================================
//  CONTROLE DE MOTORES (igual ao v6.1 + giro no proprio eixo)
// ============================================================

void controlarCurva(int error) {
  float erroNormalizado = (float)error / (float)CENTRO;
  erroNormalizado = constrain(erroNormalizado, -1.0, 1.0);

  int correcaoPerda = (int)(erroNormalizado * VELOCIDADE * perdaMotorFronteiroCurva);
  int correcaoGanho = (int)(erroNormalizado * VELOCIDADE * ganhoMotorOpostoCurva);

  int velocidadeA = VELOCIDADE + min(correcaoPerda, 0) + max(correcaoGanho, 0);
  int velocidadeB = VELOCIDADE - max(correcaoPerda, 0) - min(correcaoGanho, 0);

  setMotorA(velocidadeA);
  setMotorB(velocidadeB);
}

// direcao > 0 -> pivo para a direita (mesma convencao do erro: erro>0 = linha a direita)
// direcao < 0 -> pivo para a esquerda
// Sentido deduzido do comportamento extremo de controlarCurva(), mas NUNCA
// testado fisicamente - confirme na bancada antes de confiar na pista, e
// inverta os sinais abaixo se o giro sair para o lado errado.
void girarNoEixo(int direcao, int velocidade) {
  if (direcao >= 0) {
    setMotorA(velocidade);
    setMotorB(-velocidade);
  } else {
    setMotorA(-velocidade);
    setMotorB(velocidade);
  }
}

void setMotorA(int velocidadeA) {
  velocidadeA = constrain(velocidadeA, -255, 255);
  digitalWrite(AIN1, velocidadeA >= 0 ? HIGH : LOW);
  digitalWrite(AIN2, velocidadeA >= 0 ? LOW  : HIGH);
  analogWrite(PWMA, abs(velocidadeA));
}

void setMotorB(int velocidadeB) {
  velocidadeB = constrain(velocidadeB, -255, 255);
  digitalWrite(BIN1, velocidadeB >= 0 ? HIGH : LOW);
  digitalWrite(BIN2, velocidadeB >= 0 ? LOW  : HIGH);
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


// ============================================================
//  TELEMETRIA (Serial)
// ============================================================

void imprimirTelemetria(const LeituraAgrupada &l) {
  Serial.print(F("Erro:"));
  Serial.print(l.erro);
  Serial.print(F(" Centro:"));
  Serial.print(l.centroMedia);
  Serial.print(F(" DiffMedios:"));
  Serial.print(l.diffMedios);
  Serial.print(F(" DiffExtremos:"));
  Serial.println(l.diffExtremos);
}
