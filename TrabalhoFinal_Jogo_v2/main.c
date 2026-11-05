/**

 * Trabalho Final SISEM

 * Sistema de Aquisicao e Analise de Vibracoes

 *

 * VERSAO JOGO - ADC + DMA (com interrupcao) + TIM3 + LCD + DESAFIO DO BALAO

 *

 * STM32F103RBT6

 * ADXL337:

 *   X -> PC2 -> ADC1_IN12

 *   Y -> PC1 -> ADC1_IN11

 *   Z -> PC0 -> ADC1_IN10

 *

 * DESAFIO DO BALAO:

 *   1) Ecra inicial (menu): escolher FACIL, NORMAL ou DIFICIL.

 *      SW5: clique muda o nivel; segurar 1 s comeca o desafio.

 *   2) Contagem 3, 2, 1 e depois "VAI! ABANA!": o cronometro comeca.

 *   3) Objetivo: rebentar o balao no menor tempo possivel (max. 20 s).

 *   4) No fim (POP ou tempo esgotado) volta ao menu.

 *

 * EXTRAS (aplicam NVIC, EXTI, USART RX e PWM):

 *   - SW5 (PA1, EXTI1): navega no menu (FACIL/NORMAL/DIFICIL = 300/500/700 mg).

 *   - Terminal (USART2 RX): h ajuda, 1/2/3 nivel, j jogar, r apaga recordes.

 *   - LED PB0 (TIM3_CH3 em PWM): brilho proporcional a barra.

 *   A aquisicao (ADC/DMA/TIM3) e os calculos (RMS, Med5) nao mudaram.

 */

#include "stm32f10x.h"

#include "lcd.h"

#include <stdio.h>

#include <math.h>

#define AMOSTRAS_BLOCO 32U
#define ELEMENTOS_MEIO_DMA (AMOSTRAS_BLOCO * 3U)
// Reduz a ocupacao da USART a 9600 bps; os eventos LCD/REBOU sao sempre escritos.
#define USART_LOG_CADA_BLOCOS 2U

// Visualizacao: barra + balao com subida moderada e descida lenta.
// A escala grafica e o limiar de rebentamento sao configuracoes diferentes.
#define BARRA_X                   12U
#define BARRA_Y                   73U
#define BARRA_LARGURA            104U
#define BARRA_ALTURA               8U
#define BARRA_RUIDO_REPOUSO_MG   35U
#define BARRA_PERIODO_BLOCOS      2U  // 2 x 160 ms = 320 ms (jogo mais fluido)
#define BARRA_DELTA_MIN_PX        3U
#define BARRA_PASSO_SUBIDA_PX    12U
#define BARRA_PASSO_DESCIDA_PX    5U  // Descida intencionalmente mais lenta

#define BALAO_CX                 64U
#define BALAO_CY                112U
#define BALAO_RAIO_MIN            7U
#define BALAO_RAIO_MAX           22U
#define BALAO_LINHA_INICIAL     90U
#define BALAO_NUM_FAIXAS        11U
#define BALAO_ALT_FAIXA          4U
#define BALAO_CORDA_FIM        154U

// Niveis de vibracao em mg (MED5), com histerese para nao oscilar no limite.
#define NIVEL_MEDIA_ENTRA_MG    120U
#define NIVEL_MEDIA_SAI_MG       95U
#define NIVEL_ALTA_ENTRA_MG     260U
#define NIVEL_ALTA_SAI_MG       230U

// Rebenta quando a MEDIANA passa o limiar do nivel em 3 blocos seguidos.
#define REBENTAMENTO_BLOCOS       3U

// Mostrar o rebentamento (ou o fim do tempo) durante 16 blocos (~2,6 s).
// Apos esse periodo, o jogo volta ao menu.
#define REBOU_DURACAO_BLOCOS     16U

// PB1: LED do rebentamento. Se for ativo a zero, usar 0U.
#define LED_ATIVO_ALTO            1U

#define LCD_PRETO  0x0000U
#define LCD_BRANCO 0xFFFFU
#define LCD_VERDE  0x07E0U
#define LCD_VERMELHO 0xF800U
#define LCD_CINZA  0xC618U
#define LCD_AMARELO 0xFFE0U
#define LCD_AZUL   0x001FU
#define LCD_CIANO  0x07FFU
#define LCD_LARANJA 0xFD20U

// =====================================================
// JOGO: DESAFIO DO BALAO
// =====================================================
#define JOGO_MENU     0U   // ecra inicial: escolher o nivel
#define JOGO_ATIVO    1U   // cronometro a contar
#define JOGO_REBOU    2U   // balao rebentou: mostra o tempo
#define JOGO_FIM      3U   // tempo esgotado sem rebentar
#define JOGO_CONTAGEM 4U   // contagem 3, 2, 1 antes de comecar

#define JOGO_LIMITE_BLOCOS  125U  // 125 x 160 ms = 20 s
#define JOGO_MS_POR_BLOCO   160U  // 32 amostras / 200 Hz
#define CONTAGEM_BLOCOS      18U  // 3 x 6 blocos = ~3 s
#define CONTAGEM_PASSO        6U  // ~1 s por numero

// Botao SW5 no menu: largar antes de 1 s = clique; segurar 1 s = jogar.
#define BOTAO_SEGURAR_BLOCOS  6U  // 6 x 160 ms = ~1 s
#define BOTAO_DEBOUNCE_BLOCOS 2U  // ignora ressaltos durante 320 ms

// Menu: uma linha por nivel (texto em tamanho 2 = 16 px de altura).
#define MENU_Y0              40U
#define MENU_PASSO           32U
#define JOGO_REC_X           58U  // a direita do nome do nivel

// Zonas de cor da barra (em pixeis): verde, amarelo, vermelho.
#define BARRA_ZONA_AMARELA   40U
#define BARRA_ZONA_VERMELHA  75U

// Linha de texto de estado (por baixo do cabecalho).
#define ESTADO_Y             33U
#define TEMPO_Y              22U

// 2 metades, 32 amostras por metade, 3 eixos

volatile uint16_t adc_buffer[2][AMOSTRAS_BLOCO][3];
// Metades do buffer prontas, marcadas pela interrupcao do DMA
// (DMA1_Channel1_IRQHandler em stm32f10x_it.c):
//   bit 0 = 1.a metade pronta (Half Transfer)
//   bit 1 = 2.a metade pronta (Transfer Complete)
// volatile: e alterada dentro da interrupcao e lida no main.
volatile uint8_t dma_metade_pronta = 0U;
// Numero de metades DMA rejeitadas por terem sido alcancadas pela escrita.
static uint32_t blocos_dma_descartados = 0U;

// =====================================================

// RCC - 72 MHz

// =====================================================

void RCC_Config_HSE_PLL_Max(void)

{

    RCC_DeInit();

    RCC_HSEConfig(RCC_HSE_ON);

    if (RCC_WaitForHSEStartUp() == SUCCESS)

    {

        FLASH_SetLatency(FLASH_Latency_2);

        FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);

        RCC_HCLKConfig(RCC_SYSCLK_Div1);

        RCC_PCLK1Config(RCC_HCLK_Div2);

        RCC_PCLK2Config(RCC_HCLK_Div1);

        RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_6);

        RCC_PLLCmd(ENABLE);

        while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);

        RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);

        while (RCC_GetSYSCLKSource() != 0x08);

    }

    else

    {

        while (1);

    }

}

