/**
Trabalho USART - Ficha 5
Juliana Rodrigues 1231182
Ema Mota
*/

#include "stm32f10x.h"
#include "stm32f10x_it.h"
#include "lcd.h"

volatile int nivel_duty = 10; // começa com 100% de brilho (nível 10 de 10)

void RCC_Config_HSE_PLL_Max(void) {

	RCC_DeInit();

	RCC_HSEConfig(RCC_HSE_ON); // ativar o HSE

	ErrorStatus HSEStartUpStatus;
	HSEStartUpStatus = RCC_WaitForHSEStartUp();
	if (HSEStartUpStatus == SUCCESS) /* devolve SUCCESS/ERROR */
	{
		FLASH_SetLatency(FLASH_Latency_2); // freq é 72 Mhz
		FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);

		RCC_HCLKConfig(RCC_SYSCLK_Div1); // a freq máx do HCLK (AHB) é 72 MHz. Div1 = 72 MHz
		RCC_PCLK1Config(RCC_HCLK_Div2);  // a freq máx do PCLK1 (APB1) é 36 MHz. Div2 = 36 MHz
		RCC_PCLK2Config(RCC_HCLK_Div1);  // a freq máx do PCLK2 (APB2) é 72 MHz. Div1 = 72 MHz

		RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_6); // configuração da PLL (12x6=72Mhz)

		RCC_PLLCmd(ENABLE); // ativa-se o PLL
		while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET); // esperar que a PLL arranque

		RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK); // configurar o PLL como clock
		while (RCC_GetSYSCLKSource() != 0x08);     // esperar que estabilize o PLL
	}
	else {
		while (1); /* ou inicia o procedimento de erro */
	}
}

// Configuração do Led D1
void GPIO_Configuration(void) {

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); /* Enable GPIOB clock */

	// o PB0 como saída para o Led D1
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	GPIO_ResetBits(GPIOB, GPIO_Pin_0); // começa apagado
}

// Exercicio 1 (Comunicação bidirecional assíncrona por polling a 9600 bps)

void ex1_USART2_Init(void) {

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);  /* Enable GPIOA clock */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE); /* Enable USART2 clock */

	// configurar os pinos da USART2: PA2 (TX) e PA3 (RX)
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP; // TX em Alternate Function Push-Pull
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; // RX em entrada flutuante
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	// configuração da USART2 a 9600 bps, 8 bits de dados e 1 stop bit
	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate            = 9600;
	USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits            = USART_StopBits_1;
	USART_InitStructure.USART_Parity              = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART2, &USART_InitStructure);

	USART_Cmd(USART2, ENABLE); // ativar a USART2
}

// Exercicio 2 (Recepção da USART2 por interrupção)

void ex2_USART2_Init(void) {

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate            = 9600;
	USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits            = USART_StopBits_1;
	USART_InitStructure.USART_Parity              = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART2, &USART_InitStructure);

	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE); // ativar interrupção quando recebe dados (RXNE)

	// configuração do NVIC para a USART2
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	USART_Cmd(USART2, ENABLE);
}

// Exercicio 3 (Menu no display com o carácter 'M')

void mostra_menu(void) {

	lcd_draw_fillrect(0, 0, LCD_WIDTH, LCD_HEIGHT, 0x0000); // limpa o ecrã a preto

	// opções do menu pedidas na ficha
	lcd_draw_string(10, 20, (unsigned char *)"=== MENU ===", 0xFFFF, 1);
	lcd_draw_string(10, 50, (unsigned char *)"1 - 9600",     0x07E0, 1);
	lcd_draw_string(10, 70, (unsigned char *)"2 - 19200",    0x07E0, 1);
	lcd_draw_string(10, 90, (unsigned char *)"3 - 115200",   0x07E0, 1);
}

// Exercicio 4 (Função para alterar a configuração da USART conforme o menu)

