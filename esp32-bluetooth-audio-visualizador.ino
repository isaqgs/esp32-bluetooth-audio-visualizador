#include "ESP_I2S.h"
#include "BluetoothA2DPSink.h"
#include <arduinoFFT.h>
#include <FastLED.h>

#define SAMPLES       512
#define SAMPLING_FREQ 44100
#define NUM_BANDS     8
#define TAM_RING      4096
#define MASCARA_RING  (TAM_RING - 1)
#define INTERVALO_MS  20             // ~50 análises/atualizações por segundo
#define DEBUG_SERIAL  0              // 1 = imprime as barras no Serial
#define DEGRADE_HORIZONTAL 1         // 1 = vermelho(grave)->verde(agudo) na horizontal; 0 = degradê vertical antigo

// ---------- Pinos ----------
#define LED_PIN   5
#define LED_BT 2                     // LED azul da placa (indica Bluetooth conectado)
const uint8_t I2S_BCLK = 26;
const uint8_t I2S_LRC  = 25;
const uint8_t I2S_DIN  = 22;

// ---------- Matriz (igual ao código de teste) ----------
#define WIDTH      8
#define HEIGHT     8
#define NUM_LEDS   (WIDTH * HEIGHT)
#define SERPENTINE false
#define FLIP_X     false
#define FLIP_Y     false
#define ALTURA_MAX HEIGHT

CRGB leds[NUM_LEDS];

uint16_t XY(int x, int y) {
  if (FLIP_X) x = WIDTH - 1 - x;
  if (FLIP_Y) y = HEIGHT - 1 - y;
  if (SERPENTINE && (y % 2 == 1)) return y * WIDTH + (WIDTH - 1 - x);
  return y * WIDTH + x;
}

void setPixel(int x, int y, CRGB cor) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
  leds[XY(x, y)] = cor;
}

void drawBar(int x, int h) {
  for (int i = 0; i < h; i++) {
#if DEGRADE_HORIZONTAL
    uint8_t matiz = x * 96 / (WIDTH - 1);   // coluna 0 = vermelho (0), coluna 7 = verde (96)
#else
    uint8_t matiz = 96 - i * 13;            // verde embaixo -> vermelho no topo
#endif
    setPixel(x, HEIGHT - 1 - i, CHSV(matiz, 255, 255));
  }
}

// NOVO: desenha as 8 barras e atualiza a matriz
void desenharBarras();

// ---------- Áudio / Bluetooth ----------
I2SClass i2s;
BluetoothA2DPSink a2dp_sink(i2s);
bool conectadoAntes = false;

// ---------- FFT ----------
float vReal[SAMPLES];
float vImag[SAMPLES];
ArduinoFFT<float> FFT = ArduinoFFT<float>(vReal, vImag, SAMPLES, SAMPLING_FREQ);

const int limites[NUM_BANDS + 1] = {1, 3, 5, 8, 13, 22, 38, 64, 128};
float bandas[NUM_BANDS];

// ---------- Captura de amostras ----------
int16_t ring[TAM_RING];
volatile uint32_t escritas = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

void capturarAudio(const uint8_t *dados, uint32_t tamanho) {
  const int16_t *pcm = (const int16_t *)dados;
  uint32_t pares = tamanho / 4;

  portENTER_CRITICAL(&mux);
  uint32_t w = escritas;
  for (uint32_t i = 0; i < pares; i++) {
    int32_t mono = ((int32_t)pcm[2 * i] + pcm[2 * i + 1]) / 2;
    ring[w & MASCARA_RING] = (int16_t)mono;
    w++;
  }
  escritas = w;
  portEXIT_CRITICAL(&mux);
}

int16_t quadro[SAMPLES];

bool lerQuadro(int16_t *destino) {
  bool ok = false;
  portENTER_CRITICAL(&mux);
  if (escritas >= SAMPLES) {
    uint32_t inicio = escritas - SAMPLES;
    for (int i = 0; i < SAMPLES; i++) {
      destino[i] = ring[(inicio + i) & MASCARA_RING];
    }
    ok = true;
  }
  portEXIT_CRITICAL(&mux);
  return ok;
}

bool carregarAmostras() {
  if (!lerQuadro(quadro)) return false;
  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = quadro[i] / 32768.0f;
    vImag[i] = 0;
  }
  return true;
}

void calcularBandas() {
  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();

  for (int b = 0; b < NUM_BANDS; b++) {
    float maximo = 0;
    for (int k = limites[b]; k < limites[b + 1]; k++) {
      if (vReal[k] > maximo) maximo = vReal[k];
    }
    bandas[b] = maximo;
  }
}

