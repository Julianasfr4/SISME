/**
Trabalho RCC
Juliana Rodrigues 1231182
Ema Mota
*/


//Exercicio 1

#include "stm32f10x.h"

void RCC_Config_HSI_default(void) {
	
RCC_DeInit();
RCC_HSICmd(ENABLE);

while(RCC_GetFlagStatus(RCC_FLAG_HSIRDY ) == RESET ); //permite esperar que um sinal HSI de relógio esteja pronto.

FLASH_SetLatency(FLASH_Latency_0);// pq a freq é 8 MHZ

// estão configurados para o valor máximo possível mediante o SYSCLK (8MHZ).
RCC_PCLK1Config(RCC_HCLK_Div1); 
RCC_PCLK2Config(RCC_HCLK_Div1); 
RCC_HCLKConfig(RCC_SYSCLK_Div1);

RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);//configurar o HSI como clock

while(RCC_GetSYSCLKSource() != 0x00); //esperar que establize o HSI

}


//Exercicio 2


void RCC_Config_HSI_PLL_Max(void) {
	
RCC_DeInit();

RCC_HSICmd(ENABLE); //ligar o HSI

while(RCC_GetFlagStatus(RCC_FLAG_HSIRDY ) == RESET ); //permite esperar que um sinal HSI de relógio esteja pronto.

FLASH_SetLatency(FLASH_Latency_2);// pq a freq é máxima

FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable); //de maneira a acelaramos o processo pois a frq é máx

// estão configurados para o valor máximo possível mediante o SYSCLK máxima
// entram 8 MHz e são divididos por 2 para entrarem no bloco PLL ou seja fica 4 MHz, depois como temos de obter a máxima frquencia temos de multiplicar pelo multiplicador máximo possível(16). POrtanto ficamos com 4x16=64Mhz
RCC_PCLK1Config(RCC_HCLK_Div2); //  a freq máx do PCLK1 (APB1) é 36 MHz. Div2 = 32 MHz
RCC_PCLK2Config(RCC_HCLK_Div1); //  a freq máx do PCLK2 (APB2) é 72 MHz. Div1 = 64 MHz
RCC_HCLKConfig(RCC_SYSCLK_Div1); //	a freq máx do HCLK (AHB) é 72 MHz. Div1 = 64 MHz

RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_16); //configuração da PLL 

RCC_PLLCmd(ENABLE);//ativa-se o PLL

while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY ) == RESET ); // esperar que a PLL arranque

RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK); // onfigurar o PLL como clock

while(RCC_GetSYSCLKSource() != 0x08);     //esperar que establize o PLL
}

//Exercicio 3


void RCC_Config_HSE_Default(void){
	
	RCC_DeInit();
	
	RCC_HSEConfig(RCC_HSE_ON);//ativar o HSE
	
	ErrorStatus HSEStartUpStatus;
	HSEStartUpStatus = RCC_WaitForHSEStartUp();
	if(HSEStartUpStatus == SUCCESS)/*devolve SUCCESS/ERROR*/
	{
		FLASH_SetLatency(FLASH_Latency_0); // freq é 12 Mhz
		RCC_PCLK1Config(RCC_HCLK_Div1); 
		RCC_PCLK2Config(RCC_HCLK_Div1); 
		RCC_HCLKConfig(RCC_SYSCLK_Div1);
		RCC_SYSCLKConfig(RCC_SYSCLKSource_HSE); 
		while(RCC_GetSYSCLKSource() != 0x04);
	}
	else
	while(1); /*ou inicia o procedimento de erro*/
	
}
//Exercicio 4 (usei o valor de 12 Mhz para o HSE do exercio 3) 



