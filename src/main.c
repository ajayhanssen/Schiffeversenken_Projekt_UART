#include <stm32f0xx.h>
#include "mci_clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "gpio.h"
#include "parsing.h"

#define DEBUG
//#define DEBUG_SCHIFFPY
//#define DEBUG_NO_WIN_LOOSE
//#define DEBUG_HTERM
//#define DEBUG_ENDGAME_WIN
//#define DEBUG_ENDGAME_LOSS
//#define DEBUG_SET_LAYOUT
//#define CHEATING_LOG_ACTIVE
//#define LOG_MESSAGES_ACTIVE
//#define REAL_OPPONENT

// This is a simple macro to print debug messages if DEBUG is defined
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

int SEND( char* data ) {
    int count = strlen(data);

    while( count-- ) {
        while( !( USART1->ISR & USART_ISR_TXE ) ) {};
        USART1->TDR = *data++;
    }
    return count;
}

void UART2_config(void){
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

void UART1_config(void){
    // Enable peripheral  GPIOA clock
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    // Enable peripheral  USART2 clock
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    // Configure PA2 as USART2_TX using alternate function 1
    GPIOA->MODER |= GPIO_MODER_MODER9_1;
    GPIOA->AFR[0] |= 0b0001 << (4*2);


    // Configure PA3 as USART2_RX using alternate function 1
    GPIOA->MODER |= GPIO_MODER_MODER10_1;
    GPIOA->AFR[0] |= 0b0001 << (4*3);

    // Configure the UART Baude rate Register 
    USART1->BRR = (APB_FREQ / BAUDRATE);
    // Enable the UART using the CR1 register
    USART1->CR1 |= ( USART_CR1_RE | USART_CR1_TE | USART_CR1_UE );
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

void Send_SF(void);
void Wait_SF(void);
void GameEnd(void);
void ProtocolError(void);
void initializeSM(void);

typedef enum {IDLE=0, 
                STARTS1, STARTS1_SEND_CS, STARTS1_WAIT_START,
                STARTS2, STARTS2_WAIT_CS, STARTS2_SEND_START,
                OFFENSE_SHOOT, OFFENSE_WAIT,
                DEFENSE,
                SEND_SF, WAIT_SF,
                GAMEEND, PROTOCOLERROR} State_Type;
static void (*state_table[])(void)={Idle, 
                                    StartS1, StartS1_send_CS, StartS1_wait_Start,
                                    StartS2, StartS2_wait_CS, StartS2_send_Start,
                                    Offense_shoot, Offense_wait,
                                    Defense,
                                    Send_SF, Wait_SF,
                                    GameEnd, ProtocolError}; // Function pointer array for Statemachine functions


//-----------------Create global variables
static State_Type curr_state; /* The "current state" */
static uint8_t seed = 0;
static uint8_t player1 = 0; // 1 for player one, zero for player two
static uint8_t gamestatus = 0; // 1 for game won, 2 for game lost, 3 for protocol error
static uint8_t gameiterations = 0; // counting the number of game iterations

static char rx_buffer[BUFFERSIZE];
static uint8_t rx_index = 0;

static uint8_t myboard[10*10] = {0};                           //------------------Init my board (Reihe*10 + Spalte (0-9!!!))
static uint8_t enemyboard[10*10] = {0};                        //------------------Init enemy board (Reihe*10 + Spalte (0-9!!!))
static uint8_t hits_on_me[10*10] = {0};                        //------------------Init hits on me (Reihe*10 + Spalte (0-9!!!))

static uint8_t myweights[10] = {0};                                 //------------------Init my checksum array
static uint8_t enemycs[10] = {0};                              //------------------Init enemy checksum array
static uint8_t shoot_weights[10] = {0};                        //------------------Init weights for shooting logic array

static uint8_t field_msgs[10*10] = {0};                        //------------------Init array for holding enemy field message
static uint8_t field_msgs_count = 0;

static uint8_t hit_mycs_count = 0;                             //------------------How often they hit highest column in mycs in a row
typedef struct{
    uint8_t row;
    uint8_t col;
}Shot;

Shot find_next_shot(uint8_t* enemyboard);
Shot find_next_shot_dumb(uint8_t* enemyboard);
uint8_t are_adjacent_hits(uint8_t* enemyboard, uint8_t row, uint8_t col);
uint8_t find_longest_available_ship(uint8_t* board);

static Shot last_shot = {0, 0};

#ifdef DEBUG_HTERM
char st_ch = 'x';
#else
char st_ch = '\n';
#endif

int main(void){
    // Configure the system clock to 48MHz
    EPL_SystemClock_Config();
    UART1_config();
    UART2_config();

    // Enable the GPIOC peripheral clock
    RCC->AHBENR |= RCC_AHBENR_GPIOCEN;
    blue_button_config();
    GPIO_OUTPUT_CONFIG(GPIOA, 5);

    initializeSM();
    clear_UART_RX_buffer();

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
    //reset_game();


    int msg_status = receive_msg_with_certain_prefix("START"); //----------------msg_status = 0 if not received, 1 if received, 2 if invalid message

    if(msg_status == 1){
        curr_state = STARTS2;
        srand(seed);
    }else if(msg_status == 2){
        LOG("Invalid Message\n");
    }

    // If the blue button is pressed, my yConti becomes S1
    if(!(GPIOC->IDR & GPIO_IDR_13)){

        #ifdef REAL_OPPONENT
        SEND("START52216067\n");
        LOG("START message sent\n");
        #else
        LOG("START52216067\n");
        #endif

        srand(seed);
        curr_state = STARTS1;
    }

    if (seed < 250){                                    //------------------Seed for randomness, increased until moving to the next state
        seed++;
    }else{
        seed = 0;
    }
}

//-----------------StartS1 State
void StartS1(void){

    player1 = 1;
    int msg_status = receive_msg_with_certain_prefix("CS"); //----------------msg_status = 0 if not received, 1 if received, 2 if invalid message

    if(msg_status == 1){
        for (int i = 0; i < 10; i++){
            enemycs[i] = rx_buffer[i+2] - '0';    //------------------Save the checksum of the enemy
            shoot_weights[i] = enemycs[i];
        }
        curr_state = STARTS1_SEND_CS;                           //-----------------Change the checksum received flag    
    }else if(msg_status == 2){
        LOG("Invalid Message, expected CS\n");
    }

}

void StartS1_send_CS(void){
    #ifdef DEBUG_SET_LAYOUT
    place_boats_standard(myboard);
    #else
    place_boats_randomly(myboard);                        //------------------Place my boats randomly
    #endif

    char own_checksum[14];                            //------------------Init checksum
    calculate_checksum(myboard, own_checksum);              //------------------Calculate own checksum

    for(int i = 0; i < 10; i++){
        myweights[i] = own_checksum[i+2] - '0';               //------------------Save my own checksum as ints
    }

    #ifdef REAL_OPPONENT
    SEND(own_checksum);
    LOG("CS sent\n");
    #else
    LOG("%s", own_checksum);                             //------------------Send own checksum
    #endif

    curr_state = STARTS1_WAIT_START;                     //------------------Change the state, wait for Start_msg from S2
}

void StartS1_wait_Start(void){
    
    int msg_status = receive_msg_with_certain_prefix("START"); //----------------msg_status = 0 if not received, 1 if received, 2 if invalid message

    if(msg_status == 1){
        //parse_message(rx_buffer);
        curr_state = OFFENSE_SHOOT;                       //-----------------Change the state, going to Offense

        #ifdef DEBUG
        //LOG("successfully read Startmsg");
        #endif
    }else if(msg_status == 2){
        LOG("Invalid Message, expected START\n");
    }
}


//-----------------StartS2 State
void StartS2(void){
    player1 = 0;
    #ifdef DEBUG_SET_LAYOUT
    place_boats_standard(myboard);
    #else
    place_boats_randomly(myboard);                        //------------------Place my boats randomly
    #endif

    char own_checksum[14];                                  //------------------Init checksum
    calculate_checksum(myboard, own_checksum);              //------------------Calculate own checksum

    for(int i = 0; i < 10; i++){
        myweights[i] = own_checksum[i+2] - '0';               //------------------Save my own checksum as ints
    }

    #ifdef REAL_OPPONENT
    SEND(own_checksum);
    LOG("CS sent\n");
    #else
    LOG("%s", own_checksum);                             //------------------Send own checksum
    #endif

    curr_state = STARTS2_WAIT_CS;                     //------------------Change the state, wait for Start_msg from S2
}

void StartS2_wait_CS(void){
    
    int msg_status = receive_msg_with_certain_prefix("CS"); //----------------msg_status = 0 if not received, 1 if received, 2 if invalid message

    if(msg_status == 1){
        for (int i = 0; i < 10; i++){
            enemycs[i] = rx_buffer[i+2] - '0';    //------------------Save the checksum of the enemy
            shoot_weights[i] = enemycs[i];
        }
        curr_state = STARTS2_SEND_START;                           //-----------------Change the checksum received flag
    }else if(msg_status == 2){
        LOG("Invalid Message, expected CS\n");
    }
}

void StartS2_send_Start(void){
    #ifdef REAL_OPPONENT
    SEND("START52216067\n");
    LOG("START message sent\n");
    #else
    LOG("START52216067\n");
    #endif

    curr_state = DEFENSE;                      //-----------------Change the state, going to Defense
}

//-----------------Offense State

// enemyboard legend: 0-->did not shoot there already, 1-->shot there already, 2-->hit

void Offense_shoot(void){
    #ifdef DEBUG_LVL_2
    LOG("in Offense_shoot\n");
    #endif

    gameiterations++;

    char shoot_msg[8] = {'B', 'O', 'O', 'M', '0', '0', '\n', '\0'};
    
    Shot next_shot = find_next_shot(enemyboard);
    //Shot next_shot = find_next_shot_dumb(enemyboard);

    shoot_msg[4] = '0' + next_shot.col;
    shoot_msg[5] = '0' + next_shot.row;

    #ifdef REAL_OPPONENT
    SEND(shoot_msg);
    LOG("I shot at %d, %d\n", next_shot.row, next_shot.col);
    #else
    LOG("%s", shoot_msg);
    #endif

    enemyboard[next_shot.row*10 + next_shot.col] = 1;
    last_shot = next_shot;
    curr_state = OFFENSE_WAIT;            //------------------Change the state, going to waiting for response
        
}

void Offense_wait(void){
    #ifdef DEBUG_LVL_3
    //LOG("in Offense_wait\n");
    #endif
    if(USART2->ISR & USART_ISR_RXNE){
        


        char received_char = USART2->RDR;
        if(received_char == st_ch){                          //---------?--------If too much time has passed, CHEATER-State?
            rx_buffer[rx_index] = '\0';

            if(rx_buffer[0] == 'T'){
                #ifdef REAL_OPPONENT
                LOG("I hit\n");
                #endif
                enemyboard[last_shot.row*10 + last_shot.col] = 2;    //------------------If hit, mark the enemy board
                shoot_weights[last_shot.col] -= 1;                  //------------------If hit successfully, decrease the weight of the column
                if (check_win_or_loss() == 1){
                    gamestatus = 1;                            //------------------If win, set the gamestatus to 1
                    curr_state = GAMEEND;                              //---------?---------Check if win or loss

                    #ifdef REAL_OPPONENT
                    LOG("I won\n")
                    #endif
                }else{
                    curr_state = DEFENSE;                                 //------------------Change the state, going to Defense
                }

            }else if(rx_buffer[0] == 'W'){
                #ifdef REAL_OPPONENT
                LOG("I missed\n");
                #endif
                enemyboard[last_shot.row*10 + last_shot.col] = 1;
                curr_state = DEFENSE;                                 //------------------Change the state, going to Defense
            }else if(rx_buffer[0]=='S'){
                for(int i = 0; i < 10; i++){
                    field_msgs[field_msgs_count*10 + i] = rx_buffer[i+4] - '0';
                }
                field_msgs_count++;
                curr_state = GAMEEND;
            }


            rx_index = 0;
        }else if (rx_index < BUFFERSIZE - 1){
            rx_buffer[rx_index++] = received_char;
        }        
    }
}


//-----------------Defense State
void Defense(void){
    
    gameiterations++;

    int msg_status = receive_msg_with_certain_prefix("BOOM");           //----------------msg_status = 0 if not received, 1 if received, 2 if invalid message

    if(msg_status == 1){
        Shot received_shot;
        received_shot.col = rx_buffer[4] - '0';                         //------------------Get the row of the shot
        received_shot.row = rx_buffer[5] - '0';                         //------------------Get the col of the shot

        uint8_t hits_on_me_count = get_hit_count(hits_on_me);           //------------------Get the number of hits on me

        if(myboard[received_shot.row*10 + received_shot.col] != 0){
            
            if(hits_on_me_count == 29){
                curr_state = GAMEEND;
            }else{
                #ifdef REAL_OPPONENT
                SEND("T\n");                                                //------------------If hit, send T
                LOG("They hit at %d, %d\n", received_shot.row, received_shot.col);
                #else
                LOG("T\n");
                #endif
            }

            hits_on_me[received_shot.row*10 + received_shot.col] = 1;   //------------------If hit, mark the hits_on_me board
            myweights[received_shot.col] -= 1;                          //------------------If hit successfully, decrease the weight of the column

        }else{
            #ifdef REAL_OPPONENT
            SEND("W\n");                                                //------------------If miss, send W
            LOG("They missed at %d, %d\n", received_shot.row, received_shot.col);
            #else
            LOG("W\n");                                                 //------------------If miss, send W
            #endif
        }

        #ifdef DEBUG_LVL_1
        LOG("i received a shot\n");
        #endif

        if (check_win_or_loss() == 2){
            gamestatus = 2;
            curr_state = GAMEEND;

            #ifdef REAL_OPPONENT
            LOG("I lost\n");
            #endif

        }else{
            curr_state = OFFENSE_SHOOT;
            #ifdef DEBUG_LVL_1
            LOG("now transitioning to shooting mode\n");
            #endif
        }
    }else if(msg_status == 2){
        LOG("Invalid Message, expected BOOMxx\n");
    }
}

void Send_SF(void){
    SEND_Board_messages();
    curr_state = WAIT_SF;
}

void Wait_SF(void){

    if(field_msgs_count < 10){
        int msg_status = receive_msg_with_certain_prefix("SF"); //----------------msg_status = 0 if not received, 1 if received, 2 if invalid message

        if(msg_status == 1){
            for (int i = 0; i < 10; i++){
            field_msgs[field_msgs_count*10 + i] = rx_buffer[i+4] - '0';
            }
            field_msgs_count++;
        }   
    }
    if(field_msgs_count == 10){
        curr_state = PROTOCOLERROR;
    }
}

void ProtocolError(void){
    #ifdef DEBUG_HTERM
    LOG("finished\n");
    #endif
    reset_game();
}

void GameEnd(void){

    #ifdef DEBUG_HTERM
    if(player1 == 1){LOG("Game ended, i am player 1\n")}else{LOG("Game ended, i am player 2\n")}
    #endif

    curr_state = SEND_SF;
    
}


//-----------------End of Statemachine functions-----------------


int msg_starts_with(const char* str, const char* prefix){
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

void parse_message(char *msg){
    LOG("%s\n", msg);
}

int receive_msg_with_certain_prefix(const char* prefix){ //----0 if not received, 1 if received, 2 if invalid message
    if(USART2->ISR & USART_ISR_RXNE){
        
        char received_char = USART2->RDR;
        if(received_char == st_ch){                          //---------?--------If too much time has passed, CHEATER-State?
            rx_buffer[rx_index] = '\0';

            if(msg_starts_with(rx_buffer, prefix)){
                rx_index = 0;
                return 1;
            }else{
                rx_index = 0;
                return 2;                   //---------?--------Invalid message, maybe CHEATER-State?
            }
        }else if (rx_index < BUFFERSIZE - 1){
            rx_buffer[rx_index++] = received_char;
        }        
    }
    return 0;
}

void clear_UART_RX_buffer(void) {
    // Check if the RXNE flag is set
    while (USART2->ISR & USART_ISR_RXNE) {
        // Read the RDR to clear the RXNE flag
        volatile char temp = USART2->RDR;
    }
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

    #ifdef DEBUG_NO_WIN_LOOSE
    return 0;
    #endif

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

    #ifdef DEBUG_ENDGAME_WIN
    hits_on_enemy_count = 30;
    #endif

    #ifdef DEBUG_ENDGAME_LOSS
    hits_on_me_count = 30;
    #endif

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

int get_hit_count(uint8_t* board){
    int hit_count = 0;
    for(uint8_t i = 0; i < 100; i++){
        if(board[i] == 1){
            hit_count++;
        }
    }
    return hit_count;

}

void SEND_Board_messages(void){
    for (int col = 0; col < 10; col++){
        
        char curr_msg[] = {'S', 'F', '0', 'D', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '\n', '\0'};
        curr_msg[2] += col;
        for (int row = 0; row < 10; row++){
            curr_msg[row+4] += myboard[row*10 + col];
        }
        #ifdef REAL_OPPONENT
        SEND(curr_msg);
        #else
        LOG("%s", curr_msg);
        #endif
    }
    #ifdef REAL_OPPONENT
    LOG("finished sending checksum\n");
    #endif
}

uint8_t is_valid_position(uint8_t col, uint8_t row, uint8_t direction, uint8_t size) {
    //---------------check if the boat ends p being out of bounds when placed, depending on location, orientation and size
    //---------------return 1 if planned location is ok, return 0 if not
    if (direction == 0) {
        if (col + size > 9) {
            return 0;
        }
    }
    else if (direction == 1) {
        if (row + size > 9) {
            return 0;
        }
    }
    else {
        return 1;
    }
}

uint8_t is_not_reserved(uint8_t col, uint8_t row, uint8_t direction, uint8_t size, uint8_t* res_slots) {
    //---------------check if planned placing spot is already reserved by other boats (leaving one field of space between)
    //---------------return 1 if planned location is ok, return 0 if it is already reserved
    if (direction == 0) {
        for (uint8_t i = 0; i < size; i++) {
            if (res_slots[row * 10 + col + i] == 1) {
                return 0;
            }
        }
    }
    else if (direction == 1) {
        for (uint8_t i = 0; i < size; i++) {
            if (res_slots[(row + i) * 10 + col] == 1) {
                return 0;
            }
        }
    }
    else {
        return 1;
    }
}

void place_boat_and_reserve(uint8_t col, uint8_t row, uint8_t direction, uint8_t size, uint8_t* res_slots, uint8_t* board) {
    //---------------called after checking location and reservations, places boat and reserves space 1 tile around it
    if (direction == 0) {
        for (int i = 0; i < size; i++) {
            board[row * 10 + col + i] = size;
            //res_slots[row*10 + col + i] = 1;

            for (int c = col - 1; c <= col + size; c++) {
                for (int r = row - 1; r <= row + 1; r++) {
                    if (c >= 0 && c < 10 && r >= 0 && r < 10) {
                        res_slots[r * 10 + c] = 1;
                    }
                }
            }
        }
    }
    else if (direction == 1) {
        for (int i = 0; i < size; i++) {
            board[(row + i) * 10 + col] = size;
            //res_slots[(row + i)*10 + col] = 1;

            for (int c = col - 1; c <= col + 1; c++) {
                for (int r = row - 1; r <= row + size; r++) {
                    if (c >= 0 && c < 10 && r >= 0 && r < 10) {
                        res_slots[r * 10 + c] = 1;
                    }
                }
            }
        }
    }

}
void place_boats_randomly(uint8_t* board) {
    //---------------randomly place boats on the board, leaving one tile of space between them
    uint8_t res_fields[10 * 10] = { 0 };                                    //------------------array containing reservations
    uint8_t boat_sizes[] = { 5, 4, 4, 3, 3, 3, 2, 2, 2, 2 };                //------------------array of the boats needed to be placed
    uint8_t array_size = sizeof(boat_sizes) / sizeof(boat_sizes[0]);            //------------------calcing size of the array

    for (uint8_t boat = 0; boat < array_size; boat++) {
        uint8_t tries = 0;
        uint8_t placed = 0;

        while (tries < 300) {
            uint8_t row = rand() % 10;                                      //------------------generate random numbers for location and rotation
            uint8_t col = rand() % 10;
            uint8_t direction = rand() % 2;

            //------------------check if position is valid and if not in reserved spots
            if (is_valid_position(col, row, direction, boat_sizes[boat]) && is_not_reserved(col, row, direction, boat_sizes[boat], res_fields)) {
                place_boat_and_reserve(col, row, direction, boat_sizes[boat], res_fields, board); //------------------place the boat and reserve tiles around it
                placed = 1;
                break;
            }
            tries++;
        }
        if (!placed) {
            for (uint8_t i = 0; i < 100; i++) { board[i] = 0; }
            place_boats_standard(board); //------------------if no valid random placement was found, place standard layout
        } 
    }
}


void place_boats_standard(uint8_t* board){ //--------------fixed layout, used if no random layout found in several 
    place_boat(myboard, 0, 0, 5, 0);
    place_boat(myboard, 2, 0, 4, 0);
    place_boat(myboard, 1, 7, 4, 1);
    place_boat(myboard, 4, 2, 3, 0);
    place_boat(myboard, 6, 6, 3, 1);
    place_boat(myboard, 6, 8, 3, 1);
    place_boat(myboard, 9, 0, 2, 0);
    place_boat(myboard, 9, 3, 2, 0);
    place_boat(myboard, 7, 2, 2, 0);
    place_boat(myboard, 6, 0, 2, 0);
}

Shot find_next_shot(uint8_t* enemyboard){

    uint8_t longest_ship = find_longest_available_ship(enemyboard);
    #ifdef DEBUG_HTERM
    LOG("longest ship: %d\n", longest_ship);
    #endif

    int prefer_random = 0;
    int c = 0;
    int r = 0;

    for (int col = 0; col < 10; col++) {
        for (int row = 0; row < 10; row++) {
            prefer_random = 0;
            if (enemyboard[row * 10 + col] == 2) {
                // Check if there is a hit beside another hit, assume ship facing that way
                if (col + 1 < 10 && enemyboard[row * 10 + col + 1] == 2) {
                    prefer_random = 1;
                    c = col;
                    r = row;
                    while (c + 1 < 10 && enemyboard[r * 10 + c + 1] == 2) { c++; }
                    if (c + 1 < 10 && enemyboard[r * 10 + c + 1] == 0) { return (Shot){r, c + 1}; }
                }
                if (col - 1 >= 0 && enemyboard[row * 10 + col - 1] == 2) {
                    prefer_random = 1;
                    c = col;
                    r = row;
                    while (c - 1 >= 0 && enemyboard[r * 10 + c - 1] == 2) { c--; }
                    if (c - 1 >= 0 && enemyboard[r * 10 + c - 1] == 0) { return (Shot){r, c - 1}; }
                }
                if (row + 1 < 10 && enemyboard[(row + 1) * 10 + col] == 2) {
                    prefer_random = 1;
                    c = col;
                    r = row;
                    while (r + 1 < 10 && enemyboard[(r + 1) * 10 + c] == 2) { r++; }
                    if (r + 1 < 10 && enemyboard[(r + 1) * 10 + c] == 0) { return (Shot){r + 1, c}; }
                }
                if (row - 1 >= 0 && enemyboard[(row - 1) * 10 + col] == 2) {
                    prefer_random = 1;
                    c = col;
                    r = row;
                    while (r - 1 >= 0 && enemyboard[(r - 1) * 10 + c] == 2) { r--; }
                    if (r - 1 >= 0 && enemyboard[(r - 1) * 10 + c] == 0) { return (Shot){r - 1, c}; }
                }

                // Check to the right, left, down, and up if there is a hit
                if (prefer_random == 0) {
                    if (col + 1 < 10 && enemyboard[row * 10 + col + 1] == 0) {
                        return (Shot){row, col + 1};
                    } else if (col - 1 >= 0 && enemyboard[row * 10 + col - 1] == 0) {
                        return (Shot){row, col - 1};
                    } else if (row + 1 < 10 && enemyboard[(row + 1) * 10 + col] == 0) {
                        return (Shot){row + 1, col};
                    } else if (row - 1 >= 0 && enemyboard[(row - 1) * 10 + col] == 0) {
                        return (Shot){row - 1, col};
                    }
                }
            }
        }
    }

    //------------------If no hit was found, shoot with weighted randomness-------------------//

    //------calculate the total weight of the enemy board through checksum
    uint8_t chosen_col = 0;
    for (int i = 0; i < 10; i++){
        if (shoot_weights[i] > shoot_weights[chosen_col]){
            chosen_col = i;
        }
    }

    uint16_t tries = 0;
    while(tries < 500){
        uint8_t randrow = rand() % 10;
        if (enemyboard[randrow*10 + chosen_col] == 0){
            
            if(are_adjacent_hits(enemyboard, randrow, chosen_col) == 1){ //--------------if there have been hits in adjacent columns, shoot with 1/10 chance
                if (rand() % 30 == 0){
                    return (Shot){randrow, chosen_col};
                }
            }else{                                                       //--------------if there have not been hits in adjacent columns, shoot there
                return (Shot){randrow, chosen_col};
            }
        }
    }
    return find_next_shot_dumb(enemyboard);                              //--------------if no valid shot was found in 500 tries, shoot next in line
}

uint8_t are_adjacent_hits(uint8_t* enemyboard, uint8_t row, uint8_t col){
    if (col + 1 < 10 && enemyboard[row*10 + col + 1] == 2){
        return 1;
    }
    if (col - 1 >= 0 && enemyboard[row*10 + col - 1] == 2){
        return 1;
    }
    if (row + 1 < 10 && enemyboard[(row + 1)*10 + col] == 2){
        return 1;
    }
    if (row - 1 >= 0 && enemyboard[(row - 1)*10 + col] == 2){
        return 1;
    }
    return 0;
}

Shot find_next_shot_dumb(uint8_t* enemyboard){
    for (int col = 0; col < 10; col++){
        for (int row = 0; row < 10; row++){
            if(enemyboard[row*10 + col] == 0){
                return (Shot){row, col};
            }
        }
    }
    return (Shot){0, 0};
}


uint8_t find_longest_available_ship(uint8_t* board){
    uint8_t ship_list[] = {5, 4, 4, 3, 3, 3, 2, 2, 2, 2};       //--------------list of all ships
    uint8_t ship_length_v = 0;                                  //--------------length of the ship vertically
    uint8_t ship_length_h = 0;                                  //--------------length of the ship horizontally

    for(uint8_t col = 0; col < 10; col++){                      //--------------iterate through the board
        for(uint8_t row = 0; row < 10; row++){
            if(board[row*10 + col] == 2){                       //--------------look for vertically placed ships
                ship_length_v++;
            }else{
                for(uint8_t i = 0; i < 10; i++){
                    if(ship_list[i] == ship_length_v){          //--------------eliminate ship from list
                        ship_list[i] = 0;
                    }
                }
                ship_length_v = 0;
            }
            if(board[col*10 + row] == 2){                       //--------------look for horizontally placed ships
                ship_length_h++;
            }else{
                for(uint8_t i = 0; i < 10; i++){
                    if(ship_list[i] == ship_length_h){          //--------------eliminate ship from list
                        ship_list[i] = 0;
                        break;
                    }
                }
                ship_length_h = 0;
            }
        }
    }
    uint8_t longest_ship = 0;
    for(uint8_t i = 0; i < 10; i++){                    //--------------find longest ship left in the list
        if(ship_list[i] > longest_ship){
            longest_ship = ship_list[i];
        }
    }
    return longest_ship;
}

uint8_t is_enemy_cs_30(int* enemycs){
    int sum = 0;
    for (int i = 0; i < 10; i++){
        sum += enemycs[i];
    }
    if (sum == 30){
        return 1;
    }else{
        return 0;

        #ifdef CHEATING_LOG_ACTIVE
        LOG("Enemy CS sum does not equal 30\n");
        #endif

    }
}

uint8_t do_cs_and_hits_match(uint8_t* enemycs, uint8_t* enemyboard){ //--------------check if the checksum and the hits on the enemy board match, 1 if yes, 0 if cheater

    for (int col = 0; col < 10; col++) {
        uint8_t colhitsum = 0;
        uint8_t colwatersum = 0;

        for (int row = 0; row < 10; row++) {
            if (enemyboard[row * 10 + col] == 2) {
                colhitsum++;
            } else if (enemyboard[row * 10 + col] == 1) {
                colwatersum++;
            }
        }

        // If the hits exceed the checksum or the water tiles exceed the possible number of water tiles
        if (colhitsum > enemycs[col] || colwatersum > (10 - enemycs[col])) {

            #ifdef CHEATING_LOG_ACTIVE
            LOG("Checksum inconsistensies found\n");
            #endif

            return 0;  //------------Cheating detected, more boat- or more water-hits than checksum suggests
        }
    }
    return 1;  //--------All columns match
}

uint8_t more_than_100_iterations(void){
    if (gameiterations > 200){
        return 1;
    }else{
        return 0;
    }
}

uint8_t does_board_message_match(uint8_t* field_msgs, uint8_t* enemyboard){
    for(int col = 0; col < 10; col++){
        for(int row = 0; row < 10; row++){
            if (field_msgs[col*10 + row] >= 2 && enemyboard[row*10 + col] == 1){

                #ifdef CHEATING_LOG_ACTIVE
                LOG("Field message and board do not match\n");
                #endif

                return 0; //-------cheated, marked hit on field message but not on board
            }else{
                return 1;
            }
        }
    }
}

uint8_t they_using_my_cs(uint8_t* myweights, Shot their_last_shot){

    uint8_t my_highest_col = 0;
    for (uint8_t i = 0; i < 10; i++){
        if (myweights[i] > myweights[my_highest_col]){
            my_highest_col = i;
        }
    }
    if (their_last_shot.col == my_highest_col){
        hit_mycs_count++;
    }else{
        hit_mycs_count = 0;
    }

    if (hit_mycs_count >= 5){

        #ifdef CHEATING_LOG_ACTIVE
        LOG("They are probably using my checksum\n");
        #endif

        return 1;
    }else{
        return 0;
    }
}

void reset_game(void){              //--------------reset all variables to start a new game, not used, just press reset-button
    curr_state = IDLE;
    seed = 0;
    player1 = 0;
    gamestatus = 0;
    gameiterations = 0;
    rx_index = 0;
    field_msgs_count = 0;
    hit_mycs_count = 0;
    for ( int i = 0; i < 100; i++){
        myboard[i] = 0;
        enemyboard[i] = 0;
        hits_on_me[i] = 0;
        field_msgs[i] = 0;
    }
    for(int i = 0; i < 10; i++){
        myweights[i] = 0;
        shoot_weights[i] = 0;
        enemycs[i] = 0;
    }
    for (int i = 0; i < BUFFERSIZE; i++){
        rx_buffer[i] = 0;
    }

    clear_UART_RX_buffer();
}