// ---------- Bandas -> alturas (Etapa 7, sem alterações) ----------
const float ganho[NUM_BANDS] = {1.0, 1.0, 1.4, 1.6, 2.1, 2.5, 3.0, 3.8};

const float PISO_RUIDO     = 0.05;
const float REF_MINIMA     = 1.0;
const float DECAIMENTO_REF = 0.995;
const float QUEDA          = 0.25;
const float CORTE_REL      = 0.05;   // ignora bandas < 5% da referência
const float HISTERESE      = 0.3;    // folga antes de mudar a altura da barra

float refNivel = REF_MINIMA;
float suave[NUM_BANDS];
int   alturas[NUM_BANDS];

void calcularAlturas() {
  float pesadas[NUM_BANDS];
  float maxAtual = 0;

  for (int b = 0; b < NUM_BANDS; b++) {
    pesadas[b] = bandas[b] * ganho[b];
    if (pesadas[b] > maxAtual) maxAtual = pesadas[b];
  }

  refNivel *= DECAIMENTO_REF;
  if (maxAtual > refNivel) refNivel = maxAtual;
  if (refNivel < REF_MINIMA) refNivel = REF_MINIMA;

  for (int b = 0; b < NUM_BANDS; b++) {
    float nivel = 0;
    float rel = pesadas[b] / refNivel;
    if (pesadas[b] > PISO_RUIDO && rel > CORTE_REL) {
      nivel = sqrtf((rel - CORTE_REL) / (1.0f - CORTE_REL));
      if (nivel > 1) nivel = 1;
    }
    float alvo = nivel * ALTURA_MAX;

    if (alvo > suave[b]) {
      suave[b] = alvo;
    } else {
      suave[b] -= QUEDA;
      if (suave[b] < alvo) suave[b] = alvo;
    }
    float s = suave[b];
    int h = alturas[b];              // altura do quadro anterior
    if (s > h + 0.5f + HISTERESE || s < h - 0.5f - HISTERESE) {
      h = (int)(s + 0.5f);
    }
    alturas[b] = h;
  }
}

// ---------- NOVO: alturas -> LEDs ----------
void desenharBarras() {
  FastLED.clear();
  for (int b = 0; b < NUM_BANDS; b++) {
    drawBar(b, alturas[b]);          // 1 coluna por banda
  }
  FastLED.show();
}

// ---------- Controle de tempo ----------
unsigned long ultimaAnalise = 0;
unsigned long ultimaImpressao = 0;
uint32_t escritasVisto = 0;
unsigned long gasto = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando");
  pinMode(LED_BT, OUTPUT);
  digitalWrite(LED_BT, LOW);        

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(30);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 400);
  FastLED.clear();
  FastLED.show();

  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
  if (!i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("ERRO: o I2S não iniciou");
    while (true) delay(1000);
  }

  a2dp_sink.set_stream_reader(capturarAudio, true);
  a2dp_sink.start("Minha Caixa");
  Serial.println("Bluetooth ligado. Procure por 'Minha Caixa' no celular.");
}

void loop() {
  bool conectado = a2dp_sink.is_connected();
  if (conectado != conectadoAntes) {
    conectadoAntes = conectado;
    digitalWrite(LED_BT, conectado ? HIGH : LOW);
    Serial.println(conectado ? "Celular conectado!" : "Desconectado.");
  }

  if (millis() - ultimaAnalise >= INTERVALO_MS) {
    ultimaAnalise = millis();

    uint32_t agora = escritas;
    if (agora != escritasVisto && carregarAmostras()) {
      escritasVisto = agora;
      unsigned long inicio = micros();
      calcularBandas();
      gasto = micros() - inicio;
    } else {
      for (int b = 0; b < NUM_BANDS; b++) bandas[b] = 0;
    }
    calcularAlturas();
    desenharBarras();                // NOVO
  }

#if DEBUG_SERIAL
  if (millis() - ultimaImpressao >= 100) {
    ultimaImpressao = millis();
    for (int b = 0; b < NUM_BANDS; b++) {
      for (int r = 0; r < ALTURA_MAX; r++) {
        Serial.print(r < alturas[b] ? '#' : '.');
      }
      Serial.print(' ');
    }
    Serial.printf("| ref: %.1f | %lu us\n", refNivel, gasto);
  }
#endif

  delay(1);
}