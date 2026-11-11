# Estação Meteorológica Automática — Firmware

Firmware da estação meteorológica automática desenvolvida no campus de Cachoeira do Sul
da Universidade Federal de Santa Maria (UFSM), dentro do programa RS Talentos.

A placa lê seis sensores uma vez por minuto, grava tudo em um cartão microSD e envia a
leitura por rede celular 4G para uma planilha do Google. A gravação no cartão é
independente do envio: se a rede cair, o dado não é perdido.

- **Autor:** Erik Luiz Dullius Carneiro — bolsista de hardware
- **Coordenação:** Prof. Paulo Cesar
- **Linguagem:** C++ (Arduino)
- **Revisão:** 6

---

## Hardware

| Item | Modelo |
|---|---|
| Placa | LilyGO T-A7670G (ESP32-WROVER-E + modem SIMCom A7670G 4G) |
| Temperatura e umidade do ar | DHT22 |
| Pressão atmosférica | BMP280 (I2C) |
| Pluviômetro | PB10 — reed switch, 0,25 mm por pulso |
| Anemômetro | SVDV10 — reed switch, raio de 147 mm |
| Biruta (direção do vento) | potenciômetro em divisor de tensão |
| Nível da água | sensor submersível 4–20 mA, faixa de 5 m |
| Armazenamento | cartão microSD (slot da própria placa) |
| Alimentação | painel 20 Wp + controlador PWM + bateria chumbo-ácido 12 V |

## Ligação dos pinos

| Função | GPIO | Observação |
|---|---|---|
| Alimentação dos periféricos | 12 | `POWERON_PIN` — precisa ir para HIGH antes de tudo |
| DHT22 (dados) | 32 | |
| BMP280 SDA / SCL | 21 / 22 | I2C, endereço 0x76 ou 0x77 |
| Pluviômetro | 18 | `INPUT_PULLUP` interno |
| Anemômetro | 19 | `INPUT_PULLUP` interno |
| Biruta | 34 | **pino só de entrada, sem pull-up interno** — precisa de resistor externo de 4,7 kΩ |
| Nível da água | 39 | **pino só de entrada** — resistor shunt de 150 Ω para converter 4–20 mA em tensão |
| microSD SCLK / MISO / MOSI / CS | 14 / 2 / 15 / 13 | |
| Modem PWRKEY / RESET | 4 / 5 | |
| Modem RX / TX | 27 / 26 | |

> Os GPIO 34 a 39 do ESP32 são apenas de entrada e **não possuem pull-up nem
> pull-down internos**. É por isso que a biruta e o sensor de nível levam resistor
> externo na placa, e os dois sensores de pulso não levam.

## Divisor de tensão da biruta

```
3V3 ── 4,7 kΩ ──┬── GPIO 34
                │
             potenciômetro da biruta
                │
               GND
```

## Resistor shunt do sensor de nível

O sensor é um laço de corrente 4–20 mA com zero vivo: 4 mA significa reservatório
vazio e 20 mA significa cheio. Como 0 mA não é um valor válido, uma leitura abaixo
de 3,5 mA indica cabo rompido ou sensor desligado, e não nível zero.

Sobre o resistor de 150 Ω:

- 4 mA × 150 Ω = 0,6 V → vazio
- 20 mA × 150 Ω = 3,0 V → cheio (5 m)

## Bibliotecas necessárias

Instalar pelo Gerenciador de Bibliotecas da Arduino IDE:

- `DHT sensor library` (Adafruit)
- `Adafruit BMP280 Library`
- `Adafruit Unified Sensor`
- `Adafruit BusIO`

As bibliotecas `WiFi`, `SD`, `SPI` e `Wire` já vêm com o core do ESP32.

**Placa a selecionar na IDE:** ESP32 Wrover Module (ou ESP32 Dev Module),
Partition Scheme `Huge APP` se faltar espaço.

## O que configurar antes de gravar

No início do arquivo:

```cpp
#define APN_NOME   "zap.vivo.com.br"   // APN da operadora do chip
#define APN_USER   "vivo"
#define APN_SENHA  "vivo"

const char* WEB_APP_URL = "https://script.google.com/macros/s/.../exec";

const unsigned long INTERVALO_MS = 60000UL;   // intervalo entre leituras
```

O `WEB_APP_URL` é o endereço do Apps Script publicado como aplicativo da web.
Ele sai do outro repositório/pasta deste projeto.

## O que é gravado

O cartão microSD recebe um arquivo `estacao.csv` com 18 colunas:

```
Data, Hora, Temperatura do ar (C), Umidade relativa (%),
Temperatura BMP280 (C), Pressao atmosferica (hPa), Precipitacao do dia (mm),
Velocidade do vento (m/s), Direcao do vento (graus), Direcao do vento,
Nivel da agua (m), Status DHT22, Status BMP280, Status pluviometro,
Status anemometro, Status biruta, Status nivel, Envio
```

A planilha recebe as 17 primeiras — a coluna `Envio` só existe no cartão, porque
registra se aquela linha conseguiu ou não ser enviada.

As colunas de status assumem `OK`, `ERRO`, `TRAVADO` ou `SEM PULSO`. O
`TRAVADO` dos sensores de pulso significa que o pino ficou preso em nível baixo,
o que indica chave colada ou curto no cabo; `SEM PULSO` significa que o pino está
saudável mas não houve pulso dentro da janela esperada (3 horas para o vento,
7 dias para a chuva).

### Arquivos no cartão

- `estacao.csv` — os dados. Se o cabeçalho do arquivo existente não bater com o
  do firmware, o código cria `estacao1.csv`, `estacao2.csv` e assim por diante,
  em vez de misturar formatos diferentes no mesmo arquivo.
- `chuva.txt` — guarda o dia do mês e a chuva acumulada, no formato `22,0.00`.
  Serve para a estação não perder o acumulado do dia se reiniciar. A precipitação
  zera na virada do dia.

## Relógio

O horário vem do modem, que pega a hora da rede celular. A sincronização é
refeita a cada 6 horas para corrigir a deriva do relógio interno do ESP32, que
chegou a passar de uma hora de erro em testes anteriores.

## Alimentação durante os testes

A placa precisa de uma fonte capaz de fornecer picos de corrente durante o
registro na rede celular. Alimentada só pela USB do notebook, ela reinicia no
meio do registro. Use uma fonte de **5 V / 2 A**.

## Limitação conhecida

A tabela de direção do vento (`DIRECOES[]`) ainda está com os valores originais,
sem calibração. A faixa de ADC correspondente a Oeste é estreita demais e essa
direção nunca aparece nos registros, enquanto Leste aparece em excesso. A
calibração em campo está pendente — é preciso girar a biruta nas oito posições e
anotar o valor de ADC lido em cada uma.

## Licença

Projeto acadêmico. Uso livre para fins de ensino e pesquisa.
