/**
 * Trabalho Final SISEM
 * Juliana Rodrigues 1231182
 * Ema Mota
 *
 * Sistema de Aquisição e Análise de Vibrações
 * VERSÃO JOGO - ADC + DMA (com interrupção) + TIM3 + LCD + DESAFIO DO BALÃO
 * 
 * STM32F103RBT6
 * ADXL337:
 *   X -> PC2 -> ADC1_IN12
 *   Y -> PC1 -> ADC1_IN11
 *   Z -> PC0 -> ADC1_IN10
 *
 * Funcionamento do Jogo:
 *   1) Placa parada: o LCD mostra "ABANA P/ JOGAR" e a leitura do sensor em direto.
 *   2) Ao abanar (Med5 >= 120 mg) ou ao carregar no botão SW5, o jogo arranca diretamente!
 *   3) O objetivo é rebentar o balão (500 mg) no menor tempo possível (máx. 20 s).
 *   4) Quando rebenta, dá-se a explosão "POP!", o LED PB1 acende e o recorde fica guardado em RAM.
 *   5) Após ~2.6 s, volta automaticamente ao ecrã pronto para jogar novamente.
 *
 * Periféricos e Extras implementados:
 *   - ADC1 + DMA1 em modo circular com interrupções HT e TC via NVIC.
 *   - TIM3 a 200 Hz: trigger TRGO para o ADC e PWM no LED PB0 (Canal 3).
 *   - EXTI1: botão SW5 (PA1) com interrupção externa.
 *   - USART2: comunicação série assíncrona a 9600 bps com receção por interrupção (RXNE).
 *   - SPI1: LCD ST7735 com valores em tempo real (mg e g), barra gráfica e balão dinâmico.
 */

#include "stm32f10x.h"
#include "lcd.h"
#include <stdio.h>
#include <math.h>

#define AMOSTRAS_BLOCO 32U
#define ELEMENTOS_MEIO_DMA (AMOSTRAS_BLOCO * 3U)
#define USART_LOG_CADA_BLOCOS 2U // Enviar dados para a USART de 2 em 2 blocos

// Parâmetros da barra no LCD
#define BARRA_X                   12U
#define BARRA_Y                   73U
#define BARRA_LARGURA            104U
#define BARRA_ALTURA               8U
#define BARRA_RUIDO_REPOUSO_MG   35U // ruído com a placa parada
#define BARRA_PERIODO_BLOCOS      2U // atualiza de 2 em 2 blocos (320 ms)
#define BARRA_DELTA_MIN_PX        3U
#define BARRA_PASSO_SUBIDA_PX    12U // subida da barra
#define BARRA_PASSO_DESCIDA_PX    5U // descida da barra

// Parâmetros do balão no LCD
#define BALAO_CX                 64U
#define BALAO_CY                112U
#define BALAO_RAIO_MIN            7U
#define BALAO_RAIO_MAX           22U
#define BALAO_LINHA_INICIAL     90U
#define BALAO_NUM_FAIXAS        11U
#define BALAO_ALT_FAIXA          4U
#define BALAO_CORDA_FIM        154U

// Limiares de vibração em mg
#define NIVEL_MEDIA_ENTRA_MG    120U
#define NIVEL_MEDIA_SAI_MG       95U
#define REARME_AUTOMATICO_MG     95U // rearme ao pousar a placa
#define NIVEL_ALTA_ENTRA_MG     260U
#define NIVEL_ALTA_SAI_MG       230U

// Condições de rebentamento do balão
#define REBENTAMENTO_BLOCOS       3U // blocos seguidos acima do limiar
#define REBOU_DURACAO_BLOCOS     16U // tempo de espera após rebentar (~2.6 s)

#define LED_ATIVO_ALTO            1U // 1 = ativo a nível alto

// Cores em RGB565 (com ajuste BGR para o ecrã da placa 3SO)
#define LCD_ORDEM_BGR 1U

#define LCD_PRETO  0x0000U
#define LCD_BRANCO 0xFFFFU
#define LCD_VERDE  0x07E0U
#define LCD_CINZA  0xC618U
#if LCD_ORDEM_BGR
#define LCD_VERMELHO 0x001FU
#define LCD_AMARELO  0x07FFU
#define LCD_AZUL     0xF800U
#define LCD_CIANO    0xFFE0U
#define LCD_LARANJA  0x053FU
#else
#define LCD_VERMELHO 0xF800U
#define LCD_AMARELO  0xFFE0U
#define LCD_AZUL     0x001FU
#define LCD_CIANO    0x07FFU
#define LCD_LARANJA  0xFD20U
#endif

// Estados do jogo
#define JOGO_ESPERA   0U // parado à espera: "ABANA P/ JOGAR"
#define JOGO_ATIVO    1U // a jogar: cronómetro a contar
#define JOGO_REBOU    2U // balão rebentou (mostra POP!)
#define JOGO_FIM      3U // tempo esgotado (20 segundos)

#define JOGO_LIMITE_BLOCOS  125U // 125 * 160 ms = 20 s
#define JOGO_MS_POR_BLOCO   160U // 32 amostras / 200 Hz = 160 ms
#define JOGO_INICIO_MG      120U // vibração mínima para arrancar o jogo (120 mg)
#define LIMIAR_REBENTAMENTO_MG 500U // limiar fixo de 500 mg para rebentar

// Parâmetros do botão SW5
#define BOTAO_SEGURAR_BLOCOS  6U // 6 x 160 ms = ~1 s para começar o jogo
#define BOTAO_DEBOUNCE_BLOCOS 2U // debounce de 320 ms

// Posições no menu e ecrã
#define MENU_Y0              36U  // primeira opção do menu
#define MENU_PASSO           26U  // espaçamento entre opções
#define MENU_REC_X           31U  // x do recorde no menu
#define MENU_REC_Y          114U // y do recorde no menu
#define MENU_VALOR_Y         128U // y da leitura do sensor no menu
#define MENU_RODAPE_Y        142U // y da mensagem segura SW5

