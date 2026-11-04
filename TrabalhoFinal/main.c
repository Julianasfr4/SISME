/**

 * Trabalho Final SISEM

 * Sistema de Aquisicao e Analise de Vibracoes

 *

 * VERSAO FINAL - ADC + DMA + TIM3 + LCD + BALAO

 *

 * STM32F103RBT6

 * ADXL337:

 *   X -> PC2 -> ADC1_IN12

 *   Y -> PC1 -> ADC1_IN11

 *   Z -> PC0 -> ADC1_IN10

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
#define BARRA_RMS_MAX_MG        500U
#define BARRA_RUIDO_REPOUSO_MG   35U
#define BARRA_PERIODO_BLOCOS      4U  // 4 x 160 ms = 640 ms
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

// Rebenta apenas quando a MEDIANA ultrapassa 500 mg em 3 blocos seguidos.
#define LIMIAR_REBENTAMENTO_MG  500U
#define REBENTAMENTO_BLOCOS       3U

// Mostrar o rebentamento durante 8 blocos de 160 ms (~1,28 s).
// Apos esse periodo, o balao regressa automaticamente ao inicio.
#define REBOU_DURACAO_BLOCOS      8U

// Evitar rebentamentos repetidos enquanto a vibracao continua muito alta.
// Um novo rebentamento fica disponivel depois de Med5 descer ate 95 mg.
#define REARME_AUTOMATICO_MG     95U

// PB1: LED do rebentamento. Se for ativo a zero, usar 0U.
#define LED_ATIVO_ALTO            1U

#define LCD_PRETO  0x0000U
#define LCD_BRANCO 0xFFFFU
#define LCD_VERDE  0x07E0U
#define LCD_VERMELHO 0xF800U
#define LCD_CINZA  0xC618U
#define LCD_AMARELO 0xFFE0U

// 2 metades, 32 amostras por metade, 3 eixos

volatile uint16_t adc_buffer[2][AMOSTRAS_BLOCO][3];
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

// Valores de animacao: NAO alteram os dados recolhidos pelo ADC.
static uint8_t barra_visual_px = 0U;
static uint8_t barra_desenhada_px = 0U;
static uint8_t balao_raio_desenhado = 0U;
static uint8_t balao_rebentou = 0U;
static uint8_t contador_rebentamento = 0U;
static uint8_t blocos_rebou = 0U;
static uint8_t pode_rebentar = 1U;

// Estado do texto de intensidade, independente da velocidade do balao.
#define VIB_BAIXA 0U
#define VIB_MEDIA 1U
#define VIB_ALTA  2U
static uint8_t nivel_vibracao = VIB_BAIXA;

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

    lcd_draw_fillrect(10U, 31U, 108U, 12U, LCD_PRETO);
    lcd_draw_string(24U, 33U, (unsigned char *)texto, cor, 1U);
}

static uint8_t AtualizaNivel(uint32_t mediana_mg)
{
    uint8_t novo = nivel_vibracao;

    if (balao_rebentou != 0U)
    {
        return 0U;  // Preservar mensagem de rebentamento.
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
    if (mediana_mg >= BARRA_RMS_MAX_MG)
    {
        return BARRA_LARGURA;
    }

    return (uint8_t)(
        ((mediana_mg - BARRA_RUIDO_REPOUSO_MG) * BARRA_LARGURA +
         (BARRA_RMS_MAX_MG - BARRA_RUIDO_REPOUSO_MG) / 2U) /
        (BARRA_RMS_MAX_MG - BARRA_RUIDO_REPOUSO_MG));
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

    // PB1 - saida para o LED. O botao PA1 ja nao e utilizado.
    gpio.GPIO_Pin = GPIO_Pin_1;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &gpio);
    AtualizaLED(0U);
}

// Maquina de estados do rebentamento, chamada em todos os blocos de 160 ms.
// 1) 3 blocos consecutivos com Med5 >= 500 mg: rebenta, LED ligado.
// 2) Aguarda ~1,28 s sem bloquear o ADC/DMA.
// 3) Apaga LED e desenha automaticamente um balao pequeno.
// 4) Para nao rebentar em ciclo continuo, uma nova explosao so e permitida
//    depois de Med5 ter descido ate 95 mg.
static uint8_t TrataRebentamento(uint32_t rms_mediana_mg)
{
    // 0: nenhum evento; 1: rebentou; 2: reiniciou automaticamente.
    uint8_t evento = 0U;

    if (balao_rebentou != 0U)
    {
        if (blocos_rebou < REBOU_DURACAO_BLOCOS)
        {
            blocos_rebou++;
        }

        if (blocos_rebou >= REBOU_DURACAO_BLOCOS)
        {
            // Reinicio automatico, mesmo que a vibracao ainda esteja alta.
            balao_rebentou = 0U;
            blocos_rebou = 0U;
            contador_rebentamento = 0U;
            barra_visual_px = 0U;
            barra_desenhada_px = 0U;
            nivel_vibracao = VIB_BAIXA;
            AtualizaLED(0U);

            MostraNivelNoLCD(VIB_BAIXA);   // Apaga "REBOU!".
            DesenhaBalao(0U, BALAO_RAIO_MIN);
            DesenhaCorda(0U, BALAO_RAIO_MIN);
            balao_raio_desenhado = BALAO_RAIO_MIN;
            evento = 2U;
        }
        return evento;
    }

    // Reabilitar um futuro rebentamento so depois da intensidade baixar.
    if ((pode_rebentar == 0U) &&
        (rms_mediana_mg <= REARME_AUTOMATICO_MG))
    {
        pode_rebentar = 1U;
    }

    if (pode_rebentar == 0U)
    {
        contador_rebentamento = 0U;
        return 0U;
    }

    if (rms_mediana_mg >= LIMIAR_REBENTAMENTO_MG)
    {
        if (contador_rebentamento < REBENTAMENTO_BLOCOS)
        {
            contador_rebentamento++;
        }

        if ((contador_rebentamento >= REBENTAMENTO_BLOCOS) &&
            (barra_visual_px == BARRA_LARGURA) &&
            (barra_desenhada_px == BARRA_LARGURA) &&
            (balao_raio_desenhado == BALAO_RAIO_MAX))
        {
            balao_rebentou = 1U;
            pode_rebentar = 0U;
            blocos_rebou = 0U;
            contador_rebentamento = 0U;
            AtualizaLED(1U);

            // Desenhar apenas as regioes alteradas pelo rebentamento.
            DesenhaCorda(balao_raio_desenhado, 0U);
            DesenhaBalao(balao_raio_desenhado, 0U);
            balao_raio_desenhado = 0U;
            lcd_draw_fillrect(BARRA_X, BARRA_Y,
                              BARRA_LARGURA, BARRA_ALTURA, LCD_PRETO);
            barra_desenhada_px = 0U;
            barra_visual_px = 0U;

            lcd_draw_fillrect(10U, 31U, 108U, 12U, LCD_PRETO);
            lcd_draw_string(30U, 33U, (unsigned char *)"REBOU!",
                            LCD_VERMELHO, 1U);
            evento = 1U;
        }
    }
    else
    {
        contador_rebentamento = 0U;
    }
    return evento;
}

// Limitar o ritmo de animacao: subida gradual, descida ainda mais lenta.
static uint8_t AtualizaAnimacao(uint32_t rms_mediana_mg, uint32_t bloco)
{
    uint8_t alvo, diferenca, novo_raio;
    uint8_t desenhou = 0U;

    if (balao_rebentou != 0U ||
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
            lcd_draw_fillrect((uint8_t)(BARRA_X + barra_desenhada_px),
                              BARRA_Y, diferenca, BARRA_ALTURA, LCD_VERDE);
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
    evento = TrataRebentamento(rms_mediana_mg);
    if (evento == 0U)
    {
        mudou_nivel = AtualizaNivel(rms_mediana_mg);
        mudou_animacao = AtualizaAnimacao(rms_mediana_mg, numero);
    }
    desenhou_lcd = (uint8_t)(mudou_nivel || mudou_animacao);

    if (evento == 1U)       marca = " [REBOU]";
    else if (evento == 2U)  marca = " [REINICIO AUTO]";
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

    // O lcd_init ja apaga todo o LCD. Desenhar so a interface estatica.
    lcd_draw_string(10, 12, (unsigned char *)"SISEM VIBRACAO",
                    LCD_BRANCO, 1);
    MostraNivelNoLCD(VIB_BAIXA);
    lcd_draw_string(10, 53, (unsigned char *)"0", LCD_BRANCO, 1);
    lcd_draw_string(82, 53, (unsigned char *)"500 mg", LCD_BRANCO, 1);

    // Moldura fixa da barra. Interior: x=12..115, y=73..80.
    lcd_draw_fillrect(10, 70, 108, 1, LCD_BRANCO);
    lcd_draw_fillrect(10, 83, 108, 1, LCD_BRANCO);
    lcd_draw_fillrect(10, 71, 1, 12, LCD_BRANCO);
    lcd_draw_fillrect(117, 71, 1, 12, LCD_BRANCO);

    // Balao inicial pequeno, com corda.
    DesenhaBalao(0U, BALAO_RAIO_MIN);
    DesenhaCorda(0U, BALAO_RAIO_MIN);
    balao_raio_desenhado = BALAO_RAIO_MIN;

    // ================================================

    // 3. INICIALIZAR OS RESTANTES PERIFERICOS

    // ================================================

    USART2_Config();

    ConfiguraLED();

    ADC1_DMA_Config();

    TIM3_Config_200Hz();

    // ================================================

    // 4. MENSAGENS INICIAIS

    // ================================================

    USART2_EnviaTexto("\r\nPROJETO SISEM\r\n");

    USART2_EnviaTexto("ADC + DMA + TIM3 + LCD\r\n");

    USART2_EnviaTexto("200Hz | Med5 | BAIXA/MEDIA/ALTA | POP=500mg | AUTO\r\n");
    USART2_EnviaTexto("LCD e USART: eventos + 1 linha / 2 blocos; DMA_desc=descartes\r\n");

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

        // Primeira metade do buffer pronta

        if (DMA_GetFlagStatus(DMA1_FLAG_HT1) != RESET)

        {

            DMA_ClearFlag(DMA1_FLAG_HT1);

            numero_bloco++;

            MostraBloco(0, numero_bloco);

        }

        // Segunda metade do buffer pronta

        if (DMA_GetFlagStatus(DMA1_FLAG_TC1) != RESET)

        {

            DMA_ClearFlag(DMA1_FLAG_TC1);

            numero_bloco++;

            MostraBloco(1, numero_bloco);

        }

    }

}
