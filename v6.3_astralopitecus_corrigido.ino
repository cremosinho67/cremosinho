/*
 * Robo Segue-Linha - v6.3: baseado nas suas alteracoes sobre o v6.2.
 *
 * MUDANCAS DESTA REVISAO (ver discussao na conversa):
 *   1) A saida de CURVA_ACENTUADA agora exige 2 leituras "limpas"
 *      seguidas, nao mais 1 - mesma logica de anti-flicker que ja
 *      usamos para ENTRAR no desafio, so que aplicada tambem pra SAIR.
 *   2) A direcao do giro agora usa o mesmo sinal que disparou a
 *      deteccao (diffExtremos), em vez de "erro" - antes podiam
 *      discordar em situacoes de curvas proximas/ruido.
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

// Quanto o robo anda para frente, em ms, ao detectar a curva e ANTES de girar.
// Objetivo: o pivo (eixo das rodas) chegar em cima do canto da curva.
// Ajustar na pista: curto demais -> gira antes do canto; longo demais -> passa do canto.
const unsigned long TEMPO_AVANCO_CURVA_MS = 250;  // <- ponto de partida, ajuste nos testes

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
//
// AVISO IMPORTANTE (pista de investigacao para o bug das curvas
// proximas): tanto o loop() normal quanto o while(true) de dentro de
// tratarCurvaAcentuada()/tratarGap() escrevem NESTE MESMO historico.
// Isso significa que, ao sair de uma curva, as ultimas gavetas ainda
// contem leituras tiradas DURANTE o giro anterior, nao leituras de
// "linha reta normal". Se as curvas estiverem muito proximas (~10cm),
// o historicoAnterior(1) usado pela curva seguinte pode, na pratica,
// ser uma leitura do giro anterior. Se o bug de hoje persistir mesmo
// depois dos dois ajustes abaixo, esse compartilhamento de historico
// entre loop principal e tratadores e o proximo suspeito.
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
const int16_t LIMIAR_CENTRO_FRACO = 100;  // abaixo disso, o centro nao ve linha
const int16_t LIMIAR_DIFF_FRACO   = 600;  // abaixo disso, extremos nao indicam curva
const int16_t LIMIAR_DIFF_CURVA   = 700;  // acima disso, um extremo esta bem mais escuro que o outro

const unsigned long TIMEOUT_GAP_MS   = 500; // seguranca: nao ficar "as cegas" indefinidamente
const unsigned long TIMEOUT_CURVA_MS = 2500; // seguranca: nao girar pra sempre se a linha sumir de vez //Original: 900
const int VELOCIDADE_GIRO = 100; // ORIGINAL: 150

// Quantas leituras "limpas" seguidas precisamos ver antes de considerar
// que a curva realmente terminou. Mesma ideia do anti-flicker usado na
// deteccao, so que aplicada tambem na saida - ver discussao do bug das
// 3 curvas proximas.
const uint8_t CICLOS_PARA_SAIR_DA_CURVA = 2;


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
    // FIX: a direcao agora usa o MESMO sinal que disparou a deteccao
    // (diffExtremos), em vez de "erro". Antes usavamos atual.erro, que
    // e calculado com um metodo diferente (media ponderada dos 6
    // sensores) e podia, em situacoes de ruido/curvas proximas,
    // discordar do sinal de diffExtremos que efetivamente identificou
    // a curva - fazendo o robo girar para o lado errado.
    tratarCurvaAcentuada(atual.diffExtremos >= 0 ? 1 : -1);
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
// nenhum identificador de ENTRADA usa ele ainda (so entra na checagem
// de SAIDA da curva, la embaixo). Fica pronto pra quando voces forem
// atacar interseccoes ou quiserem perceber a curva mais cedo.


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

  // FASE 1: anda reto por tempo fixo ate o pivo ficar sobre o canto.
  unsigned long t0 = millis();
  while (millis() - t0 < TEMPO_AVANCO_CURVA_MS) {
    LeituraAgrupada l = lerEAgrupar();
    registrarHistorico(l);
    imprimirTelemetria(l);
    moverFrente();
  }

  // FASE 2: gira no eixo ate o centro (S2/S3) reencontrar a linha nova.
  unsigned long inicio = millis();
  bool centroPerdeuLinha = false;  // protecao: so arma a saida depois que o centro saiu da linha antiga
  uint8_t ciclosLimpos = 0;

  while (millis() - inicio < TIMEOUT_CURVA_MS) {
    LeituraAgrupada l = lerEAgrupar();
    registrarHistorico(l);
    imprimirTelemetria(l);

    bool centroVeLinha = l.centroMedia >= LIMIAR_CENTRO_FRACO;

    if (!centroPerdeuLinha) {
      if (!centroVeLinha) centroPerdeuLinha = true;
    } else {
      ciclosLimpos = centroVeLinha ? (ciclosLimpos + 1) : 0;
      if (ciclosLimpos >= CICLOS_PARA_SAIR_DA_CURVA) break;
    }

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

// Valor a partir do qual o sensor e considerado "vendo preto" (0 a 1000 apos calibracao).
const uint8_t LIMIAR_SENSOR_PRETO = 500;

// A telemetria so e impressa a cada N ms
const unsigned long TELEMETRIA_INTERVALO_MS = 75;

void imprimirTelemetria(const LeituraAgrupada &l) {
  static unsigned long ultimo = 0;
  if (millis() - ultimo < TELEMETRIA_INTERVALO_MS) return;
  ultimo = millis();

  // Linha 1: sensores individuais (■ = preto, □ = claro)
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    Serial.print(F("S"));
    Serial.print(i);
    Serial.print(F(": "));
    Serial.print(sensorValues[i] > LIMIAR_SENSOR_PRETO ? F("■") : F("□"));
    Serial.print(F("   "));
  }
  Serial.println();

  // Linha 2: leituras tratadas
  Serial.print(F("Erro: "));
  Serial.print(l.erro);
  Serial.print(F(", Centro: "));
  Serial.print(l.centroMedia);
  Serial.print(F(", DiffMedios: "));
  Serial.print(l.diffMedios);
  Serial.print(F(", DiffExtremos: "));
  Serial.println(l.diffExtremos);
}
