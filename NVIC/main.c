/**
Trabalho NVIC
Juliana Rodrigues 1231182
Ema Mota
*/



#include "stm32f10x.h"
#include "stm32f10x_it.h"

volatile uint32_t toggle_count = 0;

void RCC_Config_HSE_PLL_Max(void) {
    RCC_DeInit();
    RCC_HSEConfig(RCC_HSE_ON);
    if (RCC_WaitForHSEStartUp() == SUCCESS) {
        FLASH_SetLatency(FLASH_Latency_2);
        FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);

        RCC_HCLKConfig(RCC_SYSCLK_Div1); // AHB = 72 MHz
        RCC_PCLK1Config(RCC_HCLK_Div2);  // APB1 = 36 MHz (máx.)
        RCC_PCLK2Config(RCC_HCLK_Div1);  // APB2 = 72 MHz

        RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_6); // 12 MHz * 6 = 72 MHz
        RCC_PLLCmd(ENABLE);
        while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);

        RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
        while (RCC_GetSYSCLKSource() != 0x08);
    } else {
        while (1);
    }
}

//Exercicio 1
void ex1(){

	//TIMER 3 - ativar o clock no barramento APB1
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

	//Configuração da base de tempo do TIM3
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;

	//(PSC+1)*(ARR+1)/72MHz = 0.25s (250ms -> acende 250ms, apaga 250ms = 2 Hz)
	TIM_TimeBaseStructure.TIM_Period = 2500 - 1; //auto-reload (ARR) de 0 até 65535
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_Prescaler = 7200 - 1; //prescaler (PSC) de 0 até 65535 (72MHz/7200 = 10kHz)
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; //contador crescente

	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

	//Ativa a interrupção do Update Event do TIM3
	TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

	//Configuração do NVIC para o TIM3
	NVIC_InitTypeDef NVIC_InitStructure;

	//Priority Group com 1 bit de preempção e 3 bits de sub-prioridade
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; //prioridade de preempção
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;        //sub-prioridade
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;           //ativar o canal no NVIC
	NVIC_Init(&NVIC_InitStructure);

	//Ativar o contador do TIM3
	TIM_Cmd(TIM3, ENABLE);
}



//Exercicio 2
void ex2(){

	//Ligar o pino PA1 à linha de interrupção externa EXTI1
	GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);

	//Configuração do controlador EXTI
	EXTI_InitTypeDef EXTI_InitStructure;

	EXTI_InitStructure.EXTI_Line = EXTI_Line1;               //Linha 1 do EXTI
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;       //Modo interrupção (não evento)
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;   //Flanco descendente (quando o botão SW5 é pressionado liga a 0)
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;                 //Ativar a linha EXTI1
	EXTI_Init(&EXTI_InitStructure);

	//Configuração do NVIC para a interrupção externa EXTI1
	NVIC_InitTypeDef NVIC_InitStructure;

	NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; //prioridade de preempção inicial
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;        //sub-prioridade inicial
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;           //ativar canal no NVIC
	NVIC_Init(&NVIC_InitStructure);
}



//EXERCÍCIO 3

void ex3(){

	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;

	//Priority Group com 1 bit de preempção e 3 bits de sub-prioridade
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

	//EXTI1 com prioridade mais alta (Preempção = 0)
	NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; //mais prioritário
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_Init(&NVIC_InitStructure);

	//TIM3 com prioridade mais baixa (Preempção = 1)
	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; //menos prioritário que o EXTI1
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_Init(&NVIC_InitStructure);
}

//Exercicio 4

void ex4(){

	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;

	//Priority Group com 1 bit de preempção e 3 bits de sub-prioridade
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

	//TIM3 com prioridade mais alta (Preempção = 0)
	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; //agora o Timer é mais prioritário
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_Init(&NVIC_InitStructure);

	//EXTI1 com prioridade mais baixa (Preempção = 1)
	NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; //agora a interrupção externa é menos prioritária
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_Init(&NVIC_InitStructure);
}

// EXERCÍCIO 5

void ex5(){

	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;

	//Priority Group com 1 bit de preempção e 3 bits de sub-prioridade
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

	//Caso A: EXTI1 com Sub-prioridade 0 e TIM3 com Sub-prioridade 1
	//(Mesma Preempção = 0 para ambas!)
	NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; //mesma prioridade
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;        //sub-prioridade 0
	NVIC_Init(&NVIC_InitStructure);

	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; //mesma prioridade
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;        //sub-prioridade 1
	NVIC_Init(&NVIC_InitStructure);

	/*
	NOTA: Depois de testares o Caso A, podes inverter as sub-prioridades para testar o Caso B:
	- EXTI1 com SubPriority = 1
	- TIM3  com SubPriority = 0
	*/
}
int main(void){

	//Configuração do clock do sistema a 72 MHz
	RCC_Config_HSE_PLL_Max();

	//Ativar clocks dos GPIOs e do AFIO (AFIO é obrigatório para o EXTI funcionar!)
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

	//Inicialização do Led0 (PB0) e Led1 (PB1) como saídas Push-Pull
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	//Leds começam apagados
	GPIO_ResetBits(GPIOB, GPIO_Pin_0 | GPIO_Pin_1);

	//Inicialização do botão SW5 (PA1) como entrada com Pull-Up
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	//Arrancar os dois exercícios
	ex1();
	ex2();
	ex3();// retirar quando for testar o ex4,5
	ex4();//// retirar quando for testar o ex5
	ex5();

	while(1){
	}