// =====================================================

// USART2 - 9600 bps

// =====================================================

void USART2_Config(void)

{

    GPIO_InitTypeDef GPIO_InitStructure;

    USART_InitTypeDef USART_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    // PA2 - TX

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;

    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;

    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // PA3 - RX

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;

    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = 9600;

    USART_InitStructure.USART_WordLength = USART_WordLength_8b;

    USART_InitStructure.USART_StopBits = USART_StopBits_1;

    USART_InitStructure.USART_Parity = USART_Parity_No;

    USART_InitStructure.USART_HardwareFlowControl =

        USART_HardwareFlowControl_None;

    USART_InitStructure.USART_Mode =

        USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(USART2, &USART_InitStructure);

    // Interrupcao quando chega um caracter (comandos do PC)
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

    USART_Cmd(USART2, ENABLE);

}

// =====================================================

// ENVIAR TEXTO PELA USART2

// =====================================================

void USART2_EnviaTexto(const char *texto)

{

    while (*texto != '\0')

    {

        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE)

               == RESET);

        USART_SendData(USART2, (uint16_t)*texto);

        texto++;

    }

    while (USART_GetFlagStatus(USART2, USART_FLAG_TC)

           == RESET);

}

// =====================================================

// ADC1 + DMA1

// =====================================================

void ADC1_DMA_Config(void)

{

    GPIO_InitTypeDef GPIO_InitStructure;

    ADC_InitTypeDef ADC_InitStructure;

    DMA_InitTypeDef DMA_InitStructure;

    // 1. Ativar clocks

    RCC_APB2PeriphClockCmd(

        RCC_APB2Periph_GPIOC | RCC_APB2Periph_ADC1,

        ENABLE

    );

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    // ADC = 72 MHz / 6 = 12 MHz

    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    // 2. Entradas analogicas PC0, PC1 e PC2

    GPIO_InitStructure.GPIO_Pin =

        GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2;

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;

    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // 3. Configurar DMA

    DMA_DeInit(DMA1_Channel1);

    DMA_InitStructure.DMA_PeripheralBaseAddr =

        (uint32_t)&ADC1->DR;

    DMA_InitStructure.DMA_MemoryBaseAddr =

        (uint32_t)&adc_buffer[0][0][0];

    DMA_InitStructure.DMA_DIR =

        DMA_DIR_PeripheralSRC;

    // 2 blocos x 32 amostras x 3 eixos

    DMA_InitStructure.DMA_BufferSize =

        2 * AMOSTRAS_BLOCO * 3;

    DMA_InitStructure.DMA_PeripheralInc =

        DMA_PeripheralInc_Disable;

    DMA_InitStructure.DMA_MemoryInc =

        DMA_MemoryInc_Enable;

    DMA_InitStructure.DMA_PeripheralDataSize =

        DMA_PeripheralDataSize_HalfWord;

    DMA_InitStructure.DMA_MemoryDataSize =

        DMA_MemoryDataSize_HalfWord;

    // Agora o DMA funciona continuamente

    DMA_InitStructure.DMA_Mode =

        DMA_Mode_Circular;

    DMA_InitStructure.DMA_Priority =

        DMA_Priority_High;

    DMA_InitStructure.DMA_M2M =

        DMA_M2M_Disable;

    DMA_Init(DMA1_Channel1, &DMA_InitStructure);

    // Gerar interrupcao a meio (HT) e no fim (TC) do buffer.
    DMA_ITConfig(DMA1_Channel1, DMA_IT_HT | DMA_IT_TC, ENABLE);

    // 4. Configurar ADC

    ADC_DeInit(ADC1);

    ADC_InitStructure.ADC_Mode =

        ADC_Mode_Independent;

    ADC_InitStructure.ADC_ScanConvMode =

        ENABLE;

    ADC_InitStructure.ADC_ContinuousConvMode =

        DISABLE;

    // NOVO: o ADC e desencadeado pelo TIM3

    ADC_InitStructure.ADC_ExternalTrigConv =

        ADC_ExternalTrigConv_T3_TRGO;

    ADC_InitStructure.ADC_DataAlign =

        ADC_DataAlign_Right;

    ADC_InitStructure.ADC_NbrOfChannel = 3;

    ADC_Init(ADC1, &ADC_InitStructure);

    // 5. Configurar a sequencia X, Y, Z

    ADC_RegularChannelConfig(

        ADC1,

        ADC_Channel_12,

        1,

        ADC_SampleTime_239Cycles5

    );

    ADC_RegularChannelConfig(

        ADC1,

        ADC_Channel_11,

        2,

        ADC_SampleTime_239Cycles5

    );

    ADC_RegularChannelConfig(

        ADC1,

        ADC_Channel_10,

        3,

        ADC_SampleTime_239Cycles5

    );

    // 6. Ativar ADC

    ADC_DMACmd(ADC1, ENABLE);

    ADC_Cmd(ADC1, ENABLE);

    // 7. Calibracao

    ADC_ResetCalibration(ADC1);

    while (ADC_GetResetCalibrationStatus(ADC1));

    ADC_StartCalibration(ADC1);

    while (ADC_GetCalibrationStatus(ADC1));

    // NOVO: permitir o trigger externo

    ADC_ExternalTrigConvCmd(ADC1, ENABLE);

}

// =====================================================
// NVIC - DMA1 CANAL 1, EXTI1 (SW5) E USART2 (RX)
// =====================================================
// PriorityGroup_1: 1 bit de preempcao (0..1), 3 bits de sub-prioridade.
// O DMA tem a preempcao mais alta (0): se uma metade nao for tratada a
// tempo, o DMA escreve por cima. O botao e a USART sao a velocidade
// humana: preempcao 1, e entre eles decide a sub-prioridade.
void NVIC_Config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // mais prioritario
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&NVIC_InitStructure);
}

// =====================================================

// TIM3 - FREQUENCIA DE AMOSTRAGEM 200 Hz

// =====================================================

void TIM3_Config_200Hz(void)

{

    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    // TIM3 recebe 72 MHz:

    // PSC = 7199 -> 10 kHz

    // ARR = 49   -> 200 Hz

    TIM_TimeBaseStructure.TIM_Prescaler = 7200 - 1;

    TIM_TimeBaseStructure.TIM_Period = 50 - 1;

    TIM_TimeBaseStructure.TIM_ClockDivision =

        TIM_CKD_DIV1;

    TIM_TimeBaseStructure.TIM_CounterMode =

        TIM_CounterMode_Up;

    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    // O evento de update produz o sinal TRGO

    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);

    // Ainda nao iniciamos o timer

}

// =====================================================
// MEDIANA MOVEL DOS ULTIMOS 5 BLOCOS (RMS TOTAL EM mg)
// Rejeita valores isolados altos e baixos.
// Nao altera as amostras do ADC nem o calculo do RMS.
// =====================================================

