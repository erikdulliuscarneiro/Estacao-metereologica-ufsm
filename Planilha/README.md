# Estação Meteorológica Automática — Recebimento na Planilha

Script em Google Apps Script que recebe as leituras da estação meteorológica
automática por HTTP e grava cada uma como uma linha em uma planilha do Google
Sheets.

É a outra metade do projeto: o firmware da placa monta a URL com os valores
medidos e chama este script, que escreve a linha na planilha.

- **Autor:** Erik Luiz Dullius Carneiro — bolsista de hardware
- **Coordenação:** Prof. Paulo Cesar
- **Linguagem:** JavaScript (Google Apps Script)
- **Instituição:** UFSM — Campus Cachoeira do Sul — programa RS Talentos

---

## Como funciona

A placa faz uma requisição GET parecida com esta:

```
https://script.google.com/macros/s/SEU_ID/exec?data=11/11/2026&hora=14:32:00&tempdht=24.6&...
```

O script lê cada parâmetro, converte os numéricos para número, grava a linha e
responde `OK`.

## Instalação

1. Crie uma planilha nova no Google Sheets.
2. Vá em **Extensões > Apps Script**.
3. Apague o conteúdo do editor e cole o `apps_script.gs` no lugar.
4. Salve.
5. **Implantar > Nova implantação**.
6. Em tipo, escolha **Aplicativo da Web**.
7. Em *Executar como*, deixe **Eu**.
8. Em *Quem pode acessar*, escolha **Qualquer pessoa**. Isso é obrigatório — a
   placa não faz login no Google.
9. Autorize o acesso quando o Google pedir (aparece um aviso de app não
   verificado; é o seu próprio script, siga em **Avançado > Ir para...**).
10. Copie o endereço gerado e cole no firmware, na constante `WEB_APP_URL`.

## Como atualizar depois

Para alterar o código sem trocar o endereço:

> **Implantar > Gerenciar implantações > ícone de lápis > Versão: Nova versão > Implantar**

**Não** use *Nova implantação* para atualizar. Ela gera um endereço diferente, e
a placa continua mandando dados para o endereço antigo.

## Parâmetros recebidos

| Parâmetro | Conteúdo | Tipo |
|---|---|---|
| `data` | data da leitura, `dd/MM/yyyy` | texto |
| `hora` | hora da leitura, `HH:mm:ss` | texto |
| `tempdht` | temperatura do ar, °C | número |
| `umidade` | umidade relativa, % | número |
| `tempbmp` | temperatura do BMP280, °C | número |
| `pressao` | pressão atmosférica, hPa | número |
| `chuva` | precipitação acumulada do dia, mm | número |
| `vento` | velocidade do vento, m/s | número |
| `graus` | direção do vento em graus | número |
| `direcao` | direção do vento por extenso (N, NE, L...) | texto |
| `nivel` | nível da água, m | número |
| `stdht` | status do DHT22 | texto |
| `stbmp` | status do BMP280 | texto |
| `stpluv` | status do pluviômetro | texto |
| `stanem` | status do anemômetro | texto |
| `stbir` | status da biruta | texto |
| `stniv` | status do sensor de nível | texto |

A data e a hora vêm da placa, não do servidor. Se chegarem vazias, o script usa
a hora do servidor no fuso `America/Sao_Paulo`.

## Colunas da planilha

```
A  Data
B  Hora
C  Temperatura do ar (C)
D  Umidade relativa (%)
E  Temperatura BMP280 (C)
F  Pressao atmosferica (hPa)
G  Precipitacao do dia (mm)
H  Velocidade do vento (m/s)
I  Direcao do vento (graus)
J  Direcao do vento
K  Nivel da agua (m)
L  Status DHT22
M  Status BMP280
N  Status pluviometro
O  Status anemometro
P  Status biruta
Q  Status nivel
```

O cabeçalho é criado automaticamente na primeira execução, se a planilha estiver
vazia.

## Dois detalhes que custaram caro

### 1. A função `n()` — por que número e não texto

Tudo que chega pela URL chega como texto. Se o script gravasse o texto direto,
a planilha em português brasileiro interpretaria `"21.6"` como **21 de junho**,
porque em pt-BR o separador decimal é a vírgula e o ponto vira separador de data.
A função `n()` converte o texto em número de verdade antes de gravar:

```javascript
function n(v) {
  if (v === undefined || v === null || v === "") return "";
  var x = parseFloat(v);
  return isNaN(x) ? "" : x;
}
```

### 2. `setNumberFormats` — por que carimbar o formato

Mesmo gravando número correto, o Google herda o formato da célula de cima. Um
valor correto de pressão como `1013.7` dentro de uma célula formatada como data
aparece na tela como `09/10/1902`, porque 1013,7 é o número de série daquela
data na contagem do Sheets. O número guardado está certo; só a aparência estava
errada. Por isso o script carimba o formato de cada coluna na linha recém-gravada.

Para consertar linhas antigas que já ficaram assim: selecione as colunas **C até
K** e vá em **Formatar > Número > Número**.

## Observação sobre esta versão

Esta é a versão **sem travas**, que é a que está em uso. Consequências:

- Abrir o endereço do script no navegador grava uma linha em branco na planilha.
- Se a mesma leitura chegar duas vezes, entram duas linhas.

A proteção contra linha repetida foi resolvida no firmware, não aqui: a causa das
leituras triplicadas era a placa seguir o redirecionamento HTTP 302 que o Apps
Script devolve, e seguir esse redirecionamento faz o script rodar de novo. O
firmware hoje trata o 302 como sucesso e não vai atrás do destino.

## Licença

Projeto acadêmico. Uso livre para fins de ensino e pesquisa.