#define JOGO_REC_X           58U  // x do recorde no jogo
#define JOGO_INFO_Y          53U  // y da dificuldade e recorde no jogo
#define JOGO_VALOR_Y         41U  // y do valor em mg e g no jogo
#define ESTADO_Y             29U  // y da mensagem de estado (VAI! ABANA!)
#define TEMPO_Y              19U  // y do cronómetro

// Zonas de cor da barra
#define BARRA_ZONA_AMARELA   40U
#define BARRA_ZONA_VERMELHA  75U

// Buffer do DMA (2 metades x 32 amostras x 3 eixos)
volatile uint16_t adc_buffer[2][AMOSTRAS_BLOCO][3];

// Flags de metades prontas do DMA (bit 0 = HT, bit 1 = TC)
volatile uint8_t dma_metade_pronta = 0U;
static uint32_t blocos_dma_descartados = 0U;

// =====================================================
// Configuração do Clock: HSE + PLL para 72 MHz
// =====================================================
void RCC_Config_HSE_PLL_Max(void) {

    RCC_DeInit();

    RCC_HSEConfig(RCC_HSE_ON); // ativar o HSE

    ErrorStatus HSEStartUpStatus;
    HSEStartUpStatus = RCC_WaitForHSEStartUp();
    if (HSEStartUpStatus == SUCCESS) /* devolve SUCCESS/ERROR */
    {
        FLASH_SetLatency(FLASH_Latency_2); // freq é 72 MHz
        FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);

        RCC_HCLKConfig(RCC_SYSCLK_Div1); // AHB a 72 MHz
        RCC_PCLK1Config(RCC_HCLK_Div2);  // APB1 a 36 MHz (máximo suportado)
        RCC_PCLK2Config(RCC_HCLK_Div1);  // APB2 a 72 MHz

        RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_6); // 12 MHz * 6 = 72 MHz

        RCC_PLLCmd(ENABLE); // ativa-se o PLL
        while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET); // esperar que a PLL arranque

        RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK); // configurar o PLL como clock
        while (RCC_GetSYSCLKSource() != 0x08);     // esperar que estabilize o PLL
    }
    else {
        while (1);
    }
}

// =====================================================
// Configuração da USART2 a 9600 bps com Interrupção de Recepção
// =====================================================
void USART2_Config(void) {

    // Ativar clocks do GPIOA e da USART2
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;

    // Configurar pino PA2 (TX)
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP; // saída alternativa Push-Pull
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // Configurar pino PA3 (RX)
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; // entrada flutuante
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // Parâmetros da USART2: 9600 bps, 8 bits, 1 stop bit, sem paridade
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate            = 9600;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE); // ativar interrupção de recepção (RXNE)

    USART_Cmd(USART2, ENABLE); // ativar a USART2
}

// Envia uma string de texto pela USART2
void USART2_EnviaTexto(const char *texto) {

    while (*texto != '\0') {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET); // esperar por TX vazio
        USART_SendData(USART2, (uint16_t)*texto); // enviar caracter
        texto++;
    }
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET); // esperar pelo fim da transmissão
}

// =====================================================
// Configuração do ADC1 com DMA1 e trigger no TIM3
// =====================================================
void ADC1_DMA_Config(void) {

    // Ativar clocks do GPIOC, ADC1 e DMA1
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_ADC1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    RCC_ADCCLKConfig(RCC_PCLK2_Div6); // 72 MHz / 6 = 12 MHz

    // Configurar pinos PC0, PC1 e PC2 como entradas analógicas
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AIN; // entrada analógica
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // Configuração do Canal 1 do DMA1
    DMA_DeInit(DMA1_Channel1);
    DMA_InitTypeDef DMA_InitStructure;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR; // registo de dados do ADC1
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)&adc_buffer[0][0][0]; // buffer na memória RAM
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC; // periférico é a fonte
    DMA_InitStructure.DMA_BufferSize         = 2 * AMOSTRAS_BLOCO * 3; // 2 metades x 32 amostras x 3 canais
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable; // não incrementa periférico
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable; // incrementa endereço de memória
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord; // 16 bits
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord; // 16 bits
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Circular; // modo circular
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High; // prioridade alta
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);

    DMA_ITConfig(DMA1_Channel1, DMA_IT_HT | DMA_IT_TC, ENABLE); // ativar interrupções HT e TC

    // Configuração do ADC1
    ADC_DeInit(ADC1);
    ADC_InitTypeDef ADC_InitStructure;
    ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent; // modo independente
    ADC_InitStructure.ADC_ScanConvMode       = ENABLE; // modo scan
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE; // conversão não contínua
    ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_T3_TRGO; // trigger externo pelo TIM3 (TRGO)
    ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right; // alinhamento à direita
    ADC_InitStructure.ADC_NbrOfChannel       = 3; // 3 canais
    ADC_Init(ADC1, &ADC_InitStructure);

    // Sequência dos canais: X (CH12), Y (CH11), Z (CH10)
    ADC_RegularChannelConfig(ADC1, ADC_Channel_12, 1, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_11, 2, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_10, 3, ADC_SampleTime_239Cycles5);

    ADC_DMACmd(ADC1, ENABLE); // ligar DMA ao ADC1
    ADC_Cmd(ADC1, ENABLE);    // ativar o ADC1

    // Calibração do ADC1
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1)); // esperar reset da calibração

    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1)); // esperar fim da calibração

    ADC_ExternalTrigConvCmd(ADC1, ENABLE); // ativar trigger externo
}