static uint32_t MedianaMovel5(uint32_t rms_atual_mg)
{
    static uint32_t historico[5];
    static uint8_t posicao = 0U;
    static uint8_t inicializado = 0U;
    uint32_t ordenado[5];

    // No arranque, evita usar zeros como se fossem amostras reais.
    if (inicializado == 0U)
    {
        for (uint8_t i = 0U; i < 5U; i++)
        {
            historico[i] = rms_atual_mg;
        }
        inicializado = 1U;
    }

    // Substituir a entrada mais antiga no buffer circular.
    historico[posicao] = rms_atual_mg;
    posicao = (uint8_t)((posicao + 1U) % 5U);

    // Copia local para ordenar sem modificar a ordem temporal.
    for (uint8_t i = 0U; i < 5U; i++)
    {
        ordenado[i] = historico[i];
    }

    // Ordenacao por insercao de apenas 5 numeros.
    for (uint8_t i = 1U; i < 5U; i++)
    {
        uint32_t valor = ordenado[i];
        uint8_t j = i;

        while ((j > 0U) && (ordenado[j - 1U] > valor))
        {
            ordenado[j] = ordenado[j - 1U];
            j--;
        }
        ordenado[j] = valor;
    }

    return ordenado[2];
}

// =====================================================
// BARRA + BALAO (desenho incremental)
// =====================================================

static void DesenhaCorda(uint8_t raio_antigo, uint8_t raio_novo);

// Valores de animacao: NAO alteram os dados recolhidos pelo ADC.
static uint8_t barra_visual_px = 0U;
static uint8_t barra_desenhada_px = 0U;
static uint8_t balao_raio_desenhado = 0U;
static uint8_t contador_rebentamento = 0U;
static uint8_t blocos_rebou = 0U;
static uint8_t blocos_contagem = 0U;

// Estado do jogo e cronometro (contados em blocos de 160 ms).
static uint8_t estado_jogo = JOGO_MENU;
static uint32_t tempo_jogo_blocos = 0U;

// Dificuldade: limiar de rebentamento = fim de escala da barra (mg).
// A barra tem de estar cheia para rebentar, por isso os dois sao iguais.
#define DIF_FACIL    0U
#define DIF_NORMAL   1U
#define DIF_DIFICIL  2U
#define NUM_DIFICULDADES 3U
static const uint32_t limiar_dificuldade_mg[NUM_DIFICULDADES] = {300U, 500U, 700U};
static const char *nome_dificuldade[NUM_DIFICULDADES] = {"FACIL", "NORMAL", "DIFICIL"};
static const uint16_t cor_dificuldade[NUM_DIFICULDADES] = {LCD_VERDE, LCD_AMARELO, LCD_VERMELHO};
static uint8_t dificuldade = DIF_NORMAL;
#define LIMIAR_ATUAL_MG  (limiar_dificuldade_mg[dificuldade])

// Recorde (melhor tempo) por dificuldade; 0 = ainda sem recorde.
// No LCD so aparece no jogo, ao lado do nome do nivel.
static uint32_t recorde_blocos[NUM_DIFICULDADES] = {0U, 0U, 0U};

// Marcas escritas pelas rotinas de interrupcao (stm32f10x_it.c).
volatile uint8_t botao_pressionado = 0U;  // EXTI1_IRQHandler (SW5)
volatile char comando_usart = 0;          // USART2_IRQHandler (RXNE)

// Estado do texto de intensidade, independente da velocidade do balao.
#define VIB_BAIXA 0U
#define VIB_MEDIA 1U
#define VIB_ALTA  2U
static uint8_t nivel_vibracao = VIB_BAIXA;

// Escreve uma mensagem centrada na linha de estado.
static void MostraEstado(const char *texto, uint16_t cor)
{
    uint8_t n = 0U;
    while ((texto[n] != '\0') && (n < 18U))
    {
        n++;
    }
    lcd_draw_fillrect(10U, 31U, 108U, 12U, LCD_PRETO);
    lcd_draw_string((uint8_t)(64U - n * 3U), ESTADO_Y,
                    (unsigned char *)texto, cor, 1U);
}

// Cronometro centrado no topo: "TEMPO: 05.4 s".
static void MostraCronometro(void)
{
    char texto[20];
    uint32_t decimas = (tempo_jogo_blocos * JOGO_MS_POR_BLOCO) / 100U;

    snprintf(texto, sizeof(texto), "TEMPO: %02lu.%lu s",
             (unsigned long)(decimas / 10U),
             (unsigned long)(decimas % 10U));
    lcd_draw_string(25U, TEMPO_Y, (unsigned char *)texto, LCD_BRANCO, 1U);
}

// Numero de caracteres de um texto curto (para centrar).
static uint8_t Comprimento(const char *texto)
{
    uint8_t n = 0U;
    while (texto[n] != '\0')
    {
        n++;
    }
    return n;
}

// Nome do nivel escolhido, a esquerda por cima da barra (y = 53).
static void MostraDificuldade(void)
{
    lcd_draw_string(10U, 53U, (unsigned char *)nome_dificuldade[dificuldade],
                    cor_dificuldade[dificuldade], 1U);
}

// Recorde do nivel escolhido: "REC: 05.4 s", ou "REC: --.- s" se ainda nao ha.
// O texto em tamanho 1 pinta o seu fundo, por isso escreve por cima do antigo.
static void MostraRecorde(uint8_t x, uint8_t y)
{
    char texto[20];
    uint32_t decimas = (recorde_blocos[dificuldade] * JOGO_MS_POR_BLOCO) / 100U;

    if (recorde_blocos[dificuldade] == 0U)
    {
        snprintf(texto, sizeof(texto), "REC: --.- s");
    }
    else
    {
        snprintf(texto, sizeof(texto), "REC: %02lu.%lu s",
                 (unsigned long)(decimas / 10U),
                 (unsigned long)(decimas % 10U));
    }
    lcd_draw_string(x, y, (unsigned char *)texto, LCD_CIANO, 1U);
}

// Titulo e linha laranja, iguais no menu e no jogo.
static void DesenhaCabecalho(void)
{
    lcd_draw_string(16U, 6U, (unsigned char *)"DESAFIO DO BALAO",
                    LCD_AMARELO, 1U);
    lcd_draw_fillrect(10U, 16U, 108U, 1U, LCD_LARANJA);
}

// =====================================================
// MENU INICIAL (pagina a parte)
// =====================================================
// Os tres niveis, um por linha: o escolhido na sua cor, os outros a cinzento.
static void DesenhaOpcoes(void)
{
    for (uint8_t i = 0U; i < NUM_DIFICULDADES; i++)
    {
        uint8_t y = (uint8_t)(MENU_Y0 + i * MENU_PASSO);
        const char *nome = nome_dificuldade[i];
        uint16_t cor = (i == dificuldade) ? cor_dificuldade[i] : LCD_CINZA;

        // O texto em tamanho 2 nao apaga o fundo: limpar a linha primeiro.
        lcd_draw_fillrect(0U, y, 128U, 16U, LCD_PRETO);
        lcd_draw_string((uint8_t)(64U - Comprimento(nome) * 6U), y,
                        (unsigned char *)nome, cor, 2U);
    }
}

