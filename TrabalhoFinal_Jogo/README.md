# Trabalho Final SISEM: versão "Desafio do balão"

Versão em jogo do trabalho final (pasta [`../TrabalhoFinal`](../TrabalhoFinal)).
A aquisição e o processamento não mudaram. Mudaram a interface no LCD, a lógica de jogo e o aviso de "bloco pronto", que passou de polling para interrupção do DMA (NVIC). Foram acrescentados o botão SW5 (EXTI), comandos pelo terminal (USART RX), um LED em PWM e três dificuldades.

## Como se joga

1. Com a placa parada, o LCD mostra **ABANA P/ JOGAR**.
2. Quando abanas a placa (Med5 ≥ 120 mg), aparece **VAI! ABANA!** e o cronómetro começa.
3. O objetivo é rebentar o balão **no menor tempo possível**, com um máximo de 20 s.
4. Quando rebenta, aparecem a explosão e **POP!**, o LED PB1 acende e o tempo fica como recorde (**REC**) se for o melhor até agora.
5. Se passarem 20 s sem rebentar, aparece **TEMPO ESGOTADO**.
6. Para jogar outra vez, pousa a placa (Med5 ≤ 95 mg) e volta a abanar.

O recorde fica em RAM (um por dificuldade): perde-se ao fazer reset ou desligar a placa.

## Controlos

| Onde | O quê | Quando |
| --- | --- | --- |
| Botão SW5 (PA1) | Passa para a dificuldade seguinte: FÁCIL → NORMAL → DIFÍCIL | Só na espera |
| Terminal: `h` ou `?` | Mostra a ajuda | Sempre |
| Terminal: `1`, `2`, `3` | Dificuldade FÁCIL (300 mg), NORMAL (500 mg), DIFÍCIL (700 mg) | Só na espera |
| Terminal: `r` | Apaga os recordes das 3 dificuldades | Só na espera |

**Dificuldade:** muda o limiar de rebentamento e o fim de escala da barra (que têm de ser iguais, porque só rebenta com a barra cheia). O nome aparece no LCD entre o "0" e a escala, com cor verde, amarela ou vermelha.

**LED PB0:** brilha mais quanto mais cheia está a barra. O LED PB1 continua a acender quando o balão rebenta.

## O que ficou igual

- RCC a 72 MHz (HSE 12 MHz × 6).
- ADC1 em scan de 3 canais (PC2, PC1, PC0), disparado pelo TIM3 a 200 Hz.
- DMA1 canal 1 circular, com duas metades de 32 amostras (blocos de 160 ms).
- Cálculo do RMS AC por eixo, RMS total, mediana dos últimos 5 blocos e histerese dos níveis.
- Limiar de rebentamento: Med5 ≥ 500 mg em 3 blocos seguidos, com a barra e o balão no máximo.
- USART2 a 9600 bps e SPI1 para o LCD.
- `lcd.c` e `syscalls.c` são cópias iguais às de `TrabalhoFinal`.

## Periféricos novos

| Periférico | Configuração | Função no código |
| --- | --- | --- |
| EXTI1 (SW5, PA1) | PA1 em `GPIO_Mode_IPU`, `GPIO_EXTILineConfig(GPIOA, PinSource1)`, flanco descendente | `Botao_EXTI_Config()` |
| USART2 RX | `USART_ITConfig(USART2, USART_IT_RXNE, ENABLE)` | em `USART2_Config()` |
| TIM3 canal 3 (PB0) | PWM1, polaridade alta, pré-carga ativa, mesmo período do TIM3 (200 Hz, ARR = 49); duty = CCR3 / 50 | `PWM_LED_Config()`, `AtualizaPWM()` |
| NVIC | `PriorityGroup_1`. DMA1_Channel1: preempção 0. EXTI1: preempção 1, sub 0. USART2: preempção 1, sub 1 | `NVIC_Config()` |

**Porque estas prioridades:** se o DMA não for atendido a tempo, a metade seguinte é escrita por cima e perdem-se dados, por isso tem a preempção mais alta e pode interromper as outras. O botão e a USART acontecem à velocidade humana; ficam com a mesma preempção e a sub-prioridade só decide qual é atendida primeiro se chegarem ao mesmo tempo.

**Rotinas de interrupção** (`stm32f10x_it.c`): as três só limpam a flag e marcam uma variável `volatile` (`dma_metade_pronta`, `botao_pressionado`, `comando_usart`). O `main` lê e limpa as três de uma vez, entre `__disable_irq()` e `__enable_irq()`, e faz o trabalho (LCD, USART TX) fora das interrupções. O antigo `EXTI1_IRQHandler` da Ficha 4, que ficava preso num `while` à espera de largar o botão, foi substituído.

**Debounce do botão:** o `main` ignora toques a menos de 2 blocos (320 ms) do anterior.

## Interrupção do DMA (NVIC)

Na versão original, o `while(1)` verificava as flags HT1/TC1 do DMA por polling. Agora é o DMA que avisa por interrupção:

| Onde | O quê |
| --- | --- |
| `main.c`, `ADC1_DMA_Config()` | `DMA_ITConfig(DMA1_Channel1, DMA_IT_HT \| DMA_IT_TC, ENABLE)`: interrupção a meio (HT) e no fim (TC) do buffer |
| `main.c`, `NVIC_DMA_Config()` | `NVIC_PriorityGroup_1`, canal `DMA1_Channel1_IRQn`, preempção 0, sub-prioridade 0, `ENABLE` |
| `stm32f10x_it.c`, `DMA1_Channel1_IRQHandler()` | Rotina curta: limpa `DMA1_IT_HT1` / `DMA1_IT_TC1` e marca o bit 0 ou 1 da variável `volatile uint8_t dma_metade_pronta` |
| `main.c`, `while(1)` | Lê e limpa `dma_metade_pronta` com `__disable_irq()` / `__enable_irq()` (secção crítica curta) e processa a metade marcada |

O cálculo do RMS e o desenho no LCD continuam no `main`, porque uma rotina de interrupção deve ser curta.
O NVIC é configurado antes de ligar o DMA e o TIM3, para não perder a primeira interrupção.

## O que mudou no jogo (`main.c`)

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
2. Substitui o `main.c` e o `stm32f10x_it.c` por estes (o handler do DMA está no `stm32f10x_it.c`). Confirma que o projeto tem `lcd.c`, `lcd.h` e `syscalls.c`.
3. Compila, grava a placa e abre um terminal a 9600 bps (por exemplo `screen /dev/ttyUSB0 9600`).
4. Verifica: espera → início → rebentar → recorde → voltar à espera → tempo esgotado.
5. Carrega no SW5 na espera: a dificuldade muda no LCD e no terminal.
6. No terminal escreve `h`, `1`, `3` e `r`.
7. Durante o jogo, o LED PB0 deve brilhar mais à medida que a barra enche.

Estado: a lógica foi compilada e simulada no PC com cabeçalhos falsos; falta confirmar na placa as posições do texto no LCD, as interrupções (no terminal, `DMA_desc` deve ficar a 0 e o número do bloco deve subir cerca de 6 vezes por segundo) e se o LED PB0 é ativo a 1 (se acender ao contrário, pôr `LED_ATIVO_ALTO` a `0U`).