void RCC_Config_HSE_PLL_Max(void){
	
	RCC_DeInit();
	
	RCC_HSEConfig(RCC_HSE_ON);//ativar o HSE
	
	ErrorStatus HSEStartUpStatus;
	HSEStartUpStatus = RCC_WaitForHSEStartUp();
	if(HSEStartUpStatus == SUCCESS)/*devolve SUCCESS/ERROR*/
	{
		FLASH_SetLatency(FLASH_Latency_2); // freq é 72 Mhz
		FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);
		RCC_PCLK1Config(RCC_HCLK_Div2); 
		RCC_PCLK2Config(RCC_HCLK_Div1); 
		RCC_HCLKConfig(RCC_SYSCLK_Div1);
		
		RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_6); //configuração da PLL (12x6=72Mhz) 
		
		RCC_PLLCmd(ENABLE);//ativa-se o PLL
		while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY ) == RESET ); // esperar que a PLL arranque

		RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK); // configurar o PLL como clock
		while(RCC_GetSYSCLKSource() != 0x08);     //esperar que establize o PLL
		}
	else{
	while(1); /*ou inicia o procedimento de erro*/
	}
}

//Exercicio 5 (/*Não deu para usar o HSI (8 MHz) porque a regra obriga a dividir logo por 2, ficando 4 MHz. Para chegar aos 30 MHz precisávamos de multiplicar por 7.5, e como a PLL só aceita números inteiros, com o HSI era impossível.
			   //Então usamos o HSE que tem 12 MHz. Mas se usássemos os 12 MHz diretos também não dava um número certo (12 x 2.5). A solução foi meter o HSE a dividir por 2, ficando 6 MHz. Depois foi só usar o multiplicador 5 e ficamos com 6 x 5 = 30 MHz*/)


void RCC_Config_30MHz(void){
	
	RCC_DeInit();
	
	RCC_HSEConfig(RCC_HSE_ON);//ativar o HSE
	
	ErrorStatus HSEStartUpStatus;
	HSEStartUpStatus = RCC_WaitForHSEStartUp();
	if(HSEStartUpStatus == SUCCESS)/*devolve SUCCESS/ERROR*/
	{
		FLASH_SetLatency(FLASH_Latency_1); // freq é 30 Mhz
		FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);
		RCC_PCLK1Config(RCC_HCLK_Div1); 
		RCC_PCLK2Config(RCC_HCLK_Div1); 
		RCC_HCLKConfig(RCC_SYSCLK_Div1);
		
		RCC_PLLConfig(RCC_PLLSource_HSE_Div2, RCC_PLLMul_5); //configuração da PLL (12/2=6Mhz 6x5=30 Mhz) 
		RCC_PLLCmd(ENABLE);//ativa-se o PLL
		while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY ) == RESET ); // esperar que a PLL arranque

		RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK); // configurar o PLL como clock
		while(RCC_GetSYSCLKSource() != 0x08);     //esperar que establize o PLL
	}
	else
	while(1); /*ou inicia o procedimento de erro*/
	
}
	



//Exercicio 6

void delay(uint32_t n) {
    volatile uint32_t i;
    for (i = 0; i < n; i++);
}

//Exercicio 6

int main(void)
{
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); /* Enable GPIOA clock */

	//  o PA5 como saída
	GPIO_InitTypeDef GPIO_InitStructure;    
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5;          
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;    
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;     
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	while(1){
		RCC_Config_30MHz();
		for (int i = 0; i < 10; i++){ //piscar as primeiras 10 vezes 
            GPIOA->BSRR = 0x00000020; // Set GPIOA5 (Liga o LED)
            delay(500000);   
            GPIOA->BSRR = 0x00200000; // Desliga o LED
            delay(500000);            // Espera apagado
		}
		
		RCC_Config_HSI_PLL_Max();
		for (int i = 0; i < 10; i++){ //piscar a segunda  10 vezes 
            GPIOA->BSRR = 0x00000020; // Set GPIOA5 (Liga o LED)
            delay(500000);         
            
            GPIOA->BSRR = 0x00200000; // Desliga o LED
            delay(500000);            // Espera apagado
		}	
		
	}
}
