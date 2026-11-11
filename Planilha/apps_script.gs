/*
  ==========================================================================
  APPS SCRIPT DA ESTACAO METEOROLOGICA  -  versao sem travas
  --------------------------------------------------------------------------
  Igual a anterior, mas sem os dois blocos de protecao:

   . nao barra mais pedido vazio  -> abrir o endereco no navegador grava
                                     uma linha em branco na planilha
   . nao descarta mais leitura repetida -> se a placa mandar a mesma leitura
                                     duas vezes, entram duas linhas

  O que continua valendo:
   . a funcao n() converte texto em numero (e o que impede o Google de
     entender "21.6" como a data 21 de junho)
   . a data e a hora vem da placa, nao do servidor
   . o formato de cada coluna e carimbado na hora de gravar

  PUBLICAR: Implantar > Gerenciar implantacoes > lapis > Nova versao.
  Nunca "Nova implantacao", que troca o endereco.
  ==========================================================================
*/

function doGet(e) {
  try {
    var planilha = SpreadsheetApp.getActiveSpreadsheet().getSheets()[0];

    // so cria o cabecalho se a planilha estiver completamente vazia
    if (planilha.getLastRow() === 0) {
      planilha.appendRow([
        "Data", "Hora",
        "Temperatura do ar (C)", "Umidade relativa (%)",
        "Temperatura BMP280 (C)", "Pressao atmosferica (hPa)",
        "Precipitacao do dia (mm)", "Velocidade do vento (m/s)",
        "Direcao do vento (graus)", "Direcao do vento",
        "Nivel da agua (m)",
        "Status DHT22", "Status BMP280", "Status pluviometro",
        "Status anemometro", "Status biruta", "Status nivel"
      ]);
      planilha.getRange(1, 1, 1, 17).setFontWeight("bold");
      planilha.setFrozenRows(1);
    }

    // converte texto em numero
    function n(v) {
      if (v === undefined || v === null || v === "") return "";
      var x = parseFloat(v);
      return isNaN(x) ? "" : x;
    }

    // texto puro (direcao e status)
    function t(v) {
      return (v === undefined || v === null) ? "" : String(v);
    }

    // data e hora vindas da placa; se nao vierem, usa a do servidor
    var fuso  = "America/Sao_Paulo";
    var agora = new Date();

    var data = t(e.parameter.data);
    var hora = t(e.parameter.hora);
    if (data === "") data = Utilities.formatDate(agora, fuso, "dd/MM/yyyy");
    if (hora === "") hora = Utilities.formatDate(agora, fuso, "HH:mm:ss");

    planilha.appendRow([
      data,
      hora,
      n(e.parameter.tempdht),
      n(e.parameter.umidade),
      n(e.parameter.tempbmp),
      n(e.parameter.pressao),
      n(e.parameter.chuva),
      n(e.parameter.vento),
      n(e.parameter.graus),
      t(e.parameter.direcao),
      n(e.parameter.nivel),
      t(e.parameter.stdht),
      t(e.parameter.stbmp),
      t(e.parameter.stpluv),
      t(e.parameter.stanem),
      t(e.parameter.stbir),
      t(e.parameter.stniv)
    ]);

    // carimba o formato de cada coluna na linha recem-gravada
    var linha = planilha.getLastRow();
    planilha.getRange(linha, 1, 1, 17).setNumberFormats([[
      "dd/MM/yyyy",   // Data
      "HH:mm:ss",     // Hora
      "0.0",          // Temperatura do ar
      "0.0",          // Umidade
      "0.0",          // Temperatura BMP280
      "0.0",          // Pressao
      "0.00",         // Precipitacao do dia
      "0.00",         // Velocidade do vento
      "0",            // Direcao em graus
      "@",            // Direcao (texto)
      "0.000",        // Nivel da agua
      "@", "@", "@", "@", "@", "@"   // os seis status
    ]]);

    return ContentService.createTextOutput("OK");

  } catch (erro) {
    return ContentService.createTextOutput("ERRO: " + erro.message);
  }
}
