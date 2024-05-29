#include <stm32f0xx.h>
#include "mci_clock.h"
#include <stdio.h>
#include "gpio.h"
#include "parsing.h"

#define DEBUG
//#define DEBUG_LVL_1
//#define DEBUG_LVL_2
//#define DEBUG_LVL_3
#define DEBUG_LVL_4
#define DEBUG_LVL_5

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

void StartS1(void);
void StartS1_send_CS(void);
void StartS1_wait_Start(void);

void StartS2(void);
void StartS2_wait_CS(void);
void StartS2_send_Start(void);

void Offense_shoot(void);
void Offense_wait(void);

void Defense(void);
void GameEndWon(void);
void GameEndLost(void);
void initializeSM(void);

typedef enum {IDLE=0, 
                STARTS1, STARTS1_SEND_CS, STARTS1_WAIT_START,
                STARTS2, STARTS2_WAIT_CS, STARTS2_SEND_START,
                OFFENSE_SHOOT, OFFENSE_WAIT,
                DEFENSE,
                GAMEENDLOST, GAMEENDWON} State_Type;
static void (*state_table[])(void)={Idle, 
                                    StartS1, StartS1_send_CS, StartS1_wait_Start,
                                    StartS2, StartS2_wait_CS, StartS2_send_Start,
                                    Offense_shoot, Offense_wait,
                                    Defense,
                                    GameEndLost, GameEndWon}; // Function pointer array for Statemachine functions


//-----------------Create global variables
static State_Type curr_state; /* The "current state" */
static char rx_buffer[BUFFERSIZE];
static uint8_t rx_index = 0;

static uint8_t myboard[10*10] = {0};                           //------------------Init my board (Reihe*10 + Spalte (0-9!!!))
static uint8_t enemyboard[10*10] = {0};                        //------------------Init enemy board (Reihe*10 + Spalte (0-9!!!))
static uint8_t hits_on_me[10*10] = {0};                        //------------------Init hits on me (Reihe*10 + Spalte (0-9!!!))

typedef struct{
    uint8_t row;
    uint8_t col;
}Shot;

