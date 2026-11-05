# Trabalho Final SISEM: versão "Desafio do balão"

Versão em jogo do trabalho final (pasta [`../TrabalhoFinal`](../TrabalhoFinal)).
A aquisição e o processamento não mudaram. Só mudaram a interface no LCD e a lógica de jogo.

Há uma versão mais completa em [`../TrabalhoFinal_Jogo_v2`](../TrabalhoFinal_Jogo_v2), com interrupções (DMA, botão e USART), LED em PWM e dificuldades.

## Como se joga

1. Com a placa parada, o LCD mostra **ABANA P/ JOGAR**.
2. Quando abanas a placa (Med5 ≥ 120 mg), aparece **VAI! ABANA!** e o cronómetro começa.
3. O objetivo é rebentar o balão **no menor tempo possível**, com um máximo de 20 s.
4. Quando rebenta, aparecem a explosão e **POP!**, o LED PB1 acende e o tempo fica como recorde (**REC**) se for o melhor até agora.
5. Se passarem 20 s sem rebentar, aparece **TEMPO ESGOTADO**.
6. Para jogar outra vez, pousa a placa (Med5 ≤ 95 mg) e volta a abanar.

O recorde fica em RAM: perde-se ao fazer reset ou desligar a placa.

## O que ficou igual

- RCC a 72 MHz (HSE 12 MHz × 6).
- ADC1 em scan de 3 canais (PC2, PC1, PC0), disparado pelo TIM3 a 200 Hz.
- DMA1 canal 1 circular, com duas metades de 32 amostras (blocos de 160 ms).
- Cálculo do RMS AC por eixo, RMS total, mediana dos últimos 5 blocos e histerese dos níveis.
- Limiar de rebentamento: Med5 ≥ 500 mg em 3 blocos seguidos, com a barra e o balão no máximo.
- USART2 a 9600 bps e SPI1 para o LCD.
- `lcd.c`, `stm32f10x_it.c` e `syscalls.c` são cópias iguais às de `TrabalhoFinal`.

## O que mudou (só em `main.c`)

| Alteração | Antes | Agora |
| --- | --- | --- |
| Lógica principal | `TrataRebentamento()`: rebenta e reinicia sozinho | `AtualizaJogo()`: máquina de estados ESPERA → ATIVO → REBOU/FIM → ESPERA |
| Cronómetro | Não existia | Conta blocos de 160 ms e mostra `T:ss.d` |
| Recorde | Não existia | Melhor tempo em `recorde_blocos`, mostrado como `REC:ss.d` |
| Tempo limite | Não existia | 125 blocos = 20 s (`JOGO_LIMITE_BLOCOS`) |
| Ritmo da animação | A cada 4 blocos (640 ms) | A cada 2 blocos (320 ms), `BARRA_PERIODO_BLOCOS = 2` |
| Tempo a mostrar o resultado | 8 blocos (≈1,3 s) | 16 blocos (≈2,6 s), `REBOU_DURACAO_BLOCOS = 16` |
| Barra | Toda verde | Verde até 40 px, amarelo até 75 px, vermelho depois (`PintaBarra()`) |
| Balão | Só vermelho | Com brilho branco (`DesenhaBrilho()`) |
| Rebentamento | Texto "REBOU!" | Pedaços espalhados + "POP!" em tamanho 2 (`DesenhaExplosao()`) e "NOVO RECORDE!" quando é o melhor tempo |
| Cabeçalho | "SISEM VIBRACAO" | "DESAFIO DO BALAO" com linha laranja |
| Linha de estado | Só o nível de vibração | Mensagens do jogo (`MostraEstado()`); o nível VIB só aparece durante o jogo, a partir de 1 s |
| Terminal | Marcas `[REBOU]`, `[REINICIO AUTO]` | Marcas `[INICIO]`, `[REBOU]`, `[TEMPO ESGOTADO]`, `[ESPERA]` e a linha `>>> REBENTOU em X s \| Recorde: Y s` |

## Máquina de estados do jogo

```
ESPERA --(abanar: Med5 >= 120 mg e rearmado)--> ATIVO
ATIVO  --(rebenta)----------------------------> REBOU --(16 blocos)--> ESPERA
ATIVO  --(20 s sem rebentar)------------------> FIM   --(16 blocos)--> ESPERA
```

Nenhum estado bloqueia: o ADC e o DMA continuam a adquirir durante todo o jogo.

## Como testar

1. No STM32CubeIDE, abre o projeto da placa (3SO) e guarda uma cópia do `main.c` atual.
2. Substitui o `main.c` por este. Confirma que o projeto tem `lcd.c`, `lcd.h` e `syscalls.c`.
3. Compila, grava a placa e abre um terminal a 9600 bps (por exemplo `screen /dev/ttyUSB0 9600`).
4. Verifica: espera → início → rebentar → recorde → voltar à espera → tempo esgotado.

Estado: a lógica foi compilada e simulada no PC com cabeçalhos falsos; falta confirmar na placa as posições do texto no LCD.