// =====================================================
// Configuração das Interrupções no NVIC
// =====================================================
void NVIC_Config(void) {

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1); // 1 bit de preempção, 3 de sub-prioridade

    NVIC_InitTypeDef NVIC_InitStructure;

    // Interrupção do DMA1 Canal 1 (máxima prioridade)
    NVIC_InitStructure.NVIC_IRQChannel                   = DMA1_Channel1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // Interrupção do Botão SW5 (EXTI1)
    NVIC_InitStructure.NVIC_IRQChannel                   = EXTI1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // Interrupção da USART2 (recepção)
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

// =====================================================
// Configuração do TIM3 para gerar 200 Hz
// =====================================================
void TIM3_Config_200Hz(void) {

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE); // ativar clock do TIM3

    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_TimeBaseStructure.TIM_Prescaler     = 7200 - 1; // 72 MHz / 7200 = 10 kHz
    TIM_TimeBaseStructure.TIM_Period        = 50 - 1;   // 10 kHz / 50 = 200 Hz
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up; // contagem crescente
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update); // update gera sinal TRGO
}

// Filtro de mediana móvel dos últimos 5 blocos
static uint32_t MedianaMovel5(uint32_t rms_atual_mg) {

    static uint32_t historico[5];
    static uint8_t posicao = 0U;
    static uint8_t inicializado = 0U;
    uint32_t ordenado[5];

    if (inicializado == 0U) {
        for (uint8_t i = 0U; i < 5U; i++) {
            historico[i] = rms_atual_mg;
        }
        inicializado = 1U;
    }

    historico[posicao] = rms_atual_mg;
    posicao = (uint8_t)((posicao + 1U) % 5U);

    for (uint8_t i = 0U; i < 5U; i++) {
        ordenado[i] = historico[i];
    }

    // Ordenação por inserção
    for (uint8_t i = 1U; i < 5U; i++) {
        uint32_t valor = ordenado[i];
        uint8_t j = i;

        while ((j > 0U) && (ordenado[j - 1U] > valor)) {
            ordenado[j] = ordenado[j - 1U];
            j--;
        }
        ordenado[j] = valor;
    }

    return ordenado[2]; // devolve o valor do meio
}

// Variáveis da animação e do jogo
static void DesenhaCorda(uint8_t raio_antigo, uint8_t raio_novo);

static uint8_t barra_visual_px = 0U;
static uint8_t barra_desenhada_px = 0U;
static uint8_t balao_raio_desenhado = 0U;
static uint8_t contador_rebentamento = 0U;
static uint8_t blocos_rebou = 0U;

static uint8_t estado_jogo = JOGO_ESPERA;
static uint32_t tempo_jogo_blocos = 0U;
static uint32_t recorde_blocos = 0U; // melhor tempo em blocos (0 = nenhum)
static uint8_t pode_rebentar = 1U;

// Flags das interrupções
volatile uint8_t botao_pressionado = 0U; // botão SW5 (EXTI1)
volatile char comando_usart = 0;         // caracter recebido na USART2

#define VIB_BAIXA 0U
#define VIB_MEDIA 1U
#define VIB_ALTA  2U
static uint8_t nivel_vibracao = VIB_BAIXA;

// Escrever mensagem de estado no ecrã
static void MostraEstado(const char *texto, uint16_t cor) {

    uint8_t n = 0U;
    while ((texto[n] != '\0') && (n < 18U)) {
        n++;
    }
    lcd_draw_fillrect(10U, (uint8_t)(ESTADO_Y - 1U), 108U, 10U, LCD_PRETO); // limpa a linha
    lcd_draw_string((uint8_t)(64U - n * 3U), ESTADO_Y, (unsigned char *)texto, cor, 1U);
}

// Mostrar cronómetro no ecrã
static void MostraCronometro(void) {

    char texto[20];
    uint32_t decimas = (tempo_jogo_blocos * JOGO_MS_POR_BLOCO) / 100U;

    snprintf(texto, sizeof(texto), "TEMPO: %02lu.%lu s",
             (unsigned long)(decimas / 10U),
             (unsigned long)(decimas % 10U));
    lcd_draw_string(25U, TEMPO_Y, (unsigned char *)texto, LCD_BRANCO, 1U);
}

// Devolve o comprimento de uma string
static uint8_t Comprimento(const char *texto) {

    uint8_t n = 0U;
    while (texto[n] != '\0') {
        n++;
    }
    return n;
}

// Mostrar limiar e recorde no ecrã
static void MostraRecorde(void) {

    char texto[20];
    uint32_t decimas = (recorde_blocos * JOGO_MS_POR_BLOCO) / 100U;

    lcd_draw_string(10U, JOGO_INFO_Y, (unsigned char *)"LIM: 500mg", LCD_CINZA, 1U);

    if (recorde_blocos == 0U) {
        snprintf(texto, sizeof(texto), "REC: --.- s");
    }
    else {
        snprintf(texto, sizeof(texto), "REC: %02lu.%lu s",
                 (unsigned long)(decimas / 10U),
                 (unsigned long)(decimas % 10U));
    }
    lcd_draw_string(62U, JOGO_INFO_Y, (unsigned char *)texto, LCD_CIANO, 1U);
}

// Desenhar cabeçalho comum a todas as páginas
static void DesenhaCabecalho(void) {
    lcd_draw_string(16U, 6U, (unsigned char *)"DESAFIO DO BALAO", LCD_AMARELO, 1U);
    lcd_draw_fillrect(10U, 15U, 108U, 1U, LCD_LARANJA);
}

// Cor da aceleração de acordo com o nível
static uint16_t CorPorVibracao(uint32_t valor_mg) {

    if (valor_mg >= NIVEL_ALTA_ENTRA_MG)  return LCD_VERMELHO; // nível alto (>260 mg)
    if (valor_mg >= NIVEL_MEDIA_ENTRA_MG) return LCD_AMARELO;  // nível médio (>120 mg)
    return LCD_VERDE;                                          // repouso / baixo
}

