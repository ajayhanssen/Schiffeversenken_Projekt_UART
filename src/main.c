#include <stm32f0xx.h>
#include "mci_clock.h"
#include <stdio.h>
#include "gpio.h"
#include "parsing.h"

#define DEBUG
// This is a simple macro to print debug messages of DEBUG is defined
#ifdef DEBUG
  #define LOG( msg... ) printf( msg );
#else
  #define LOG( msg... ) ;
#endif

#define BAUDRATE 9600
#define BUFFERSIZE 100

// For supporting printf function we override the _write function to redirect the output to UART
int _write( int handle, char* data, int size ) {
    int count = size;
    while( count-- ) {
        while( !( USART2->ISR & USART_ISR_TXE ) ) {};
        USART2->TDR = *data++;
    }
    return size;
}

void UART_config(void){
    // Enable peripheral  GPIOA clock
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    // Enable peripheral  USART2 clock
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    // Configure PA2 as USART2_TX using alternate function 1
    GPIOA->MODER |= GPIO_MODER_MODER2_1;
    GPIOA->AFR[0] |= 0b0001 << (4*2);


    // Configure PA3 as USART2_RX using alternate function 1
    GPIOA->MODER |= GPIO_MODER_MODER3_1;
    GPIOA->AFR[0] |= 0b0001 << (4*3);

    // Configure the UART Baude rate Register 
    USART2->BRR = (APB_FREQ / BAUDRATE);
    // Enable the UART using the CR1 register
    USART2->CR1 |= ( USART_CR1_RE | USART_CR1_TE | USART_CR1_UE );
}

void blue_button_config(void){
    RCC->AHBENR |= RCC_AHBENR_GPIOCEN;
    GPIOC->MODER &= ~(GPIO_MODER_MODER13_Msk);
    GPIOC->PUPDR &= ~GPIO_PUPDR_PUPDR13_Msk;
    GPIOC->PUPDR |= GPIO_PUPDR_PUPDR13_0;
}



//-----------------Create Signatures of Statemachine functions
void Idle(void);
void StartS2(void);
void StartS1(void);
void Offense(void);
void Defense(void);
void initializeSM(void);

typedef enum {IDLE=0, STARTS1, STARTS2, OFFENSE, DEFENSE} State_Type;
static void (*state_table[])(void)={Idle, StartS1, StartS2, Offense, Defense}; // Function pointer array for Statemachine functions

//-----------------Create global variables
static State_Type curr_state; /* The "current state" */
static uint8_t rxb = '\0';
static char rx_buffer[BUFFERSIZE];
static uint8_t rx_index = 0;


int main(void){
    // Configure the system clock to 48MHz
    EPL_SystemClock_Config();
    UART_config();

    // Enable the GPIOC peripheral clock
    RCC->AHBENR |= RCC_AHBENR_GPIOCEN;
    blue_button_config();
    GPIO_OUTPUT_CONFIG(GPIOA, 5);


    initializeSM();

    for(;;){
    
        state_table[curr_state]();

        /* if(USART2->ISR & USART_ISR_RXNE){
            rxb = USART2->RDR;
            LOG("[DEBUG-LOG]: %d\r\n", rxb );
        }
            
        if (!(GPIOC->IDR & GPIO_IDR_13)){
            // Toggle the LED
            LOG("Start");
        } */
    
    
    }   
}


void initializeSM(void){
    curr_state = IDLE;
}

//-----------------Implement the Statemachine functions-----------------

//-----------------Idle State
void Idle(void){
    // If correct Start-Message is received, my yConti becomes S2
    if(USART2->ISR & USART_ISR_RXNE){
        
        char received_char = USART2->RDR;
        if(received_char == '\n'){                          //---------?--------If too much time has passed, CHEATER-State?
            rx_buffer[rx_index] = '\0';

            if(msg_starts_with(rx_buffer, "START")){
                parse_message(rx_buffer);
                curr_state = STARTS2;                       //-----------------Change the state, yConti becomes S2
            }else{
                LOG("Invalid Message\n");                   //---------?--------Invalid message, maybe CHEATER-State?
            }
            rx_index = 0;
        }else if (rx_index < BUFFERSIZE - 1){
            rx_buffer[rx_index++] = received_char;
        }        
    }
    
    // If the blue button is pressed, my yConti becomes S1
    if(!(GPIOC->IDR & GPIO_IDR_13)){
        LOG("START52216067\n")
        curr_state = STARTS1;
    }
}

//-----------------StartS1 State
void StartS1(void){
    //PARSE CS Player 2                                     //---------?--------Implement Player 2 Checksum parsing

    uint8_t myboard[10*10] = {0};                           //------------------Init my board (Reihe*10 + Spalte (0-9!!!))
    place_boat(myboard, 0, 0, 5, 0);                        //------------------Place my boats
    place_boat(myboard, 2, 0, 4, 0);
    place_boat(myboard, 1, 7, 4, 1);
    place_boat(myboard, 4, 2, 3, 0);
    place_boat(myboard, 6, 6, 3, 1);
    place_boat(myboard, 6, 8, 3, 1);
    place_boat(myboard, 9, 0, 2, 0);
    place_boat(myboard, 9, 3, 2, 0);
    place_boat(myboard, 7, 2, 2, 0);
    place_boat(myboard, 6, 0, 2, 0);

    char own_checksum[13] = {0};                            //------------------Init checksum
    calculate_checksum(myboard, own_checksum);              //------------------Calculate own checksum
    if(!(GPIOC->IDR & GPIO_IDR_13)){
        LOG("%s\n", own_checksum);                             //------------------Send own checksum
    }
}

//-----------------StartS2 State
void StartS2(void){
    LOG("StartS2 State\r\n");
    //curr_state = OFFENSE;
}

//-----------------Offense State
void Offense(void){
    //LOG("Offense State\r\n");
    //curr_state = DEFENSE;
}

//-----------------Defense State
void Defense(void){
    LOG("Defense State\r\n");
    curr_state = IDLE;
}

//-----------------End of Statemachine functions-----------------


int msg_starts_with(const char* str, const char* prefix){
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

void parse_message(char *msg){
    LOG("%s\n", msg);
}

void place_boat(uint8_t* board, uint8_t row, uint8_t col, uint8_t size, uint8_t direction){
    if(row > 9 || col > 9 || size > 5 || direction > 1){    //-----------------Check if the input is valid (0-9, 0-9, 1-5, 0-1)
        return;
    }
    if(direction == 0){                                     //-----------------If direction is 0, place the boat horizontally	
        for(uint8_t i = 0; i < size; i++){
            board[row*10 + col + i] = size;
        }
    }else{                                                  //-----------------If direction is 1, place the boat vertically
        for(uint8_t i = 0; i < size; i++){
            board[(row + i)*10 + col] = size;
        }
    }
}

void calculate_checksum(uint8_t* board, char* checksum){
    checksum[0] = 'C';
    checksum[1] = 'S';

    for (size_t i = 2; i < 12; i++)
    {
        checksum[i] = '0';
    }
    
    for(uint8_t col = 0; col < 10; col++){
        for(uint8_t row = 0; row < 10; row++){
            if (board[row*10 + col] != 0){
                checksum[col+2] += 1;
            }
        }
    }
    checksum[11] = '\n';
    checksum[12] = '\0';
}