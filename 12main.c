
////Exercicio 3
//
//	#include "stm32f10x.h"
//
//	// Função a simular um delay
//	void delay(uint32_t n)
//	{
//	    volatile uint32_t i;
//	    for (i = 0; i < n; i++);
//	        
//	}
//
//	int main(void)
//	{
//	    uint32_t T_500ms = 700000;      // valor no qual parece mais ou menos 500 ms
//
//	    RCC->APB2ENR |= (1 << 2);       // Ligar o relógio do porto A (bit 2 do APB2ENR)
//
//	    // 2. PA5 como saída push-pull a 2 MHz
//	    //    pino 5 -> CRL, deslocamento 5*4 = 20
//	    //    CNF=00 (push-pull) + MODE=10 (2 MHz) -> 0010 = 0x2
//
//	          GPIOA->CRL &= ~(0xFu << 20);    // limpa os 4 bits do PA5
//	          GPIOA->CRL |=  (0x2u << 20);    // escreve 0010
//
//	    while (1)
//	    {
//	        GPIOA->BSRR = (1 << 5);         // PA5 = 1 -> acende
//	        delay(T_500ms);
//	        GPIOA->BSRR = (1 << (5 + 16));  // PA5 = 0 -> apaga
//	        delay(T_500ms);
//	    }
//	}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////Exercicio 4
//
//	#include "stm32f10x.h"
//
//	// Função a simular um delay
//	void delay(uint32_t n)
//	{
//		volatile uint32_t i;
//		for (i = 0; i < n; i++);
//	
//	}
//
//	int main(void)
//	{
//		GPIO_InitTypeDef GPIO_InitStructure;    // GPIO Struct
//		uint32_t T_500ms = 700000;              // Tempo para 500 ms
//
//
//		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); //Ligar o relógio do porto A (substitui o RCC->APB2ENR |= (1<<2))
//
//		// 2. PA5 como saída push-pull a 2 MHz
//		GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5;          // Pino 5 do led da plca
//		GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;    // saída push-pull
//		GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;     // velocidade de comutação
//		GPIO_Init(GPIOA, &GPIO_InitStructure);               // escreve no CRL por nós
//
//
//		while (1)
//		{
//			GPIO_SetBits(GPIOA, GPIO_Pin_5);      // PA5 = 1 -> acende (usa o BSRR)
//			delay(T_500ms);
//			GPIO_ResetBits(GPIOA, GPIO_Pin_5);    // PA5 = 0 -> apaga  (usa o BRR)
//			delay(T_500ms);
//		}
//	}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////Exercicio 5


#include "stm32f10x.h"

  // Gasta tempo por software.

  void delay(uint32_t n)
  {
      volatile uint32_t i;
      for (i = 0; i < n; i++);
          
  }

  int main(void)
  {
      GPIO_InitTypeDef GPIO_InitStructure;

      uint32_t T_500ms = 700000;          // 1 Hz -> botão solto (calibrar à vista)
      uint32_t T_250ms = 350000;      	  // 2 Hz -> botão premido (o dobro)
      uint32_t tempo;                     // tempo usado em cada ciclo

      // 1. Ligar os relógios dos portos A (LED) e C (botão)
      RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC, ENABLE);

      // 2. PA5 como saída push-pull a 2 MHz (LED)
      GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5;
      GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
      GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
      GPIO_Init(GPIOA, &GPIO_InitStructure);

      // 3. PC13 como entrada floating 
      GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_13;
      GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
      GPIO_Init(GPIOC, &GPIO_InitStructure);

      // 4. Ciclo infinito
      while (1)
      {

          if (GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_13) == Bit_RESET){
          	tempo = T_250ms;            // premido -> 2 Hz
          }else{
          	tempo = T_500ms;            // solto   -> 1 Hz}
          }

          GPIO_SetBits(GPIOA, GPIO_Pin_5);        // acende
          delay(tempo);
          GPIO_ResetBits(GPIOA, GPIO_Pin_5);      // apaga
          delay(tempo);
      }
  }