// Mostrar valor da aceleração em mg e g durante o jogo
static void MostraValorJogo(uint32_t valor_mg) {

    char texto[32];
    uint8_t n;
    uint32_t int_g = valor_mg / 1000U;
    uint32_t dec_g = (valor_mg % 1000U) / 10U; // centésimas de g
    uint16_t cor   = CorPorVibracao(valor_mg);

    snprintf(texto, sizeof(texto), "VIB: %lu mg (%lu.%02lu g)",
             (unsigned long)valor_mg,
             (unsigned long)int_g,
             (unsigned long)dec_g);

    n = Comprimento(texto);
    lcd_draw_fillrect(2U, (uint8_t)(JOGO_VALOR_Y - 1U), 124U, 10U, LCD_PRETO); // limpa a linha
    lcd_draw_string((uint8_t)((128U - n * 6U) / 2U), JOGO_VALOR_Y, (unsigned char *)texto, cor, 1U); // centra o texto
}

// Mostrar nível de vibração no ecrã
static void MostraNivelNoLCD(uint8_t nivel) {

    const char *texto;
    uint16_t cor;

    if (nivel == VIB_ALTA) {
        texto = "VIB: ALTA";
        cor   = LCD_VERMELHO;
    }
    else if (nivel == VIB_MEDIA) {
        texto = "VIB: MEDIA";
        cor   = LCD_AMARELO;
    }
    else {
        texto = "VIB: BAIXA";
        cor   = LCD_VERDE;
    }

    MostraEstado(texto, cor);
}

// Atualizar o nível de vibração
static uint8_t AtualizaNivel(uint32_t mediana_mg) {

    uint8_t novo = nivel_vibracao;

    if ((estado_jogo != JOGO_ATIVO) || (tempo_jogo_blocos < 6U)) {
        return 0U;
    }

    if (nivel_vibracao == VIB_BAIXA) {
        if (mediana_mg >= NIVEL_ALTA_ENTRA_MG)       novo = VIB_ALTA;
        else if (mediana_mg >= NIVEL_MEDIA_ENTRA_MG) novo = VIB_MEDIA;
    }
    else if (nivel_vibracao == VIB_MEDIA) {
        if (mediana_mg >= NIVEL_ALTA_ENTRA_MG)       novo = VIB_ALTA;
        else if (mediana_mg <= NIVEL_MEDIA_SAI_MG)    novo = VIB_BAIXA;
    }
    else {
        if (mediana_mg <= NIVEL_MEDIA_SAI_MG)        novo = VIB_BAIXA;
        else if (mediana_mg <= NIVEL_ALTA_SAI_MG)     novo = VIB_MEDIA;
    }

    if (novo == nivel_vibracao) return 0U;

    nivel_vibracao = novo;
    MostraNivelNoLCD(novo);
    return 1U;
}

// Converter aceleração em pixeis da barra
static uint8_t AlvoBarra(uint32_t mediana_mg) {

    if (mediana_mg <= BARRA_RUIDO_REPOUSO_MG) return 0U;
    if (mediana_mg >= LIMIAR_REBENTAMENTO_MG)        return BARRA_LARGURA;

    return (uint8_t)(
        ((mediana_mg - BARRA_RUIDO_REPOUSO_MG) * BARRA_LARGURA +
         (LIMIAR_REBENTAMENTO_MG - BARRA_RUIDO_REPOUSO_MG) / 2U) /
        (LIMIAR_REBENTAMENTO_MG - BARRA_RUIDO_REPOUSO_MG));
}

// Converter largura da barra em raio do balão
static uint8_t RaioParaBarra(uint8_t largura) {

    return (uint8_t)(BALAO_RAIO_MIN +
        ((uint32_t)largura * (BALAO_RAIO_MAX - BALAO_RAIO_MIN) +
         BARRA_LARGURA / 2U) / BARRA_LARGURA);
}

// Calcular largura de cada faixa horizontal do balão
static uint8_t SemiLargura(uint8_t raio, uint8_t y) {

    int32_t topo     = (int32_t)y - (int32_t)BALAO_CY;
    int32_t fundo    = (int32_t)y + (int32_t)BALAO_ALT_FAIXA - 1 - (int32_t)BALAO_CY;
    uint32_t dy_topo  = (uint32_t)(topo < 0 ? -topo : topo);
    uint32_t dy_fundo = (uint32_t)(fundo < 0 ? -fundo : fundo);
    uint32_t dy       = dy_topo > dy_fundo ? dy_topo : dy_fundo;
    uint32_t r2       = (uint32_t)raio * raio;
    uint8_t metade   = raio;

    if ((raio == 0U) || (dy >= raio)) return 0U;

    while (((uint32_t)metade * metade + dy * dy) > r2) {
        metade--;
    }
    return metade;
}

// Desenhar o balão no LCD
static void DesenhaBalao(uint8_t raio_antigo, uint8_t raio_novo) {

    for (uint8_t faixa = 0U; faixa < BALAO_NUM_FAIXAS; faixa++) {
        uint8_t y = (uint8_t)(BALAO_LINHA_INICIAL + faixa * BALAO_ALT_FAIXA);
        uint8_t a = SemiLargura(raio_antigo, y);
        uint8_t b = SemiLargura(raio_novo, y);

        if (a == b) continue;

        if (b > a) {
            if (a == 0U) {
                lcd_draw_fillrect((uint8_t)(BALAO_CX - b), y, (uint8_t)(2U * b + 1U), BALAO_ALT_FAIXA, LCD_VERMELHO);
            }
            else {
                uint8_t diferenca = (uint8_t)(b - a);
                lcd_draw_fillrect((uint8_t)(BALAO_CX - b), y, diferenca, BALAO_ALT_FAIXA, LCD_VERMELHO);
                lcd_draw_fillrect((uint8_t)(BALAO_CX + a + 1U), y, diferenca, BALAO_ALT_FAIXA, LCD_VERMELHO);
            }
        }
        else {
            if (b == 0U) {
                lcd_draw_fillrect((uint8_t)(BALAO_CX - a), y, (uint8_t)(2U * a + 1U), BALAO_ALT_FAIXA, LCD_PRETO);
            }
            else {
                uint8_t diferenca = (uint8_t)(a - b);
                lcd_draw_fillrect((uint8_t)(BALAO_CX - a), y, diferenca, BALAO_ALT_FAIXA, LCD_PRETO);
                lcd_draw_fillrect((uint8_t)(BALAO_CX + b + 1U), y, diferenca, BALAO_ALT_FAIXA, LCD_PRETO);
            }
        }
    }
}

