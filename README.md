# Caixa Bluetooth com Visualizador de Música

Projeto acadêmico: um **ESP32** que funciona como caixa de som Bluetooth e mostra o áudio em um **equalizador de 8 barras** numa matriz de LEDs WS2812B 8x8. O ESP32 toca o áudio do celular em um alto falante, separa o som em graves, médios e agudos e acende os leds em tempo real. Quanto mais forte o grave, mais alta a barra da esquerda (vermelha); quanto mais forte o agudo, mais alta a barra da direita (verde). É o mesmo efeito dos equalizadores de aparelhos de som antigos.

## Como funciona

1. O celular se conecta ao ESP32 via **Bluetooth A2DP** (o dispositivo aparece como `Minha Caixa`).
2. O áudio recebido é enviado ao amplificador **MAX98357A** por **I2S** e toca no alto-falante.
3. Em paralelo, as amostras de áudio (estéreo → mono) são guardadas em um buffer circular.
4. A cada 20 ms (~50 vezes por segundo), é feita uma **FFT de 512 amostras** (44,1 kHz, janela Hamming), e o espectro é dividido em **8 faixas de frequência**.
5. Cada faixa vira a altura de uma barra, com ganho por faixa, ganho automático, corte de ruído, suavização e histerese (para o topo não piscar).
6. Cada coluna da matriz é uma faixa, com degradê horizontal: **vermelho (grave) → verde (agudo)**.
7. O **LED azul da placa** (GPIO 2) acende quando o celular está conectado.

## Componentes

- ESP32 (clássico)
- Matriz de LEDs WS2812B 8x8
- Amplificador I2S MAX98357A
- Alto-falante de 3 W / 4 Ω
- Protoboards e jumpers
- Resistor para proteção dos LEDS
- Recomendado: capacitor eletrolítico

## Ligações

| Função | Pino do ESP32 |
|---|---|
| I2S BCLK | GPIO 26 |
| I2S LRC | GPIO 25 |
| I2S DIN | GPIO 22 |
| Dados da matriz de LEDs | GPIO 5 |
| LED de status Bluetooth | GPIO 2 (LED da placa) |

## Bibliotecas e ambiente

- Arduino IDE com o pacote **ESP32 da Espressif versão 3.x** (versões mais novas deram erro de A2DP/AVRCP)
- [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP) (Phil Schatzmann), para o Bluetooth A2DP
- `ESP_I2S`, para a saída de áudio (já vem no pacote ESP32)
- [arduinoFFT](https://github.com/kosme/arduinoFFT), para a análise de frequências
- [FastLED](https://github.com/FastLED/FastLED), para controlar a matriz

## Configurações principais

| Parâmetro | Valor |
|---|---|
| Amostras da FFT | 512 |
| Taxa de amostragem | 44100 Hz |
| Bandas | 8 |
| Atualização | 20 ms |
| Brilho da matriz | 30 |
| Limite de energia dos LEDs | 5 V / 400 mA |

## Como usar

1. Instale o pacote ESP32 (3.x) e as bibliotecas acima na Arduino IDE.
2. Monte o circuito conforme a tabela de ligações.
3. Abra o código, compile e envie para o ESP32.
4. No celular, conecte em **Minha Caixa** e toque uma música.

## Limitações conhecidas

- **Reinício em volume alto:** com o volume muito alto, o ESP32 pode desligar/reiniciar por falta de corrente na alimentação USB. A solução é usar uma fonte externa de 5 V com corrente suficiente.
- **Chiado:** há um leve chiado no áudio, considerado aceitável para o MVP do projeto. Recomenda-se um capacitor eletrolítico (por exemplo, 470 µF a 1000 µF, tensão de 10 V ou mais) entre 5 V e GND, perto do amplificador, para estabilizar a alimentação e reduzir ruído.
- **Tom puro:** duas colunas vizinhas podem acender juntas, devido à resolução da FFT (~86 Hz por faixa).