// Pagina inicial: so os tres niveis.
static void DesenhaMenu(void)
{
    lcd_draw_fillrect(0U, 0U, 128U, 160U, LCD_PRETO);
    DesenhaOpcoes();
}

// Escreve apenas quando ha uma mudanca de nivel (pouco trafego SPI).
static void MostraNivelNoLCD(uint8_t nivel)
{
    const char *texto;
    uint16_t cor;
    if (nivel == VIB_ALTA)
    {
        texto = "VIB: ALTA";
        cor = LCD_VERMELHO;
    }
    else if (nivel == VIB_MEDIA)
    {
        texto = "VIB: MEDIA";
        cor = LCD_AMARELO;
    }
    else
    {
        texto = "VIB: BAIXA";
        cor = LCD_VERDE;
    }

    MostraEstado(texto, cor);
}

static uint8_t AtualizaNivel(uint32_t mediana_mg)
{
    uint8_t novo = nivel_vibracao;

    // So durante o jogo, e nao no 1.o segundo (deixa ver "VAI! ABANA!").
    if ((estado_jogo != JOGO_ATIVO) || (tempo_jogo_blocos < 6U))
    {
        return 0U;
    }

    if (nivel_vibracao == VIB_BAIXA)
    {
        if (mediana_mg >= NIVEL_ALTA_ENTRA_MG)
            novo = VIB_ALTA;
        else if (mediana_mg >= NIVEL_MEDIA_ENTRA_MG)
            novo = VIB_MEDIA;
    }
    else if (nivel_vibracao == VIB_MEDIA)
    {
        if (mediana_mg >= NIVEL_ALTA_ENTRA_MG)
            novo = VIB_ALTA;
        else if (mediana_mg <= NIVEL_MEDIA_SAI_MG)
            novo = VIB_BAIXA;
    }
    else
    {
        if (mediana_mg <= NIVEL_MEDIA_SAI_MG)
            novo = VIB_BAIXA;
        else if (mediana_mg <= NIVEL_ALTA_SAI_MG)
            novo = VIB_MEDIA;
    }

    if (novo == nivel_vibracao)
        return 0U;

    nivel_vibracao = novo;
    MostraNivelNoLCD(novo);
    return 1U;
}

static uint8_t AlvoBarra(uint32_t mediana_mg)
{
    if (mediana_mg <= BARRA_RUIDO_REPOUSO_MG)
    {
        return 0U;
    }
    if (mediana_mg >= LIMIAR_ATUAL_MG)
    {
        return BARRA_LARGURA;
    }

    return (uint8_t)(
        ((mediana_mg - BARRA_RUIDO_REPOUSO_MG) * BARRA_LARGURA +
         (LIMIAR_ATUAL_MG - BARRA_RUIDO_REPOUSO_MG) / 2U) /
        (LIMIAR_ATUAL_MG - BARRA_RUIDO_REPOUSO_MG));
}

static uint8_t RaioParaBarra(uint8_t largura)
{
    return (uint8_t)(BALAO_RAIO_MIN +
        ((uint32_t)largura * (BALAO_RAIO_MAX - BALAO_RAIO_MIN) +
         BARRA_LARGURA / 2U) / BARRA_LARGURA);
}

// Calcula a semi-largura de cada faixa horizontal do balao.
// Usa aritmetica inteira para evitar sqrtf durante o desenho.
static uint8_t SemiLargura(uint8_t raio, uint8_t y)
{
    int32_t topo = (int32_t)y - (int32_t)BALAO_CY;
    int32_t fundo = (int32_t)y + (int32_t)BALAO_ALT_FAIXA - 1 - (int32_t)BALAO_CY;
    uint32_t dy_topo = (uint32_t)(topo < 0 ? -topo : topo);
    uint32_t dy_fundo = (uint32_t)(fundo < 0 ? -fundo : fundo);
    uint32_t dy = dy_topo > dy_fundo ? dy_topo : dy_fundo;
    uint32_t r2 = (uint32_t)raio * raio;
    uint8_t metade = raio;

    if ((raio == 0U) || (dy >= raio))
    {
        return 0U;
    }

    while (((uint32_t)metade * metade + dy * dy) > r2)
    {
        metade--;
    }
    return metade;
}

// Cada faixa atualiza so as parcelas que mudaram de tamanho.
// Evita apagar e redesenhar o balao inteiro a cada frame.
static void DesenhaBalao(uint8_t raio_antigo, uint8_t raio_novo)
{
    for (uint8_t faixa = 0U; faixa < BALAO_NUM_FAIXAS; faixa++)
    {
        uint8_t y = (uint8_t)(BALAO_LINHA_INICIAL + faixa * BALAO_ALT_FAIXA);
        uint8_t a = SemiLargura(raio_antigo, y);
        uint8_t b = SemiLargura(raio_novo, y);

        if (a == b)
        {
            continue;
        }

        if (b > a)
        {
            if (a == 0U)
            {
                lcd_draw_fillrect((uint8_t)(BALAO_CX - b), y,
                                  (uint8_t)(2U * b + 1U),
                                  BALAO_ALT_FAIXA, LCD_VERMELHO);
            }
            else
            {
                uint8_t diferenca = (uint8_t)(b - a);
                lcd_draw_fillrect((uint8_t)(BALAO_CX - b), y,
                                  diferenca, BALAO_ALT_FAIXA, LCD_VERMELHO);
                lcd_draw_fillrect((uint8_t)(BALAO_CX + a + 1U), y,
                                  diferenca, BALAO_ALT_FAIXA, LCD_VERMELHO);
            }
        }
        else
        {
            if (b == 0U)
            {
                lcd_draw_fillrect((uint8_t)(BALAO_CX - a), y,
                                  (uint8_t)(2U * a + 1U),
                                  BALAO_ALT_FAIXA, LCD_PRETO);
            }
            else
            {
                uint8_t diferenca = (uint8_t)(a - b);
                lcd_draw_fillrect((uint8_t)(BALAO_CX - a), y,
                                  diferenca, BALAO_ALT_FAIXA, LCD_PRETO);
                lcd_draw_fillrect((uint8_t)(BALAO_CX + b + 1U), y,
                                  diferenca, BALAO_ALT_FAIXA, LCD_PRETO);
            }
        }
    }
}

// Pequeno brilho branco no balao (posicao fixa, sempre dentro do raio
// minimo). Redesenhado depois de cada alteracao da silhueta.
static void DesenhaBrilho(void)
{
    lcd_draw_fillrect((uint8_t)(BALAO_CX - 4U), (uint8_t)(BALAO_CY - 5U),
                      3U, 3U, LCD_BRANCO);
}

// Cor de cada coluna da barra: verde -> amarelo -> vermelho.
static uint16_t CorBarra(uint8_t px)
{
    if (px < BARRA_ZONA_AMARELA)
        return LCD_VERDE;
    if (px < BARRA_ZONA_VERMELHA)
        return LCD_AMARELO;
    return LCD_VERMELHO;
}