// Desenhar o pequeno brilho branco no balão
static void DesenhaBrilho(void) {
    lcd_draw_fillrect((uint8_t)(BALAO_CX - 4U), (uint8_t)(BALAO_CY - 5U), 3U, 3U, LCD_BRANCO);
}

// Cor da barra de acordo com a posição
static uint16_t CorBarra(uint8_t px) {
    if (px < BARRA_ZONA_AMARELA)  return LCD_VERDE;
    if (px < BARRA_ZONA_VERMELHA) return LCD_AMARELO;
    return LCD_VERMELHO;
}

// Preencher colunas da barra
static void PintaBarra(uint8_t de, uint8_t ate) {

    while (de < ate) {
        uint8_t fim_zona;

        if (de < BARRA_ZONA_AMARELA)       fim_zona = BARRA_ZONA_AMARELA;
        else if (de < BARRA_ZONA_VERMELHA) fim_zona = BARRA_ZONA_VERMELHA;
        else                               fim_zona = BARRA_LARGURA;

        if (fim_zona > ate) fim_zona = ate;

        lcd_draw_fillrect((uint8_t)(BARRA_X + de), BARRA_Y, (uint8_t)(fim_zona - de), BARRA_ALTURA, CorBarra(de));
        de = fim_zona;
    }
}

// Desenhar explosão ("POP!")
static void DesenhaExplosao(void) {

    static const int8_t dx[12] = {-24, 20, -14, 26, -28, 8, -6, 22, -18, 4, 14, -26};
    static const int8_t dy[12] = {-18, -14, 16, 6, -2, -24, 22, 20, 6, 14, -22, 18};

    for (uint8_t i = 0U; i < 12U; i++) {
        uint16_t cor = (i % 3U == 0U) ? LCD_AMARELO :
                       ((i % 3U == 1U) ? LCD_VERMELHO : LCD_LARANJA);
        lcd_draw_fillrect((uint8_t)((int16_t)BALAO_CX + dx[i] - 2),
                          (uint8_t)((int16_t)BALAO_CY + dy[i] - 2), 5U, 3U, cor);
    }
    lcd_draw_string((uint8_t)(BALAO_CX - 23U), (uint8_t)(BALAO_CY - 7U), (unsigned char *)"POP!", LCD_BRANCO, 2U);
}

// Desenhar balão inicial pequeno
static void BalaoInicial(void) {

    DesenhaBalao(0U, BALAO_RAIO_MIN);
    DesenhaCorda(0U, BALAO_RAIO_MIN);
    DesenhaBrilho();
    balao_raio_desenhado = BALAO_RAIO_MIN;
}

// Desenhar ou apagar a corda do balão
static void DesenhaCorda(uint8_t raio_antigo, uint8_t raio_novo) {

    uint8_t comeco_antigo = (uint8_t)(BALAO_CY + raio_antigo + 1U);
    uint8_t comeco_novo   = (uint8_t)(BALAO_CY + raio_novo + 1U);

    if ((raio_antigo != 0U) && (comeco_antigo <= BALAO_CORDA_FIM)) {
        lcd_draw_fillrect((uint8_t)(BALAO_CX - 1U), comeco_antigo, 2U,
                          (uint8_t)(BALAO_CORDA_FIM - comeco_antigo + 1U), LCD_PRETO);
    }

    if ((raio_novo != 0U) && (comeco_novo <= BALAO_CORDA_FIM)) {
        lcd_draw_fillrect((uint8_t)(BALAO_CX - 1U), comeco_novo, 2U,
                          (uint8_t)(BALAO_CORDA_FIM - comeco_novo + 1U), LCD_CINZA);
    }
}

// Atualizar o LED PB1
static void AtualizaLED(uint8_t ligado) {

    BitAction nivel = (ligado != 0U) ? Bit_SET : Bit_RESET;
#if LED_ATIVO_ALTO == 0U
    nivel = (ligado != 0U) ? Bit_RESET : Bit_SET;
#endif
    GPIO_WriteBit(GPIOB, GPIO_Pin_1, nivel);
}

// Configuração do LED PB1
static void ConfiguraLED(void) {

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); // ativar clock do GPIOB

    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = GPIO_Pin_1;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP; // saída Push-Pull
    GPIO_Init(GPIOB, &gpio);

    AtualizaLED(0U); // começa apagado
}

// =====================================================
// Configuração do PWM no pino PB0 (TIM3 Canal 3)
// =====================================================
static void PWM_LED_Config(void) {

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); // ativar clock do GPIOB

    // PB0 = TIM3_CH3
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = GPIO_Pin_0;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP; // saída alternativa Push-Pull
    GPIO_Init(GPIOB, &gpio);

    // Configuração do canal 3 em PWM
    TIM_OCInitTypeDef oc;
    TIM_OCStructInit(&oc);
    oc.TIM_OCMode      = TIM_OCMode_PWM1; // modo PWM 1
    oc.TIM_OutputState = TIM_OutputState_Enable; // ativar saída
    oc.TIM_Pulse       = 0U; // começa com duty-cycle a 0%
#if LED_ATIVO_ALTO == 0U
    oc.TIM_OCPolarity  = TIM_OCPolarity_Low;
#else
    oc.TIM_OCPolarity  = TIM_OCPolarity_High;
#endif
    TIM_OC3Init(TIM3, &oc);
    TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);
}