static Shot last_shot = {0, 0};
//static Shot hits_on_enemy[30];


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
        if(received_char == 'x'){                          //---------?--------If too much time has passed, CHEATER-State?
            rx_buffer[rx_index] = '\0';

            if(msg_starts_with(rx_buffer, "START")){
                //parse_message(rx_buffer);
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
    //PARSE CS Player 2                                         //---------?--------Implement Player 2 Checksum parsing
    
    if(USART2->ISR & USART_ISR_RXNE){
        
        char received_char = USART2->RDR;
        if(received_char == 'x'){                          //---------?--------If too much time has passed, CHEATER-State?
            rx_buffer[rx_index] = '\0';

            if(msg_starts_with(rx_buffer, "CS")){
                //parse_message(rx_buffer);                   //---------?--------Message back to Origin, remove later on!!!  
                curr_state = STARTS1_SEND_CS;                           //-----------------Change the checksum received flag
            }else{
                LOG("Invalid Message\n");                   //---------?--------Invalid message, maybe CHEATER-State?
            }
            rx_index = 0;
        }else if (rx_index < BUFFERSIZE - 1){
            rx_buffer[rx_index++] = received_char;
        }        
    }
    
}

void StartS1_send_CS(void){
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

    char own_checksum[13];                            //------------------Init checksum
    calculate_checksum(myboard, own_checksum);              //------------------Calculate own checksum

    LOG("%s", own_checksum);                             //------------------Send own checksum
    curr_state = STARTS1_WAIT_START;                     //------------------Change the state, wait for Start_msg from S2
}

void StartS1_wait_Start(void){
    if(USART2->ISR & USART_ISR_RXNE){
        //LOG("in S1 wait start");
        char received_char = USART2->RDR;
        if(received_char == 'x'){                          //---------?--------If too much time has passed, CHEATER-State?
            rx_buffer[rx_index] = '\0';            

            if(msg_starts_with(rx_buffer, "START")){
                //parse_message(rx_buffer);
                curr_state = OFFENSE_SHOOT;                       //-----------------Change the state, going to Offense

                #ifdef DEBUG
                //LOG("successfully read Startmsg");
                #endif

            }else{
                LOG("Invalid Message\n");                   //---------?--------Invalid message, maybe CHEATER-State?
            }
            rx_index = 0;
        }else if (rx_index < BUFFERSIZE - 1){
            rx_buffer[rx_index++] = received_char;
        }        
    }
}


//-----------------StartS2 State
void StartS2(void){
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

    char own_checksum[14];                                  //------------------Init checksum
    calculate_checksum(myboard, own_checksum);              //------------------Calculate own checksum
    LOG("%s", own_checksum);                             //------------------Send own checksum

    curr_state = STARTS2_WAIT_CS;                     //------------------Change the state, wait for Start_msg from S2
}

void StartS2_wait_CS(void){
    if(USART2->ISR & USART_ISR_RXNE){
        
        char received_char = USART2->RDR;
        if(received_char == 'x'){                          //---------?--------If too much time has passed, CHEATER-State?
            rx_buffer[rx_index] = '\0';

            if(msg_starts_with(rx_buffer, "CS")){
                //parse_message(rx_buffer);                   //---------?--------Message back to Origin, remove later on!!!
                curr_state = STARTS2_SEND_START;                           //-----------------Change the checksum received flag
            }else{
                LOG("Invalid Message\n");                   //---------?--------Invalid message, maybe CHEATER-State?
            }
            rx_index = 0;
        }else if (rx_index < BUFFERSIZE - 1){
            rx_buffer[rx_index++] = received_char;
        }        
    }
}

void StartS2_send_Start(void){
    LOG("START52216067\n");
    curr_state = DEFENSE;                      //-----------------Change the state, going to Defense
}

//-----------------Offense State

// enemyboard legend: 0-->did not shoot there already, 1-->shot there already, 2-->hit



void Offense_shoot(void){
    #ifdef DEBUG_LVL_2
    LOG("in Offense_shoot\n");
    #endif

    char shoot_msg[7] = {'B', 'O', 'O', 'M', '0', '0', '\n'};
    int shot_already = 0;
    for (int row = 0; row < 10; row++){
        if (shot_already == 1){ break; }

        for (int col = 0; col < 10; col++){
            if (shot_already == 1){ break; }

            if(enemyboard[row*10 + col] == 0){
                shoot_msg[4] = '0' + col;
                shoot_msg[5] = '0' + row;
                LOG("%s", shoot_msg);
                enemyboard[row*10 + col] = 1;
                last_shot.row = row;                 //------------------Save last shot row
                last_shot.col = col;                 //------------------Save last shot col
                shot_already = 1;
                curr_state = OFFENSE_WAIT;            //------------------Change the state, going to waiting for response
            }
        }
    }
    shot_already = 0;
}

void Offense_wait(void){
    #ifdef DEBUG_LVL_3
    //LOG("in Offense_wait\n");
    #endif
    if(USART2->ISR & USART_ISR_RXNE){
        
        char received_char = USART2->RDR;
        if(received_char == 'x'){                          //---------?--------If too much time has passed, CHEATER-State?
            rx_buffer[rx_index] = '\0';

            if(rx_buffer[0] == 'T'){
                enemyboard[last_shot.row*10 + last_shot.col] = 2;    //------------------If hit, mark the enemy board
                if (check_win_or_loss() == 1){
                    curr_state = GAMEENDWON;                              //---------?---------Check if win or loss

                    #ifdef DEBUG_LVL_4
                    LOG("I won\n")
                    #endif
                }else{
                    curr_state = DEFENSE;                                 //------------------Change the state, going to Defense
                    //LOG("now transitioning to defense\n");
                }
            }else if(rx_buffer[0] == 'W'){
                enemyboard[last_shot.row*10 + last_shot.col] = 1;
                curr_state = DEFENSE;                                 //------------------Change the state, going to Defense
            }
            rx_index = 0;
        }else if (rx_index < BUFFERSIZE - 1){
            rx_buffer[rx_index++] = received_char;
            #ifdef DEBUG_LVL_3
            LOG("received char: %c\n", received_char);
            #endif
        }        
    }
}


//-----------------Defense State
void Defense(void){
    
    if(USART2->ISR & USART_ISR_RXNE){
        
        char received_char = USART2->RDR;
        if(received_char == 'x'){                          //---------?--------If too much time has passed, CHEATER-State?
            rx_buffer[rx_index] = '\0';

            if(msg_starts_with(rx_buffer, "BOOM")){
                //parse_message(rx_buffer);                   //---------?--------Message back to Origin, remove later on!!!
                int shot_received_row = rx_buffer[4] - '0';  //------------------Get the row of the shot
                int shot_received_col = rx_buffer[5] - '0';  //------------------Get the col of the shot
                if(myboard[shot_received_row*10 + shot_received_col] != 0){
                    LOG("T\n");                             //------------------If hit, send T
                    hits_on_me[shot_received_row*10 + shot_received_col] = 1;    //------------------If hit, mark the hits_on_me board
                }else{
                    LOG("W\n");                             //------------------If miss, send M
                }
                #ifdef DEBUG_LVL_1
                LOG("i received a shot\n");
                #endif
                if (check_win_or_loss() == 2){
                    curr_state = GAMEENDLOST;
                    #ifdef DEBUG_LVL_1
                    LOG("I thought i won\n");
                    #endif
                }else{
                    curr_state = OFFENSE_SHOOT;
                    #ifdef DEBUG_LVL_1
                    LOG("now transitioning to shooting mode\n");
                    #endif
                }
            }else{
                LOG("Invalid Message\n");                   //---------?--------Invalid message, maybe CHEATER-State?
            }
            rx_index = 0;
        }else if (rx_index < BUFFERSIZE - 1){
            rx_buffer[rx_index++] = received_char;
            //LOG("rec:%c\n", received_char);
        }        
    }
}

void GameEndWon(void){
    //LOG("GameEnd State\r\n");
}
void GameEndLost(void){
    //LOG("GameEndLost State\r\n");
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

    for (size_t i = 2; i < 13; i++)
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
    checksum[12] = '\n';
    checksum[13] = '\0';
}

int check_win_or_loss(void){                            //------------------Check if win or loss, returns 0 if neither, 1 if win, 2 if loss
    int hits_on_me_count = 0;
    int hits_on_enemy_count = 0;
    for(uint8_t i = 0; i < 100; i++){
        if(hits_on_me[i] == 1){
            hits_on_me_count++;
        }
        if(enemyboard[i] == 2){
            hits_on_enemy_count++;
        }
    }

    if(hits_on_me_count == 30){
        // I lost
        return 2;
    }else if(hits_on_enemy_count == 30){
        // I won
        return 1;
    }else{
        return 0;
    }
}

void LOG_Board_message(void){
    char curr_msg[15];
    curr_msg[0] = 'S';
    curr_msg[1] = 'F';
    for (int col = 0; col < 10; col++){
        curr_msg[2] = (char)col;
        for (int row = 0; row < 10; row++){
            curr_msg[row+3] = sprintf("%d", myboard[row*10 + col]);
        }
        curr_msg[14] = '\n';
        LOG("%s", curr_msg);
    }
}