// Pinta as colunas [de, ate) da barra, dividindo pelas zonas de cor.
static void PintaBarra(uint8_t de, uint8_t ate)
{
    while (de < ate)
    {
        uint8_t fim_zona;

        if (de < BARRA_ZONA_AMARELA)
            fim_zona = BARRA_ZONA_AMARELA;
        else if (de < BARRA_ZONA_VERMELHA)
            fim_zona = BARRA_ZONA_VERMELHA;
        else
            fim_zona = BARRA_LARGURA;

        if (fim_zona > ate)
            fim_zona = ate;

        lcd_draw_fillrect((uint8_t)(BARRA_X + de), BARRA_Y,
                          (uint8_t)(fim_zona - de), BARRA_ALTURA,
                          CorBarra(de));
        de = fim_zona;
    }
}

// Pedacos do balao espalhados + "POP!" (posicoes fixas, sem rand()).
static void DesenhaExplosao(void)
{
    static const int8_t dx[12] = {-24, 20, -14, 26, -28, 8,
                                  -6, 22, -18, 4, 14, -26};
    static const int8_t dy[12] = {-18, -14, 16, 6, -2, -24,
                                  22, 20, 6, 14, -22, 18};

    for (uint8_t i = 0U; i < 12U; i++)
    {
        uint16_t cor = (i % 3U == 0U) ? LCD_AMARELO :
                       ((i % 3U == 1U) ? LCD_VERMELHO : LCD_LARANJA);
        lcd_draw_fillrect((uint8_t)((int16_t)BALAO_CX + dx[i] - 2),
                          (uint8_t)((int16_t)BALAO_CY + dy[i] - 2),
                          5U, 3U, cor);
    }
    // Tamanho 2: cada caracter ocupa 12 px -> "POP!" = 48 px.
    lcd_draw_string((uint8_t)(BALAO_CX - 23U), (uint8_t)(BALAO_CY - 7U),
                    (unsigned char *)"POP!", LCD_BRANCO, 2U);
}

// Desenha um balao pequeno, com corda e brilho (inicio de cada jogo).
static void BalaoInicial(void)
{
    DesenhaBalao(0U, BALAO_RAIO_MIN);
    DesenhaCorda(0U, BALAO_RAIO_MIN);
    DesenhaBrilho();
    balao_raio_desenhado = BALAO_RAIO_MIN;
}

static void DesenhaCorda(uint8_t raio_antigo, uint8_t raio_novo)
{
    uint8_t comeco_antigo = (uint8_t)(BALAO_CY + raio_antigo + 1U);
    uint8_t comeco_novo = (uint8_t)(BALAO_CY + raio_novo + 1U);

    if ((raio_antigo != 0U) && (comeco_antigo <= BALAO_CORDA_FIM))
    {
        lcd_draw_fillrect((uint8_t)(BALAO_CX - 1U), comeco_antigo,
                          2U, (uint8_t)(BALAO_CORDA_FIM - comeco_antigo + 1U),
                          LCD_PRETO);
    }

    if ((raio_novo != 0U) && (comeco_novo <= BALAO_CORDA_FIM))
    {
        lcd_draw_fillrect((uint8_t)(BALAO_CX - 1U), comeco_novo,
                          2U, (uint8_t)(BALAO_CORDA_FIM - comeco_novo + 1U),
                          LCD_CINZA);
    }
}

static void AtualizaLED(uint8_t ligado)
{
    BitAction nivel = (ligado != 0U) ? Bit_SET : Bit_RESET;
#if LED_ATIVO_ALTO == 0U
    nivel = (ligado != 0U) ? Bit_RESET : Bit_SET;
#endif
    GPIO_WriteBit(GPIOB, GPIO_Pin_1, nivel);
}

static void ConfiguraLED(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    // PB1 - saida para o LED do rebentamento.
    gpio.GPIO_Pin = GPIO_Pin_1;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &gpio);
    AtualizaLED(0U);
}

// =====================================================
// PWM NO LED PB0 (TIM3 CANAL 3)
// =====================================================
// O TIM3 ja conta a 200 Hz (ARR = 49) para disparar o ADC.
// O canal 3 do mesmo timer gera PWM no PB0 com esse periodo:
// duty = CCR3 / 50. O evento de update (TRGO) nao e afetado.
static void PWM_LED_Config(void)
{
    GPIO_InitTypeDef gpio;
    TIM_OCInitTypeDef oc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    // PB0 = TIM3_CH3: funcao alternativa push-pull
    gpio.GPIO_Pin = GPIO_Pin_0;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &gpio);

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;          // ativo enquanto CNT < CCR3
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = 0U;                        // comeca apagado
#if LED_ATIVO_ALTO == 0U
    oc.TIM_OCPolarity = TIM_OCPolarity_Low;
#else
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
#endif
    TIM_OC3Init(TIM3, &oc);
    TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);
}

// Brilho proporcional a barra: 0 px -> 0 %, 104 px -> 100 % (CCR3 = 50).
static void AtualizaPWM(uint8_t barra_px)
{
    TIM_SetCompare3(TIM3, (uint16_t)(((uint32_t)barra_px * 50U) / BARRA_LARGURA));
}

// =====================================================
// BOTAO SW5 (PA1) -> EXTI1
// =====================================================
// Ativo a zero (pull-up): carregar = flanco descendente.
static void Botao_EXTI_Config(void)
{
    GPIO_InitTypeDef gpio;
    EXTI_InitTypeDef EXTI_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_1;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);

    // Ligar o pino PA1 a linha de interrupcao externa EXTI1
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);

    EXTI_InitStructure.EXTI_Line = EXTI_Line1;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
}

// =====================================================
// ECRA DO JOGO
// =====================================================
// Pagina do desafio: cabecalho, cronometro, nivel, barra e balao.
static void DesenhaEcraJogo(void)
{
    lcd_draw_fillrect(0U, 0U, 128U, 160U, LCD_PRETO);
    DesenhaCabecalho();
    MostraCronometro();
    MostraDificuldade();
    MostraRecorde(JOGO_REC_X, 53U);

    // Moldura fixa da barra. Interior: x=12..115, y=73..80.
    lcd_draw_fillrect(10U, 70U, 108U, 1U, LCD_BRANCO);
    lcd_draw_fillrect(10U, 83U, 108U, 1U, LCD_BRANCO);
    lcd_draw_fillrect(10U, 71U, 1U, 12U, LCD_BRANCO);
    lcd_draw_fillrect(117U, 71U, 1U, 12U, LCD_BRANCO);

    // Balao inicial pequeno, com corda e brilho.
    BalaoInicial();
}

// Sai do menu: limpa o estado da animacao e comeca a contagem 3, 2, 1.
static void IniciaJogo(void)
{
    char mensagem[48];

    estado_jogo = JOGO_CONTAGEM;
    blocos_contagem = 0U;
    tempo_jogo_blocos = 0U;
    contador_rebentamento = 0U;
    nivel_vibracao = VIB_BAIXA;
    barra_visual_px = 0U;
    barra_desenhada_px = 0U;
    balao_raio_desenhado = 0U;
    AtualizaLED(0U);
    AtualizaPWM(0U);

    DesenhaEcraJogo();
    MostraEstado("PREPARA... 3", LCD_BRANCO);

    snprintf(mensagem, sizeof(mensagem), "Jogo %s (%lu mg): 3, 2, 1...\r\n",
             nome_dificuldade[dificuldade],
             (unsigned long)LIMIAR_ATUAL_MG);
    USART2_EnviaTexto(mensagem);
}

