//control a NORITAKE ITRON GU20X8 20x8 pixel bi-color VFD
//and a Futaba T202MD15AA serial VFD display using an 8051
//

#include <8051.h> 
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define CPU_FREQ        11059200UL //Hz
#define PRESCALER       12
#define PERIOD          1 //milliseconds
#define MS_PER_SEC      1000
#define TIMER_CLOCKS    ((((CPU_FREQ)/(PRESCALER))*(PERIOD))/(MS_PER_SEC))
#define TIMER_RES       65536 //16 bits

//set your baud rate here (9600 baud is the rate the serial VFD uses)
#define BAUD            9600L
//calculates the timer high bits setting for the desired baud rate
#define UART_TIMER_BITS (256 - (CPU_FREQ)/((PRESCALER)*(32)*(BAUD)))
//20 space characters, equivalent to one line on the serial VFD screen
#define SPACE_20_CHAR   "                    "

//the ports for the display address and data lines
#define DISP_ADDR_PORT  P1
#define DISP_DATA_PORT  P2
//other display control signal pins
#define DISP_CD_PIN     P1_6
#define DISP_EN_PIN     P1_7
#define DISP_WR_PIN     P3_6
#define DISP_RST_PIN    P3_7
#define DISP_BUSY_PIN   P0_7
//the addresses in the display's memory for the buffers and control
#define DISP_BLUE_BASE_ADDR     0x00
#define DISP_RED_BASE_ADDR      0x20
#define DISP_CONTROL_ADDR       0x3F
//the brightness setting values to use in control mode
#define BRIGHTNESS_LOWEST   0x00
#define BRIGHTNESS_LOW      0x01
#define BRIGHTNESS_HIGH     0x02
#define BRIGHTNESS_HIGHEST  0x03
//the pixel dimensions of the display
#define DISP_ROWS   8
#define DISP_COLS   20

//LED pins for debugging purposes
#define DEBUG_LED_PIN       P3_5
#define LIFE_BLINKER_PIN    P3_4

//the message to be displayed on the screen
const char msg_str[] =  "~*~*~* HAPPY *~*~*~ "
                        " INDEPENDENCE DAY!  ";
//string full of spaces to fill the screen
const char screen_blank[] = SPACE_20_CHAR 
                            SPACE_20_CHAR;
//character buffer for our screen
char out_screen_buff[41];

//variables for the "life blinker" interrupt routine
volatile uint32_t clocktime = 0;
volatile uint8_t toggle_bit = 0x01;

//bitmaps for the blue and red parts of the image (an American Flag)
const uint8_t blue_image[DISP_COLS] = {
    0b10101111,
    0b10101111,
    0b10101111,
    0b10101111,
    0b10101111,
    0b10101111,
    0b10101111,
    0b10101111,
    0b10101010,
    0b10101010,
    0b10101010,
    0b10101010,
    0b10101010,
    0b10101010,
    0b10101010,
    0b10101010,
    0b10101010,
    0b10101010,
    0b10101010,
    0b10101010,
};

const uint8_t red_image[DISP_COLS] = {
    0b11111010,
    0b11110101,
    0b11111010,
    0b11110101,
    0b11111010,
    0b11110101,
    0b11111010,
    0b11110101,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
};

//UART functions
void UART_Init(void);
char UART_RxChar(void);
void UART_TxChar(char ch);
void put_message(const char * in_str);

//function to initialize the "life blinker"
void init_life_blinker(void); 

//basic delay functions
void short_delay(void);
void tiny_delay(void);

//functions to control the graphical 8x20 VFD
void display_write(uint8_t addr, uint8_t data);
void display_control(uint8_t addr, uint8_t data);
void display_init(void);
void display_write_addr(uint8_t addr);
void display_write_data(uint8_t data);
void display_set_brightness(uint8_t brightness);
void display_enable(bool enable);
void display_write_buff(uint8_t base_addr, const uint8_t * data_buff);
void display_wait_busy(void);

//function to initialize the "life blinker"
void init_life_blinker(void){
    //set the timer bits and enable the interrupt
    
    //high bits
    TH0 = (TIMER_RES - TIMER_CLOCKS) >> 8;
    //low bits
    TL0 = (TIMER_RES - TIMER_CLOCKS) & 0x00ff;
    TMOD = 0x01; //set mode
    EA = 1;
    ET0 = 1;
    TR0 = 1;
}

//puts a message out to our 2x20 character serial VFD 
void put_message(const char * in_str){
    uint8_t i;
    //copy in the blank string to the buffer
    strcpy(out_screen_buff, screen_blank);
    //copy in our message string
    memcpy(out_screen_buff, in_str, strlen(in_str));
    //go through the characters and write them out via the UART
    for(i = 0; out_screen_buff[i] != '\0'; i++){
        short_delay();// add a little delay for a nice write out effect
        UART_TxChar(out_screen_buff[i]); // send out the character 
    }
}

void UART_Init(void){
    SCON = 0x50; //async, 8bit data and 1 stop bit
    PCON |= 0x08;
    TMOD |= 0x20; //timer1 in mode 2
    //set the high bits of the timer 1 register to generate our baud rate
    TH1 = (uint8_t)UART_TIMER_BITS;
    TR1 = 1; //turn on timer
    TI = 1; //set before first write
}