// Atualizar o duty-cycle do LED PB0 de acordo com a barra
static void AtualizaPWM(uint8_t barra_px) {
    TIM_SetCompare3(TIM3, (uint16_t)(((uint32_t)barra_px * 50U) / BARRA_LARGURA));
}

// =====================================================
// Configuração do Botão SW5 (PA1) com EXTI1
// =====================================================
static void Botao_EXTI_Config(void) {

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE); // ativar GPIOA e AFIO

    // PA1 como entrada com pull-up
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = GPIO_Pin_1;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode  = GPIO_Mode_IPU; // entrada com pull-up
    GPIO_Init(GPIOA, &gpio);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1); // ligar PA1 à linha EXTI1

    EXTI_InitTypeDef EXTI_InitStructure;
    EXTI_InitStructure.EXTI_Line    = EXTI_Line1;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt; // modo interrupção
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling; // flanco descendente (ao carregar)
    EXTI_InitStructure.EXTI_LineCmd = ENABLE; // ativar a linha EXTI1
    EXTI_Init(&EXTI_InitStructure);
}

// Desenhar o ecrã do jogo (ecrã único)
static void DesenhaEcraJogo(void) {

    lcd_draw_fillrect(0U, 0U, 128U, 160U, LCD_PRETO);
    DesenhaCabecalho();
    MostraCronometro();
    MostraEstado("ABANA P/ JOGAR", LCD_AMARELO);
    MostraValorJogo(0U);
    MostraRecorde();

    // Moldura da barra
    lcd_draw_fillrect(10U, 70U, 108U, 1U, LCD_BRANCO);
    lcd_draw_fillrect(10U, 83U, 108U, 1U, LCD_BRANCO);
    lcd_draw_fillrect(10U, 71U, 1U, 12U, LCD_BRANCO);
    lcd_draw_fillrect(117U, 71U, 1U, 12U, LCD_BRANCO);

    BalaoInicial(); // desenha o balão inicial
}

// Iniciar o jogo
static void IniciaJogo(void) {

    estado_jogo = JOGO_ATIVO;
    tempo_jogo_blocos = 0U;
    contador_rebentamento = 0U;
    nivel_vibracao = VIB_BAIXA;
    barra_visual_px = 0U;
    barra_desenhada_px = 0U;
    balao_raio_desenhado = 0U;
    AtualizaLED(0U);
    AtualizaPWM(0U);

    MostraCronometro();
    MostraEstado("VAI! ABANA!", LCD_LARANJA);
    USART2_EnviaTexto("Inicio do jogo: VAI! ABANA!\r\n");
}

// Máquina de estados do jogo
static uint8_t AtualizaJogo(uint32_t rms_mediana_mg) {

    if ((pode_rebentar == 0U) && (rms_mediana_mg <= REARME_AUTOMATICO_MG)) {
        pode_rebentar = 1U;
    }

    switch (estado_jogo) {
    case JOGO_ESPERA:
        // Começa ao abanar com força (>= 120 mg) ou se carregar no botão SW5
        if (((pode_rebentar != 0U) && (rms_mediana_mg >= JOGO_INICIO_MG)) ||
            (botao_pressionado != 0U))
        {
            botao_pressionado = 0U;
            IniciaJogo();
            return 3U; // início
        }
        return 0U;

    case JOGO_ATIVO:
        tempo_jogo_blocos++;
        MostraCronometro();

        if (tempo_jogo_blocos >= JOGO_LIMITE_BLOCOS) {
            estado_jogo = JOGO_FIM;
            blocos_rebou = 0U;
            pode_rebentar = 0U;
            AtualizaPWM(0U);
            MostraEstado("TEMPO ESGOTADO", LCD_VERMELHO);
            USART2_EnviaTexto("Fim do jogo: Tempo esgotado (20 s)!\r\n");
            return 4U; // tempo esgotado
        }

        if (rms_mediana_mg >= LIMIAR_REBENTAMENTO_MG) {
            if (contador_rebentamento < REBENTAMENTO_BLOCOS) {
                contador_rebentamento++;
            }
        }
        else {
            contador_rebentamento = 0U;
        }

        if ((contador_rebentamento >= REBENTAMENTO_BLOCOS) &&
            (barra_visual_px == BARRA_LARGURA) &&
            (barra_desenhada_px == BARRA_LARGURA) &&
            (balao_raio_desenhado == BALAO_RAIO_MAX))
        {
            char msg[48];
            estado_jogo = JOGO_REBOU;
            blocos_rebou = 0U;
            pode_rebentar = 0U;
            contador_rebentamento = 0U;
            AtualizaLED(1U); // acende LED PB1

            DesenhaCorda(balao_raio_desenhado, 0U);
            DesenhaBalao(balao_raio_desenhado, 0U);
            balao_raio_desenhado = 0U;
            lcd_draw_fillrect(BARRA_X, BARRA_Y, BARRA_LARGURA, BARRA_ALTURA, LCD_PRETO);
            barra_desenhada_px = 0U;
            barra_visual_px = 0U;
            AtualizaPWM(0U);
            DesenhaExplosao();

            if ((recorde_blocos == 0U) || (tempo_jogo_blocos < recorde_blocos)) {
                recorde_blocos = tempo_jogo_blocos;
                MostraEstado("NOVO RECORDE!", LCD_CIANO);
                MostraRecorde();
                snprintf(msg, sizeof(msg), "NOVO RECORDE: %02lu.%lu s!\r\n",
                         (unsigned long)((tempo_jogo_blocos * JOGO_MS_POR_BLOCO) / 1000U),
                         (unsigned long)(((tempo_jogo_blocos * JOGO_MS_POR_BLOCO) % 1000U) / 100U));
            }
            else {
                MostraEstado("REBOU!", LCD_VERMELHO);
                snprintf(msg, sizeof(msg), "Rebentou em %02lu.%lu s!\r\n",
                         (unsigned long)((tempo_jogo_blocos * JOGO_MS_POR_BLOCO) / 1000U),
                         (unsigned long)(((tempo_jogo_blocos * JOGO_MS_POR_BLOCO) % 1000U) / 100U));
            }
            USART2_EnviaTexto(msg);
            return 1U; // rebentou
        }
        return 0U;

    default: // JOGO_REBOU ou JOGO_FIM
        blocos_rebou++;
        if (blocos_rebou >= REBOU_DURACAO_BLOCOS) {
            estado_jogo = JOGO_ESPERA;
            blocos_rebou = 0U;
            tempo_jogo_blocos = 0U;
            AtualizaLED(0U); // apaga LED PB1
            AtualizaPWM(0U);

            // Limpa zona da explosão e redesenha balão inicial
            lcd_draw_fillrect(0U, 90U, 128U, 70U, LCD_PRETO);
            BalaoInicial();
            MostraCronometro();
            MostraEstado("ABANA P/ JOGAR", LCD_AMARELO);
            return 2U; // voltou à espera
        }
        return 0U;
    }
}