// Maquina de estados do jogo, chamada em todos os blocos de 160 ms.
//   MENU     --(segurar SW5 1 s, ou 'j')--> CONTAGEM (IniciaJogo, no main)
//   CONTAGEM --(~3 s)-----------------------> ATIVO
//   ATIVO    --(rebenta)--------------------> REBOU --(~2,6 s)--> MENU
//   ATIVO    --(20 s sem rebentar)----------> FIM   --(~2,6 s)--> MENU
// Rebenta quando Med5 >= limiar do nivel em 3 blocos seguidos E a barra e
// o balao ja estao no maximo.
// Nunca bloqueia: o ADC/DMA continuam a adquirir durante todo o jogo.
// Eventos: 0 nenhum; 1 rebentou; 2 voltou ao menu; 3 inicio; 4 tempo esgotado.
static uint8_t AtualizaJogo(uint32_t rms_mediana_mg)
{
    switch (estado_jogo)
    {
    case JOGO_MENU:
        // O menu so muda com o botao ou com comandos (tratados no main).
        return 0U;

    case JOGO_CONTAGEM:
        blocos_contagem++;
        if (blocos_contagem >= CONTAGEM_BLOCOS)
        {
            estado_jogo = JOGO_ATIVO;
            MostraEstado("VAI! ABANA!", LCD_LARANJA);
            return 3U;
        }
        if ((blocos_contagem % CONTAGEM_PASSO) == 0U)
        {
            // 6 blocos -> "2", 12 blocos -> "1"
            MostraEstado((blocos_contagem < 2U * CONTAGEM_PASSO) ?
                         "PREPARA... 2" : "PREPARA... 1", LCD_BRANCO);
        }
        return 0U;

    case JOGO_ATIVO:
        tempo_jogo_blocos++;
        MostraCronometro();

        if (tempo_jogo_blocos >= JOGO_LIMITE_BLOCOS)
        {
            estado_jogo = JOGO_FIM;
            blocos_rebou = 0U;
            AtualizaPWM(0U);
            MostraEstado("TEMPO ESGOTADO", LCD_VERMELHO);
            return 4U;
        }

        if (rms_mediana_mg >= LIMIAR_ATUAL_MG)
        {
            if (contador_rebentamento < REBENTAMENTO_BLOCOS)
            {
                contador_rebentamento++;
            }
        }
        else
        {
            contador_rebentamento = 0U;
        }

        if ((contador_rebentamento >= REBENTAMENTO_BLOCOS) &&
            (barra_visual_px == BARRA_LARGURA) &&
            (barra_desenhada_px == BARRA_LARGURA) &&
            (balao_raio_desenhado == BALAO_RAIO_MAX))
        {
            estado_jogo = JOGO_REBOU;
            blocos_rebou = 0U;
            contador_rebentamento = 0U;
            AtualizaLED(1U);

            // Apagar balao e barra, desenhar a explosao.
            DesenhaCorda(balao_raio_desenhado, 0U);
            DesenhaBalao(balao_raio_desenhado, 0U);
            balao_raio_desenhado = 0U;
            lcd_draw_fillrect(BARRA_X, BARRA_Y,
                              BARRA_LARGURA, BARRA_ALTURA, LCD_PRETO);
            barra_desenhada_px = 0U;
            barra_visual_px = 0U;
            AtualizaPWM(0U);
            DesenhaExplosao();

            if ((recorde_blocos[dificuldade] == 0U) ||
                (tempo_jogo_blocos < recorde_blocos[dificuldade]))
            {
                recorde_blocos[dificuldade] = tempo_jogo_blocos;
                MostraEstado("NOVO RECORDE!", LCD_CIANO);
                MostraRecorde(JOGO_REC_X, 53U);
            }
            else
            {
                MostraEstado("REBOU!", LCD_VERMELHO);
            }
            return 1U;
        }
        return 0U;

    default:
        // JOGO_REBOU ou JOGO_FIM: manter o resultado no ecra durante
        // REBOU_DURACAO_BLOCOS, sem parar a aquisicao. Depois, menu.
        blocos_rebou++;
        if (blocos_rebou >= REBOU_DURACAO_BLOCOS)
        {
            estado_jogo = JOGO_MENU;
            blocos_rebou = 0U;
            AtualizaLED(0U);
            AtualizaPWM(0U);
            DesenhaMenu();
            return 2U;
        }
        return 0U;
    }
}

// Limitar o ritmo de animacao: subida gradual, descida ainda mais lenta.
static uint8_t AtualizaAnimacao(uint32_t rms_mediana_mg, uint32_t bloco)
{
    uint8_t alvo, diferenca, novo_raio;
    uint8_t desenhou = 0U;

    if ((estado_jogo != JOGO_ATIVO) ||
        ((bloco % BARRA_PERIODO_BLOCOS) != 0U))
    {
        return 0U;
    }

    alvo = AlvoBarra(rms_mediana_mg);
    if (alvo > barra_visual_px)
    {
        diferenca = (uint8_t)(alvo - barra_visual_px);
        if (diferenca > BARRA_PASSO_SUBIDA_PX)
        {
            diferenca = BARRA_PASSO_SUBIDA_PX;
        }
        barra_visual_px = (uint8_t)(barra_visual_px + diferenca);
    }
    else if (alvo < barra_visual_px)
    {
        diferenca = (uint8_t)(barra_visual_px - alvo);
        if (diferenca > BARRA_PASSO_DESCIDA_PX)
        {
            diferenca = BARRA_PASSO_DESCIDA_PX;
        }
        barra_visual_px = (uint8_t)(barra_visual_px - diferenca);
    }

    // O brilho do LED PB0 segue o valor visual da barra.
    AtualizaPWM(barra_visual_px);

    // A barra so escreve os pixeis que aumentaram ou desapareceram.
    diferenca = (uint8_t)((barra_visual_px >= barra_desenhada_px) ?
        (barra_visual_px - barra_desenhada_px) :
        (barra_desenhada_px - barra_visual_px));

    if ((diferenca >= BARRA_DELTA_MIN_PX) ||
        ((diferenca != 0U) &&
         ((barra_visual_px == BARRA_LARGURA) ||
          (barra_visual_px == 0U))))
    {
        if (barra_visual_px > barra_desenhada_px)
        {
            PintaBarra(barra_desenhada_px, barra_visual_px);
        }
        else
        {
            lcd_draw_fillrect((uint8_t)(BARRA_X + barra_visual_px),
                              BARRA_Y, diferenca, BARRA_ALTURA, LCD_PRETO);
        }
        barra_desenhada_px = barra_visual_px;
        desenhou = 1U;
    }

    // O raio responde ao MESMO valor visual da barra.
    novo_raio = RaioParaBarra(barra_visual_px);
    if (novo_raio != balao_raio_desenhado)
    {
        // A corda e restaurada depois das alteracoes na silhueta.
        DesenhaCorda(balao_raio_desenhado, 0U);
        DesenhaBalao(balao_raio_desenhado, novo_raio);
        DesenhaCorda(0U, novo_raio);
        DesenhaBrilho();
        balao_raio_desenhado = novo_raio;
        desenhou = 1U;
    }

    return desenhou;
}

