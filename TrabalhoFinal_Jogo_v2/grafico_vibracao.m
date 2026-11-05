%% Trabalho Final SISEM - Grafico das vibracoes recebidas pela USART
%
% Le as linhas que a placa envia pela USART2 (9600 bps):
%   Bloco 120 | RMS: X=35 Y=41 Z=28 Total=61 Med5=58 mg [INICIO]
% guarda-as num .csv e desenha:
%   1) o RMS de cada eixo (X, Y, Z);
%   2) o RMS total e a mediana dos 5 ultimos blocos (Med5), com os
%      limiares dos niveis do jogo e os eventos (INICIO, REBOU, ...).
%
% Sequencia de demonstracao sugerida (cerca de 60 s):
%   10 s em repouso -> 10 s a abanar fraco -> 10 s a abanar forte ->
%   um jogo (segurar SW5 1 s) ate rebentar -> repouso.
%
% Funciona no MATLAB R2019b ou mais recente (serialport, yline, xline).
% Para usar um registo ja gravado (texto copiado do terminal), por
% USAR_FICHEIRO = true e indicar o ficheiro em FICHEIRO_LOG.

clear; clc; close all;

%% Configuracao
PORTA         = "/dev/ttyACM0";  % Windows: "COM3" (ver no Gestor de Dispositivos)
BAUD          = 9600;
DURACAO_S     = 60;              % tempo de aquisicao
USAR_FICHEIRO = false;           % true: ler FICHEIRO_LOG em vez da porta
FICHEIRO_LOG  = "registo_usart.txt";
NOME_SAIDA    = "vibracao_" + string(datetime("now", "Format", "yyyyMMdd_HHmmss"));

T_BLOCO_S     = 0.160;               % 32 amostras / 200 Hz
LIMIARES_MG   = [300 500 700];       % FACIL, NORMAL, DIFICIL
NOMES_NIVEIS  = ["FACIL" "NORMAL" "DIFICIL"];
VIB_MG        = [120 260];           % VIB: MEDIA e VIB: ALTA (entrada)

% Linha de dados: numero do bloco, X, Y, Z, Total, Med5 e marca opcional.
PADRAO = 'Bloco (\d+) \| RMS: X=(\d+) Y=(\d+) Z=(\d+) Total=(\d+) Med5=(\d+) mg( \[[A-Z ]+\])?';

%% Aquisicao
dados   = zeros(0, 6);   % [bloco X Y Z Total Med5]
eventos = strings(0, 1);

if USAR_FICHEIRO
    linhas = readlines(FICHEIRO_LOG);
    for k = 1:numel(linhas)
        [dados, eventos] = TrataLinha(linhas(k), PADRAO, dados, eventos);
    end
else
    s = serialport(PORTA, BAUD);
    configureTerminator(s, "CR/LF");
    flush(s);
    fprintf("A ler %s durante %d s...\n", PORTA, DURACAO_S);

    t0 = tic;
    while toc(t0) < DURACAO_S
        if s.NumBytesAvailable > 0
            linha = readline(s);
            fprintf("%s\n", linha);  % eco, como num terminal
            [dados, eventos] = TrataLinha(linha, PADRAO, dados, eventos);
        else
            pause(0.02);
        end
    end
    clear s;  % fecha a porta
end

if isempty(dados)
    error("Nao chegou nenhuma linha 'Bloco ... | RMS: ...'. Confirma a porta e o baud rate.");
end

%% Tempo (s) a partir do numero do bloco
t = (dados(:, 1) - dados(1, 1)) * T_BLOCO_S;

%% Guardar em CSV
tabela = table(dados(:, 1), t, dados(:, 2), dados(:, 3), dados(:, 4), ...
               dados(:, 5), dados(:, 6), eventos, ...
               'VariableNames', {'Bloco', 'Tempo_s', 'X_mg', 'Y_mg', 'Z_mg', ...
                                 'Total_mg', 'Med5_mg', 'Evento'});
writetable(tabela, NOME_SAIDA + ".csv");
fprintf("Guardado: %s.csv (%d linhas)\n", NOME_SAIDA, height(tabela));

%% Grafico
fig = figure("Name", "Vibracoes ADXL337", "Position", [100 100 1100 700]);

subplot(2, 1, 1);
plot(t, dados(:, 2), t, dados(:, 3), t, dados(:, 4), "LineWidth", 1.2);
grid on;
ylabel("RMS AC (mg)");
title("RMS por eixo (blocos de 32 amostras a 200 Hz)");
legend("X", "Y", "Z", "Location", "northwest");

subplot(2, 1, 2);
plot(t, dados(:, 5), "Color", [0.6 0.6 0.6], "LineWidth", 1.0); hold on;
plot(t, dados(:, 6), "k", "LineWidth", 1.8);
for k = 1:numel(LIMIARES_MG)
    yline(LIMIARES_MG(k), "--", NOMES_NIVEIS(k) + " " + LIMIARES_MG(k) + " mg", ...
          "LabelHorizontalAlignment", "left");
end
yline(VIB_MG(1), ":", "VIB MEDIA");
yline(VIB_MG(2), ":", "VIB ALTA");

% Eventos do jogo como linhas verticais.
idx = find(eventos ~= "" & eventos ~= "LCD");
for k = idx'
    xline(t(k), "-", eventos(k), "Color", [0.85 0.33 0.10], ...
          "LabelVerticalAlignment", "top");
end
hold off;
grid on;
xlabel("Tempo (s)");
ylabel("RMS total (mg)");
title("RMS total e mediana dos 5 ultimos blocos (Med5)");
legend("Total", "Med5", "Location", "northwest");

exportgraphics(fig, NOME_SAIDA + ".png", "Resolution", 200);
fprintf("Guardado: %s.png\n", NOME_SAIDA);

%% Funcoes locais
function [dados, eventos] = TrataLinha(linha, padrao, dados, eventos)
    tok = regexp(char(linha), padrao, "tokens", "once");
    if isempty(tok)
        return;  % linhas de ajuda, ">>> REBENTOU ...", etc.
    end
    valores = str2double(tok(1:6));
    marca = "";
    if numel(tok) >= 7
        marca = strtrim(erase(erase(string(tok{7}), "["), "]"));
    end
    dados(end + 1, :) = valores;
    eventos(end + 1, 1) = marca;
end