// Atualizar animação da barra e do balão
static uint8_t AtualizaAnimacao(uint32_t rms_mediana_mg, uint32_t bloco) {

    uint8_t alvo, diferenca, novo_raio;
    uint8_t desenhou = 0U;

    if ((estado_jogo != JOGO_ATIVO) || ((bloco % BARRA_PERIODO_BLOCOS) != 0U)) {
        return 0U;
    }

    MostraValorJogo(rms_mediana_mg); // atualiza o valor numérico em mg e g

    alvo = AlvoBarra(rms_mediana_mg);
    if (alvo > barra_visual_px) {
        diferenca = (uint8_t)(alvo - barra_visual_px);
        if (diferenca > BARRA_PASSO_SUBIDA_PX) {
            diferenca = BARRA_PASSO_SUBIDA_PX;
        }
        barra_visual_px = (uint8_t)(barra_visual_px + diferenca);
    }
    else if (alvo < barra_visual_px) {
        diferenca = (uint8_t)(barra_visual_px - alvo);
        if (diferenca > BARRA_PASSO_DESCIDA_PX) {
            diferenca = BARRA_PASSO_DESCIDA_PX;
        }
        barra_visual_px = (uint8_t)(barra_visual_px - diferenca);
    }

    AtualizaPWM(barra_visual_px); // atualiza o brilho do LED

    diferenca = (uint8_t)((barra_visual_px >= barra_desenhada_px) ?
        (barra_visual_px - barra_desenhada_px) :
        (barra_desenhada_px - barra_visual_px));

    if ((diferenca >= BARRA_DELTA_MIN_PX) ||
        ((diferenca != 0U) &&
         ((barra_visual_px == BARRA_LARGURA) || (barra_visual_px == 0U))))
    {
        if (barra_visual_px > barra_desenhada_px) {
            PintaBarra(barra_desenhada_px, barra_visual_px);
        }
        else {
            lcd_draw_fillrect((uint8_t)(BARRA_X + barra_visual_px),
                              BARRA_Y, diferenca, BARRA_ALTURA, LCD_PRETO);
        }
        barra_desenhada_px = barra_visual_px;
        desenhou = 1U;
    }

    novo_raio = RaioParaBarra(barra_visual_px);
    if (novo_raio != balao_raio_desenhado) {
        DesenhaCorda(balao_raio_desenhado, 0U);
        DesenhaBalao(balao_raio_desenhado, novo_raio);
        DesenhaCorda(0U, novo_raio);
        DesenhaBrilho();
        balao_raio_desenhado = novo_raio;
        desenhou = 1U;
    }

    return desenhou;
}

// Copiar metade do buffer do DMA
static uint8_t CopiaMetadeDMA(uint8_t metade, uint16_t destino[AMOSTRAS_BLOCO][3]) {

    uint16_t antes = DMA_GetCurrDataCounter(DMA1_Channel1);
    uint16_t depois;

    if (antes == 0U || antes > (2U * ELEMENTOS_MEIO_DMA)) return 0U;
    if ((metade == 0U && antes > ELEMENTOS_MEIO_DMA) ||
        (metade == 1U && antes <= ELEMENTOS_MEIO_DMA)) return 0U;

    for (uint32_t i = 0U; i < AMOSTRAS_BLOCO; i++) {
        for (uint8_t eixo = 0U; eixo < 3U; eixo++)
            destino[i][eixo] = adc_buffer[metade][i][eixo];
    }

    depois = DMA_GetCurrDataCounter(DMA1_Channel1);
    if (depois == 0U ||
        (metade == 0U && depois > ELEMENTOS_MEIO_DMA) ||
        (metade == 1U && depois <= ELEMENTOS_MEIO_DMA)) return 0U;

    return 1U;
}