// O DMA e circular e continua a escrever enquanto o CPU desenha no LCD.
// Copiar rapidamente a metade pronta para RAM local; nunca calcular RMS
// diretamente sobre amostras que podem ser substituidas pelo DMA.
// CNDTR: 192..97 = a escrever primeira metade; 96..1 = segunda metade.
static uint8_t CopiaMetadeDMA(uint8_t metade,
                             uint16_t destino[AMOSTRAS_BLOCO][3])
{
    uint16_t antes = DMA_GetCurrDataCounter(DMA1_Channel1);
    uint16_t depois;

    if (antes == 0U || antes > (2U * ELEMENTOS_MEIO_DMA))
        return 0U;
    if ((metade == 0U && antes > ELEMENTOS_MEIO_DMA) ||
        (metade == 1U && antes <= ELEMENTOS_MEIO_DMA))
        return 0U;

    for (uint32_t i = 0U; i < AMOSTRAS_BLOCO; i++)
    {
        for (uint8_t eixo = 0U; eixo < 3U; eixo++)
            destino[i][eixo] = adc_buffer[metade][i][eixo];
    }

    // Se houve mudanca de metade durante a copia, nao usar os dados.
    depois = DMA_GetCurrDataCounter(DMA1_Channel1);
    if (depois == 0U ||
        (metade == 0U && depois > ELEMENTOS_MEIO_DMA) ||
        (metade == 1U && depois <= ELEMENTOS_MEIO_DMA))
        return 0U;

    return 1U;
}

// Calculo da variancia AC exatamente equivalente a retirar a media:
// RMS^2 = [N*sum(x^2) - (sum(x))^2] / N^2.
// Somatorios inteiros (sem erros de cancelamento em float), tres sqrtf apenas
// para os eixos, mais um para o RMS total.
void MostraBloco(uint8_t metade, uint32_t numero)
{
    uint16_t amostras[AMOSTRAS_BLOCO][3];
    uint32_t soma[3] = {0U, 0U, 0U};
    uint32_t soma_quadrados[3] = {0U, 0U, 0U};
    uint32_t rms_eixo_mg[3];
    float rms_g[3];
    float rms_total_g;
    uint32_t rms_total_mg, rms_mediana_mg;
    uint8_t evento, mudou_nivel = 0U, mudou_animacao = 0U;
    uint8_t desenhou_lcd;
    const char *marca = "";
    char mensagem[128];
    static uint32_t ultimo_aviso_dma = 0U;

    if (CopiaMetadeDMA(metade, amostras) == 0U)
    {
        blocos_dma_descartados++;
        return;
    }

    for (uint32_t i = 0U; i < AMOSTRAS_BLOCO; i++)
    {
        for (uint8_t eixo = 0U; eixo < 3U; eixo++)
        {
            uint32_t a = amostras[i][eixo];
            soma[eixo] += a;
            soma_quadrados[eixo] += a * a;
        }
    }

    for (uint8_t eixo = 0U; eixo < 3U; eixo++)
    {
        uint64_t variacao =
            (uint64_t)AMOSTRAS_BLOCO * soma_quadrados[eixo] -
            (uint64_t)soma[eixo] * soma[eixo];
        float rms_contagens = sqrtf((float)variacao) / (float)AMOSTRAS_BLOCO;

        // Escala nominal: Vref=3.3V, sensibilidade=0.33 V/g.
        rms_g[eixo] = rms_contagens / 409.5f;
        rms_eixo_mg[eixo] = (uint32_t)(rms_g[eixo] * 1000.0f + 0.5f);
    }

    rms_total_g = sqrtf(rms_g[0]*rms_g[0] +
                        rms_g[1]*rms_g[1] + rms_g[2]*rms_g[2]);
    rms_total_mg = (uint32_t)(rms_total_g * 1000.0f + 0.5f);

    // Medicao e interface permanecem separadas.
    rms_mediana_mg = MedianaMovel5(rms_total_mg);
    evento = AtualizaJogo(rms_mediana_mg);
    if (evento == 0U)
    {
        mudou_nivel = AtualizaNivel(rms_mediana_mg);
        mudou_animacao = AtualizaAnimacao(rms_mediana_mg, numero);
    }
    desenhou_lcd = (uint8_t)(mudou_nivel || mudou_animacao);

    if (evento == 1U)       marca = " [REBOU]";
    else if (evento == 2U)  marca = " [MENU]";
    else if (evento == 3U)  marca = " [INICIO]";
    else if (evento == 4U)  marca = " [TEMPO ESGOTADO]";
    else if (desenhou_lcd)  marca = " [LCD]";

    // Menos texto a 9600 bps para libertar a CPU para o LCD/ADC.
    // Nunca esconder os eventos nem a existencia de blocos rejeitados.
    if (((numero % USART_LOG_CADA_BLOCOS) == 0U) ||
        (evento != 0U) || (desenhou_lcd != 0U) ||
        (blocos_dma_descartados != ultimo_aviso_dma))
    {
        if (blocos_dma_descartados != 0U)
        {
            snprintf(mensagem, sizeof(mensagem),
                     "Bloco %lu | RMS: X=%lu Y=%lu Z=%lu Total=%lu Med5=%lu mg%s | DMA_desc=%lu\r\n",
                     (unsigned long)numero,
                     (unsigned long)rms_eixo_mg[0],
                     (unsigned long)rms_eixo_mg[1],
                     (unsigned long)rms_eixo_mg[2],
                     (unsigned long)rms_total_mg,
                     (unsigned long)rms_mediana_mg, marca,
                     (unsigned long)blocos_dma_descartados);
        }
        else
        {
            snprintf(mensagem, sizeof(mensagem),
                     "Bloco %lu | RMS: X=%lu Y=%lu Z=%lu Total=%lu Med5=%lu mg%s\r\n",
                     (unsigned long)numero,
                     (unsigned long)rms_eixo_mg[0],
                     (unsigned long)rms_eixo_mg[1],
                     (unsigned long)rms_eixo_mg[2],
                     (unsigned long)rms_total_mg,
                     (unsigned long)rms_mediana_mg, marca);
        }
        USART2_EnviaTexto(mensagem);
        ultimo_aviso_dma = blocos_dma_descartados;
    }

    // Resultado do jogo no terminal.
    if (evento == 1U)
    {
        uint32_t t = (tempo_jogo_blocos * JOGO_MS_POR_BLOCO) / 100U;
        uint32_t r = (recorde_blocos[dificuldade] * JOGO_MS_POR_BLOCO) / 100U;
        snprintf(mensagem, sizeof(mensagem),
                 ">>> REBENTOU em %lu.%lu s | Recorde %s: %lu.%lu s\r\n",
                 (unsigned long)(t / 10U), (unsigned long)(t % 10U),
                 nome_dificuldade[dificuldade],
                 (unsigned long)(r / 10U), (unsigned long)(r % 10U));
        USART2_EnviaTexto(mensagem);
    }
}

