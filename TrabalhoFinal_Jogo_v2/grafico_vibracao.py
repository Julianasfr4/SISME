"""
Trabalho Final SISEM - Grafico das vibracoes recebidas pela USART

Le as linhas que a placa envia pela USART2 (9600 bps):
    Bloco 120 | RMS: X=35 Y=41 Z=28 Total=61 Med5=58 mg [INICIO]
guarda-as num .csv e desenha (e grava num .png):
    1) o RMS de cada eixo (X, Y, Z);
    2) o RMS total e a mediana dos 5 ultimos blocos (Med5), com os
       niveis de vibracao (MEDIA 120 mg, ALTA 260 mg) e o fim da barra.

Sequencia de demonstracao sugerida (60 s):
    repouso -> toques leves na mesa -> telemovel a vibrar ao lado da
    placa -> abanar devagar -> repouso.

Uso:
    python3 grafico_vibracao.py                      (porta /dev/ttyACM0, 60 s)
    python3 grafico_vibracao.py --porta COM3 --tempo 90
    python3 grafico_vibracao.py --ficheiro registo.txt   (texto ja gravado)

Precisa de: pip install pyserial matplotlib
"""

import argparse
import csv
import re
import time
from datetime import datetime

import matplotlib
matplotlib.use("Agg")  # so grava o .png, nao precisa de janela
import matplotlib.pyplot as plt

T_BLOCO_S = 0.160                     # 32 amostras / 200 Hz
# Niveis de vibracao mostrados no LCD (entrada) e fim da escala da barra.
LIMIARES = [(120, "VIB MEDIA"), (260, "VIB ALTA"), (500, "fim da barra")]

# Linha de dados: bloco, X, Y, Z, Total, Med5 e um evento opcional [..].
PADRAO = re.compile(
    r"Bloco (\d+) \| RMS: X=(\d+) Y=(\d+) Z=(\d+) Total=(\d+) Med5=(\d+) mg"
    r"(?: \[([A-Z ]+)\])?"
)


def trata_linha(linha, dados):
    """Se a linha for de dados, junta-a a lista. As outras sao ignoradas."""
    m = PADRAO.search(linha)
    if m is None:
        return
    bloco, x, y, z, total, med5 = (int(v) for v in m.groups()[:6])
    evento = m.group(7) or ""
    dados.append([bloco, x, y, z, total, med5, evento])


def le_porta(porta, tempo_s, dados):
    """Le a porta serie durante tempo_s segundos."""
    import serial  # so e preciso quando se le da placa

    print(f"A ler {porta} durante {tempo_s} s... (Ctrl+C para parar antes)")
    with serial.Serial(porta, 9600, timeout=0.5) as s:
        s.reset_input_buffer()
        inicio = time.time()
        try:
            while time.time() - inicio < tempo_s:
                linha = s.readline().decode("ascii", errors="ignore").strip()
                if linha:
                    print(linha)  # eco, como num terminal
                    trata_linha(linha, dados)
        except KeyboardInterrupt:
            print("Parado pelo utilizador.")


def le_ficheiro(nome, dados):
    """Le um registo do terminal ja gravado num ficheiro de texto."""
    with open(nome, encoding="utf-8", errors="ignore") as f:
        for linha in f:
            trata_linha(linha, dados)


def grava_csv(nome, tempo, dados):
    with open(nome, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Bloco", "Tempo_s", "X_mg", "Y_mg", "Z_mg",
                    "Total_mg", "Med5_mg", "Evento"])
        for t, linha in zip(tempo, dados):
            w.writerow([linha[0], f"{t:.2f}"] + linha[1:])


def desenha(nome, tempo, dados):
    x = [d[1] for d in dados]
    y = [d[2] for d in dados]
    z = [d[3] for d in dados]
    total = [d[4] for d in dados]
    med5 = [d[5] for d in dados]

    fig, (g1, g2) = plt.subplots(2, 1, figsize=(11, 7), sharex=True)

    # 1) RMS de cada eixo
    g1.plot(tempo, x, label="X")
    g1.plot(tempo, y, label="Y")
    g1.plot(tempo, z, label="Z")
    g1.set_ylabel("RMS AC (mg)")
    g1.set_title("RMS por eixo (blocos de 32 amostras a 200 Hz)")
    g1.grid(True)
    g1.legend(loc="upper left")

    # 2) RMS total, Med5 e niveis de vibracao
    g2.plot(tempo, total, color="0.6", label="Total")
    g2.plot(tempo, med5, color="black", linewidth=2, label="Med5")
    for valor, nome_nivel in LIMIARES:
        g2.axhline(valor, linestyle="--", color="tab:blue", linewidth=1)
        g2.text(tempo[-1], valor, f"{nome_nivel} {valor} mg ",
                ha="right", va="bottom", color="tab:blue")
    g2.set_xlabel("Tempo (s)")
    g2.set_ylabel("RMS total (mg)")
    g2.set_title("RMS total e mediana dos 5 ultimos blocos (Med5)")
    g2.grid(True)
    g2.legend(loc="upper left")

    fig.tight_layout()
    fig.savefig(nome, dpi=150)


def main():
    p = argparse.ArgumentParser(description="Grafico das vibracoes do ADXL337")
    p.add_argument("--porta", default="/dev/ttyACM0",
                   help="porta serie (Windows: COM3, ...)")
    p.add_argument("--tempo", type=float, default=60,
                   help="segundos de aquisicao")
    p.add_argument("--ficheiro",
                   help="ler um registo gravado em vez da porta")
    args = p.parse_args()

    dados = []
    if args.ficheiro:
        le_ficheiro(args.ficheiro, dados)
    else:
        le_porta(args.porta, args.tempo, dados)

    if not dados:
        print("Nao chegou nenhuma linha 'Bloco ... | RMS: ...'. "
              "Confirma a porta e se a placa esta a correr.")
        return

    # Tempo a partir do numero do bloco (cada bloco = 160 ms).
    # Se a numeracao voltar atras (reset da placa a meio), o tempo continua
    # a contar a partir do ponto anterior, como se fossem 2 blocos.
    tempo = [0.0]
    for anterior, atual in zip(dados, dados[1:]):
        passo = atual[0] - anterior[0]
        if passo <= 0:
            passo = 2
        tempo.append(tempo[-1] + passo * T_BLOCO_S)

    base = "vibracao_" + datetime.now().strftime("%Y%m%d_%H%M%S")
    grava_csv(base + ".csv", tempo, dados)
    desenha(base + ".png", tempo, dados)
    print(f"Guardado: {base}.csv ({len(dados)} linhas) e {base}.png")


if __name__ == "__main__":
    main()