// Processa o bloco e calcula o RMS
void MostraBloco(uint8_t metade, uint32_t numero) {

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

    if (CopiaMetadeDMA(metade, amostras) == 0U) {
        blocos_dma_descartados++;
        return;
    }

    for (uint32_t i = 0U; i < AMOSTRAS_BLOCO; i++) {
        for (uint8_t eixo = 0U; eixo < 3U; eixo++) {
            uint32_t a = amostras[i][eixo];
            soma[eixo] += a;
            soma_quadrados[eixo] += a * a;
        }
    }

    // RMS por eixo
    for (uint8_t eixo = 0U; eixo < 3U; eixo++) {
        uint64_t variacao =
            (uint64_t)AMOSTRAS_BLOCO * soma_quadrados[eixo] -
            (uint64_t)soma[eixo] * soma[eixo];
        float rms_contagens = sqrtf((float)variacao) / (float)AMOSTRAS_BLOCO;

        rms_g[eixo] = rms_contagens / 409.5f; // sensibilidade 330 mV/g
        rms_eixo_mg[eixo] = (uint32_t)(rms_g[eixo] * 1000.0f + 0.5f);
    }

    rms_total_g  = sqrtf(rms_g[0]*rms_g[0] + rms_g[1]*rms_g[1] + rms_g[2]*rms_g[2]);
    rms_total_mg = (uint32_t)(rms_total_g * 1000.0f + 0.5f);

    rms_mediana_mg = MedianaMovel5(rms_total_mg); // filtro de mediana
    evento = AtualizaJogo(rms_mediana_mg);
    if (evento == 0U) {
        mudou_nivel    = AtualizaNivel(rms_mediana_mg);
        mudou_animacao = AtualizaAnimacao(rms_mediana_mg, numero);
        if ((estado_jogo == JOGO_ESPERA) && ((numero % 2U) == 0U)) {
            MostraValorJogo(rms_mediana_mg); // leitura contínua em espera
        }
    }
    desenhou_lcd = (uint8_t)(mudou_nivel || mudou_animacao);

    if (evento == 1U)       marca = " [REBOU]";
    else if (evento == 2U)  marca = " [MENU]";
    else if (evento == 3U)  marca = " [INICIO]";
    else if (evento == 4U)  marca = " [TEMPO ESGOTADO]";
    else if (desenhou_lcd)  marca = " [LCD]";

    // Enviar dados pela USART2
    if (((numero % USART_LOG_CADA_BLOCOS) == 0U) ||
        (evento != 0U) || (desenhou_lcd != 0U) ||
        (blocos_dma_descartados != ultimo_aviso_dma))
    {
        if (blocos_dma_descartados != 0U) {
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
        else {
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

        if (evento == 1U) {
        uint32_t t = (tempo_jogo_blocos * JOGO_MS_POR_BLOCO) / 100U;
        uint32_t r = (recorde_blocos * JOGO_MS_POR_BLOCO) / 100U;
        snprintf(mensagem, sizeof(mensagem), ">>> REBENTOU em %lu.%lu s | Recorde: %lu.%lu s\r\n",
                 (unsigned long)(t / 10U), (unsigned long)(t % 10U),
                 (unsigned long)(r / 10U), (unsigned long)(r % 10U));
        USART2_EnviaTexto(mensagem);
    }
}

// Tratar caracteres recebidos pela USART2
static void TrataComando(char c) {

    if ((c == '\r') || (c == '\n')) return;

    if ((c == 'h') || (c == '?')) {
        USART2_EnviaTexto("Comandos: j = iniciar jogo | r = apagar recorde | h = ajuda\r\n");
        return;
    }

    switch (c) {
    case 'r':
        recorde_blocos = 0U;
        MostraRecorde();
        USART2_EnviaTexto("Recorde apagado.\r\n");
        break;
    case 'j':
        if (estado_jogo == JOGO_ESPERA) {
            IniciaJogo();
        }
        break;
    default:
        USART2_EnviaTexto("Comando desconhecido (h = ajuda).\r\n");
        break;
    }
}

// =====================================================
// Função Principal (main)
// =====================================================
int main(void) {

    uint32_t numero_bloco = 0;

    // 1. Configurar clock do sistema a 72 MHz
    RCC_Config_HSE_PLL_Max();

    // 2. Inicializar o ecrã LCD
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    lcd_init();

    // Desenhar a interface do jogo diretamente (ecrã único)
    DesenhaEcraJogo();

    // 3. Inicialização dos periféricos
    USART2_Config();      // USART2 com interrupção RX
    ConfiguraLED();       // LED PB1 (rebentamento)
    ADC1_DMA_Config();    // ADC1 e DMA1 com interrupções HT/TC
    TIM3_Config_200Hz();  // Timer 3 a 200 Hz
    PWM_LED_Config();     // PWM no LED PB0 (TIM3_CH3)
    Botao_EXTI_Config();  // Botão SW5 (EXTI1)
    NVIC_Config();        // Configurar as interrupções

    // 4. Mensagens iniciais na USART2
    USART2_EnviaTexto("\r\n========================================\r\n");
    USART2_EnviaTexto("PROJETO SISEM - DESAFIO DO BALAO\r\n");
    USART2_EnviaTexto("ADC + DMA (NVIC) + TIM3 (TRGO/PWM) + LCD\r\n");
    USART2_EnviaTexto("200 Hz | 3 Eixos | Med5 | POP = 500 mg\r\n");
    USART2_EnviaTexto("========================================\r\n");

    // 5. Iniciar o DMA e o TIM3
    DMA_ClearFlag(DMA1_FLAG_GL1);
    DMA_Cmd(DMA1_Channel1, ENABLE); // ativar DMA1
    TIM_Cmd(TIM3, ENABLE);          // ativar TIM3

    // 6. Ciclo infinito
    while (1) {

        uint8_t pendentes;
        uint8_t botao;
        char comando;

        // Desativa interrupções para ler variáveis partilhadas
        __disable_irq();
        pendentes          = dma_metade_pronta;
        dma_metade_pronta  = 0U;
        botao              = botao_pressionado;
        botao_pressionado  = 0U;
        comando            = comando_usart;
        comando_usart      = 0;
        __enable_irq();

        // Botão SW5: se estiver em espera, inicia o jogo
        if (botao != 0U) {
            if (estado_jogo == JOGO_ESPERA) {
                IniciaJogo();
            }
        }

        // Trata comando recebido pela USART2
        if (comando != 0) {
            TrataComando(comando);
        }

        // Processa 1ª metade do buffer DMA (Half Transfer)
        if ((pendentes & 0x01U) != 0U) {
            numero_bloco++;
            MostraBloco(0, numero_bloco);
        }

        // Processa 2ª metade do buffer DMA (Transfer Complete)
        if ((pendentes & 0x02U) != 0U) {
            numero_bloco++;
            MostraBloco(1, numero_bloco);
        }

        
    }
}