// =====================================================
// BOTAO E COMANDOS (tratados no main, fora das interrupcoes)
// =====================================================

static void MostraAjuda(void)
{
    USART2_EnviaTexto("Comandos (no menu): h ajuda | 1 facil | 2 normal | 3 dificil | j jogar | r apaga recordes\r\n");
    USART2_EnviaTexto("Botao SW5 no menu: clique muda o nivel, segurar 1 s comeca\r\n");
}

static void MudaDificuldade(uint8_t nova)
{
    char mensagem[48];

    dificuldade = nova;
    DesenhaOpcoes();
    snprintf(mensagem, sizeof(mensagem), "Nivel: %s (%lu mg)\r\n",
             nome_dificuldade[dificuldade],
             (unsigned long)LIMIAR_ATUAL_MG);
    USART2_EnviaTexto(mensagem);
}

// SW5 no menu. A interrupcao (EXTI1) so avisa que o botao foi carregado;
// em cada bloco de 160 ms o main le o pino PA1 para saber se ja foi largado.
//   largado antes de 1 s -> clique: nivel seguinte
//   ainda carregado ao fim de 1 s -> comeca o jogo
// Fora do menu o botao e ignorado.
static uint8_t botao_em_espera = 0U;
static uint32_t bloco_inicio_botao = 0U;
static uint32_t ultimo_bloco_botao = 0U;

static void BotaoCarregado(uint32_t numero_bloco)
{
    if ((estado_jogo != JOGO_MENU) || (botao_em_espera != 0U))
    {
        return;
    }
    // Debounce: ignora ressaltos logo a seguir ao ultimo toque.
    if ((numero_bloco - ultimo_bloco_botao) < BOTAO_DEBOUNCE_BLOCOS)
    {
        return;
    }
    botao_em_espera = 1U;
    bloco_inicio_botao = numero_bloco;
}

static void VerificaBotao(uint32_t numero_bloco)
{
    if (botao_em_espera == 0U)
    {
        return;
    }
    if (estado_jogo != JOGO_MENU)
    {
        botao_em_espera = 0U;
        return;
    }

    // Ativo a zero: Bit_SET = botao largado.
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1) == Bit_SET)
    {
        botao_em_espera = 0U;
        ultimo_bloco_botao = numero_bloco;
        MudaDificuldade((uint8_t)((dificuldade + 1U) % NUM_DIFICULDADES));
    }
    else if ((numero_bloco - bloco_inicio_botao) >= BOTAO_SEGURAR_BLOCOS)
    {
        botao_em_espera = 0U;
        ultimo_bloco_botao = numero_bloco;
        IniciaJogo();
    }
}

// Um caracter recebido pela USART2.
static void TrataComando(char c)
{
    if ((c == '\r') || (c == '\n'))
    {
        return;
    }
    if ((c == 'h') || (c == '?'))
    {
        MostraAjuda();
        return;
    }
    if (estado_jogo != JOGO_MENU)
    {
        USART2_EnviaTexto("Comando so no menu (fim do jogo).\r\n");
        return;
    }

    switch (c)
    {
    case 'r':
        for (uint8_t i = 0U; i < NUM_DIFICULDADES; i++)
        {
            recorde_blocos[i] = 0U;
        }
        USART2_EnviaTexto("Recordes apagados.\r\n");
        break;
    case '1':
    case '2':
    case '3':
        MudaDificuldade((uint8_t)(c - '1'));
        break;
    case 'j':
        IniciaJogo();
        break;
    default:
        USART2_EnviaTexto("Comando desconhecido (h = ajuda).\r\n");
        break;
    }
}

int main(void)

{

    uint32_t numero_bloco = 0;

    // ================================================

    // 1. CONFIGURAR O CLOCK DO STM32

    // ================================================

    RCC_Config_HSE_PLL_Max();

    // ================================================

    // 2. INICIALIZAR PRIMEIRO O LCD

    // ================================================

    // Necessario para permitir o remapeamento dos pinos SWJ do LCD

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    lcd_init();

    // Primeira pagina: menu para escolher o nivel.
    DesenhaMenu();

    // ================================================

    // 3. INICIALIZAR OS RESTANTES PERIFERICOS

    // ================================================

    USART2_Config();

    ConfiguraLED();

    ADC1_DMA_Config();

    TIM3_Config_200Hz();

    PWM_LED_Config();      // depois do TIM3: usa o canal 3 do mesmo timer

    Botao_EXTI_Config();

    NVIC_Config();         // antes de ligar o DMA e o TIM3

    // ================================================

    // 4. MENSAGENS INICIAIS

    // ================================================

    USART2_EnviaTexto("\r\nPROJETO SISEM\r\n");

    USART2_EnviaTexto("ADC + DMA (IRQ HT/TC) + TIM3 + LCD\r\n");

    USART2_EnviaTexto("200Hz | Med5 | BAIXA/MEDIA/ALTA | POP=300/500/700mg\r\n");
    USART2_EnviaTexto("DESAFIO DO BALAO: escolhe o nivel, segura SW5 1 s e rebenta em menos de 20 s\r\n");
    USART2_EnviaTexto("LCD e USART: eventos + 1 linha / 2 blocos; DMA_desc=descartes\r\n");
    MostraAjuda();

    // ================================================

    // 5. INICIAR A AQUISICAO

    // ================================================

    DMA_ClearFlag(DMA1_FLAG_GL1);

    DMA_Cmd(DMA1_Channel1, ENABLE);

    TIM_Cmd(TIM3, ENABLE);

    // ================================================

    // 6. PROGRAMA PRINCIPAL

    // ================================================

    while (1)

    {

        uint8_t pendentes;
        uint8_t botao;
        char comando;

        // Ler e limpar as marcas das interrupcoes sem que uma ISR
        // as altere a meio (seccao critica muito curta).
        __disable_irq();
        pendentes = dma_metade_pronta;
        dma_metade_pronta = 0U;
        botao = botao_pressionado;
        botao_pressionado = 0U;
        comando = comando_usart;
        comando_usart = 0;
        __enable_irq();

        // Botao SW5 (EXTI1): marca o inicio do toque (so no menu).
        if (botao != 0U)
        {
            BotaoCarregado(numero_bloco);
        }

        // Comando recebido pelo terminal (USART2 RX).
        if (comando != 0)
        {
            TrataComando(comando);
        }

        // Primeira metade do buffer pronta (interrupcao HT)
        if ((pendentes & 0x01U) != 0U)
        {
            numero_bloco++;
            MostraBloco(0, numero_bloco);
        }

        // Segunda metade do buffer pronta (interrupcao TC)
        if ((pendentes & 0x02U) != 0U)
        {
            numero_bloco++;
            MostraBloco(1, numero_bloco);
        }

        // Uma vez por bloco: clique (largou) ou segurar 1 s (jogar)?
        if (pendentes != 0U)
        {
            VerificaBotao(numero_bloco);
        }

    }

}