void UART_TxChar(char ch){
    SBUF = ch;// load data to transfer
    while(T1==0); //wait till done transmitting
    TI = 0; // clear the Tx flag for next cycle
}

char UART_RxChar(void){
    while(RI==0); // wait until received
    RI=0; //clear receive interrupt flag for next cycle
    return(SBUF); ///return the char
}

void display_set_brightness(uint8_t brightness){
    //set the brightness using the control functionality
    //set for control (set to 0)
    DISP_CD_PIN = 0;
    //put out the control address
    display_write_addr(DISP_CONTROL_ADDR);
    //put out the brightness value to the data
    display_write_data(brightness);
    //toggle write pin down and up
    DISP_WR_PIN = 0;
    //tiny delay
    tiny_delay();
    //bring the pin back up
    DISP_WR_PIN = 1;
    //wait to finish
    display_wait_busy();
}

void display_wait_busy(void){
    //wait for the busy pin to go low
    while(DISP_BUSY_PIN != 0);
}

void display_write(uint8_t addr, uint8_t data){
    //write data to an address in the display
    //set for data (set to 1)
    DISP_CD_PIN = 1;
    //put out the address
    display_write_addr(addr);
    //put out the data
    display_write_data(data);
    //toggle write pin down and up
    DISP_WR_PIN = 0;
    //tiny delay
    tiny_delay();
    //bring the pin back up
    DISP_WR_PIN = 1;
    //wait to finish
    display_wait_busy();
}

void display_write_buff(uint8_t base_addr, const uint8_t * data_buff){
    //writes an array buffer to the display starting at a base address 
    uint8_t inc; //tmp
    for(inc = 0; inc < DISP_COLS; inc++){
        //write to the addresses 
        display_write(base_addr + inc, data_buff[inc]);
    }
}

void tiny_delay(void){
    //do a tiny delay
    uint8_t i = 30;
    while(i--);
}

void display_enable(bool enable){
    //set the enable pin accordingly
    DISP_EN_PIN = (enable) ? 1 : 0;
}

void display_write_addr(uint8_t addr){
    uint8_t tmp_port;
    tmp_port = DISP_ADDR_PORT & 0b11000000; //get a copy w/ the upper bits
    //write the temp value but with the address bits OR'd in, to the port
    //(address must be 6 bits)
    DISP_ADDR_PORT = tmp_port | addr;
}

void display_write_data(uint8_t data){
    //just put the data on the port
    DISP_DATA_PORT = data;
}

void display_init(void){
    //init the display pins and the display itself
    //bring busy pin high, to enable as an input
    DISP_BUSY_PIN = 1;

    //reset the display
    DISP_RST_PIN = 0;
    //insert a small toggle delay
    tiny_delay();
    //undo reset 
    DISP_RST_PIN = 1;

    //wait for it
    display_wait_busy();

    //set WR pin high,  as falling edge is the trigger later on
    DISP_WR_PIN = 1;
    //initialize CD to control mode (set to 0)
    DISP_CD_PIN = 0;

    //disable display at first
    display_enable(false);
    //insert a small toggle delay
    tiny_delay();
    //reenable display
    display_enable(true);

    //wait for it to finish setup
    display_wait_busy();
}


void short_delay(void){
    //a short delay for stuff
    uint16_t j;
    for(j=0;j<3000;j++); //delay
}


void main(void){
    //wait a bit to let the displays start up
    uint8_t j = 5;
    while(j--){
        short_delay();
    }

    //init life blinker to show we are alive :)
    init_life_blinker();

    //init uart
    UART_Init();

    //init the display
    display_init();
    //set the brightness
    display_set_brightness(BRIGHTNESS_HIGH);

    //write the buffers to the display
    //blue
    display_write_buff(DISP_BLUE_BASE_ADDR, blue_image);
    //red
    display_write_buff(DISP_RED_BASE_ADDR, red_image);

    //put out the message to the serial display
    put_message(msg_str);

    //loop and blink debug led to show we finished
    uint16_t i;
    while(1){
        DEBUG_LED_PIN = 0; //turn on LED (active low)
        for(i=0;i<30000;i++); //delay
        DEBUG_LED_PIN = 1; //turn off LED
        for(i=0;i<30000;i++); //delay
    }
}

//the Timer 0 interrupt service routine
//used for our "life blinker"
void clockinc_Timer0_ISR(void) __interrupt 1 {
    //set the clock bits again 
    //high bits
    TH0 = (uint32_t)(TIMER_RES - TIMER_CLOCKS) >> 8;
    //low bits
    TL0 = (uint32_t)(TIMER_RES - TIMER_CLOCKS) & 0x00ff;

    //increment time
    clocktime++;

    if(clocktime >= MS_PER_SEC){
        toggle_bit ^= 0x01; 
        LIFE_BLINKER_PIN = toggle_bit;
        clocktime = 0;
    }
    //set the bits again     
    TF0 = 0;
    TR0 = 1;
}