void altera_baudrate(uint32_t novo_baudrate) {

	USART_InitTypeDef USART_InitStructure;

	USART_Cmd(USART2, DISABLE); // desligar antes de reconfigurar

	USART_InitStructure.USART_BaudRate            = novo_baudrate;
	USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits            = USART_StopBits_1;
	USART_InitStructure.USART_Parity              = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART2, &USART_InitStructure);

	USART_Cmd(USART2, ENABLE); // voltar a ligar com a nova velocidade
}

// Exercicio 6 (PWM de 1 kHz no pino GPIOB1 com duty cycle inicial de 100%)
// Como o clock do TIM3 é 72 MHz e queremos 1 kHz: (PSC+1)*(ARR+1) = 72 MHz / 1000 = 72000.
// Usamos PSC=71 (72) e ARR=999 (1000 passos), assim cada 10% de duty cycle são 100 no pulse

void ex6_PWM_Init(void) {

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); /* Enable GPIOB clock */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);  /* Enable TIM3 clock */

	// o PB1 como função alternativa push-pull ligado ao TIM3_CH4
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	TIM_TimeBaseStructure.TIM_Prescaler     = 72 - 1;   // prescaler fica a 1 MHz
	TIM_TimeBaseStructure.TIM_Period        = 1000 - 1; // 1000 contagens = 1 ms (1 kHz)
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

	// configurar o Canal 4 em PWM modo 1
	TIM_OCInitTypeDef TIM_OCInitStructure;
	TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_Pulse       = 1000; // duty cycle inicial a 100% (1000/1000)
	TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
	TIM_OC4Init(TIM3, &TIM_OCInitStructure);

	TIM_OC4PreloadConfig(TIM3, TIM_OCPreload_Enable);
	TIM_ARRPreloadConfig(TIM3, ENABLE);

	TIM_Cmd(TIM3, ENABLE); // ativar o TIM3
}

// Função de interpretação dos caracteres (Exercícios 2, 3, 4, 5 e 6)

void interpreta_caracter(char c) {

	// Exercicio 2: cada vez que recebe 'R' faz toggle do Led D1
	if (c == 'R' || c == 'r') {
		GPIOB->ODR ^= GPIO_Pin_0;
	}
	// Exercicio 3: 'M' mostra o menu no display
	else if (c == 'M' || c == 'm') {
		mostra_menu();
	}
	// Exercicio 4: mudar o baud rate de acordo com o menu
	else if (c == '1') {
		altera_baudrate(9600);
		lcd_draw_string(10, 120, (unsigned char *)"Ativo: 9600  ", 0xFFE0, 1);
	}
	else if (c == '2') {
		altera_baudrate(19200);
		lcd_draw_string(10, 120, (unsigned char *)"Ativo: 19200 ", 0xFFE0, 1);
	}
	else if (c == '3') {
		altera_baudrate(115200);
		lcd_draw_string(10, 120, (unsigned char *)"Ativo: 115200", 0xFFE0, 1);
	}
	// Exercicio 5: 'C' e 'c' apaga o conteúdo do display
	else if (c == 'C' || c == 'c') {
		lcd_draw_fillrect(0, 0, LCD_WIDTH, LCD_HEIGHT, 0x0000);
	}
	// Exercicio 6: '+' aumenta o duty cycle (10 níveis até 100%)
	else if (c == '+') {
		if (nivel_duty < 10) {
			nivel_duty++;
			TIM_SetCompare4(TIM3, nivel_duty * 100);
		}
	}
	// Exercicio 6: '-' diminui o duty cycle (10 níveis até 0%)
	else if (c == '-') {
		if (nivel_duty > 0) {
			nivel_duty--;
			TIM_SetCompare4(TIM3, nivel_duty * 100);
		}
	}
}

int main(void) {

	RCC_Config_HSE_PLL_Max();
	GPIO_Configuration();

	// inicialização do display
	lcd_init();
	lcd_draw_fillrect(0, 0, LCD_WIDTH, LCD_HEIGHT, 0x0000); // começa apagado

	// arrancar a USART2 e o PWM
	ex2_USART2_Init();
	ex6_PWM_Init();

	while (1) {
	}
}
