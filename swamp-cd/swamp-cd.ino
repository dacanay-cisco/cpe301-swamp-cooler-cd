//Cisco Dacanay
//Final Project
//5/9/23

#include <uRTCLib.h>
#include <dht.h>
#include <LiquidCrystal.h>
#include <Stepper.h>

#define TARGET_TEMP 22.0    //target temperature for swamp cooler (will turn on 1 deg above and off 1 deg below this number)
#define MONTH 5   //set month num
#define DAY 9   //set day num
#define YEAR 23   //set year 20XX
#define HOUR 12    //set hour num 0-24
#define MINUTE 0    //set minute num

volatile unsigned char *myUCSR0A = (unsigned char *)0x00C0;
volatile unsigned char *myUCSR0B = (unsigned char *)0x00C1;
volatile unsigned char *myUCSR0C = (unsigned char *)0x00C2;
volatile unsigned int  *myUBRR0  = (unsigned int *) 0x00C4;
volatile unsigned char *myUDR0   = (unsigned char *)0x00C6;

volatile unsigned char* port_b = (unsigned char*) 0x25;
volatile unsigned char* ddr_b  = (unsigned char*) 0x24;
volatile unsigned char* pin_b  = (unsigned char*) 0x23;
volatile unsigned char* port_l = (unsigned char*) 0x10B;
volatile unsigned char* ddr_l  = (unsigned char*) 0x10A;
volatile unsigned char* pin_l  = (unsigned char*) 0x109;
volatile unsigned char* port_d = (unsigned char*) 0x2B;
volatile unsigned char* ddr_d  = (unsigned char*) 0x2A;
volatile unsigned char* pin_d  = (unsigned char*) 0x29;

volatile unsigned char* my_EICRA = (unsigned char*) 0x69;
volatile unsigned char* my_EIMSK  = (unsigned char*) 0x3D;


volatile unsigned char* my_ADMUX = (unsigned char*) 0x7C;
volatile unsigned char* my_ADCSRB = (unsigned char*) 0x7B;
volatile unsigned char* my_ADCSRA = (unsigned char*) 0x7A;
volatile unsigned int* my_ADC_DATA = (unsigned int*) 0x78;

enum State {DISABLED = 0, ERROR = 1, IDLE = 2, RUNNING = 3};
volatile enum State currentState = IDLE;
volatile bool togglePower = 0;


dht DHT;
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);
Stepper stepper = Stepper(64, 12, 10, 11, 9);
uRTCLib rtc(0x68);

void setup() {
  //port setup
  //*ddr_b |= 0x01; //pb0 output (fan motor)
  //*ddr_b &= 0xFB; //pb2 input (stepper motor button)
  //*ddr_b &= 0xF7; //pb3 input (stepper motor button)
  //*ddr_b |= 0x10; //pb4 output (stepper motor)

  *ddr_b &= 0xF3; //pb2+3 input (stepper motor buttons)
  *ddr_b |= 0x11; //pb0+4 output (fan motor + stepper motor)
  *ddr_l |= 0x0F; //pl0 to pl3 output (state leds)  (pl0 = yellow, pl1 = red, pl2 = green, pl3 = blue)
  *ddr_d &= 0x08; //pd3 input (on/off)

  *my_EICRA |= 0xC0;  //set interrupt request on rising edge
  *my_EIMSK |= 0x08;  //enable pin interrupt on pd3 (int3)

  U0init(9600);
  adc_init();
  lcd.begin(16, 2);
  URTCLIB_WIRE.begin();
  rtc.set(0, MINUTE, HOUR, 2, DAY, MONTH, YEAR);

}



void loop() {
  if(togglePower) {
    if(currentState > DISABLED) {
      setState(DISABLED);
    }
    else {
      setState(IDLE);
    }
    togglePower = 0;
  }

  if(currentState == RUNNING) {
    if(!(*port_l & 0x08)) {
      *port_l |= 0x08;  //turn on blue led
      *port_l &= 0xF8;  //turn off other leds
    }

    //fan
    if(!(*port_b & 0x01)) {
      *port_b |= 0x01;  //pb0 high
    }

    int chk = DHT.read11(8); //read pin 8
    if(DHT.temperature < TARGET_TEMP - 1) {
      currentState = IDLE;
    }
  }

  if(currentState < RUNNING) {
    //fan
    if(*port_b & 0x01) {
      *port_b &= 0xFE;  //pb0 low
    }
  }
  
  if(currentState >= IDLE) {
    //water level
    *port_b |= 0x10;  //pb4 power sensor on to read input
    unsigned int adc_voltage = adc_read(0); //read channel 0
    *port_b &= 0xEF;  //pb4 plow
    //float voltage_float = adc_voltage * 5.0 / 1024.0; //convert 0-1023 to 0-5.0V
    int water_level = adc_voltage / 2;
    //U0putint(water_level);
    //U0putchar('\n');

    //Serial output
    //U0putVoltage(voltage_float);
    if(water_level < 50) {
      currentState = ERROR;
    }

    //dht
    int chk = DHT.read11(8); //read pin 8
    lcd.setCursor(0,0); 
    lcd.print("Temp: ");
    lcd.print(DHT.temperature);
    lcd.print((char)223);
    lcd.print("C");
    lcd.setCursor(0,1);
    lcd.print("Humidity: ");
    lcd.print(DHT.humidity);
    lcd.print("%");

    
  }

  if(currentState == IDLE) {
    if(!(*port_l & 0x04)) {
      *port_l |= 0x04;  //turn on green led
      *port_l &= 0xF4;  //turn off other leds
    }
    
    if(DHT.temperature > TARGET_TEMP + 1) {
      currentState = RUNNING;
    }
  }
  
  if(currentState == ERROR) {
    if(!(*port_l & 0x02)) {
      *port_l |= 0x02;  //turn on red led
      *port_l &= 0xF2;  //turn off other leds
    }

    lcd.clear();
    lcd.print("ERROR: Water Level Low");
    
    U0putStringLn("Water Level Low");
  }

  if(currentState == DISABLED) {
    if(!(*port_l & 0x01)) {
      *port_l |= 0x01;  //turn on red led
      *port_l &= 0xF1;  //turn off other leds
    }
  }

  //vent control (stepper motor)
  if(currentState > DISABLED) {
    if(*port_b & 0x0C) {}   //if both bottons pressed, do nothing
    else if(*pin_b & 0x04) {
      stepper.setSpeed(300);
      stepper.step(256);
    }
    else if(*pin_b & 0x08) {
      stepper.setSpeed(300);
      stepper.step(-256);
    }
  }


  //U0putint(currentState);


  
  //getClock();
  delay(1000);

}


ISR(INT3_vect) {  //interrupt for on/off
  togglePower = 1;  //power toggle at beginning of loop, not done directly in case interrupt happens right before another state change
}

void setState(enum State state) { //set state and print timestamp
  currentState = state;
  String stateString;
  switch(state) {
    case DISABLED:  //turn state into string for serial output
      stateString = "Disabled";
      break;
    case ERROR:
      stateString = "Error";
      break;
    case IDLE:
      stateString = "Idle";
      break;
    case RUNNING:
      stateString = "Running";
  }
  U0putString("Status: ");
  U0putString(stateString);
  U0putString(" @ ");
  getClock();
}

void getClock() {
  rtc.refresh();
  U0putint(rtc.hour());
  U0putchar(':');
  if(rtc.minute() < 10) { //display :00 instead of :0
    U0putchar('0');
  }
  U0putint(rtc.minute());
  U0putchar(' ');
  U0putint(rtc.month());
  U0putchar('/');
  U0putint(rtc.day());
  U0putchar('/');
  U0putint(rtc.year());

  U0putchar('\n');
}


void U0init(unsigned long U0baud)
{
 unsigned long FCPU = 16000000;
 unsigned int tbaud;
 tbaud = (FCPU / 16 / U0baud - 1);
 // Same as (FCPU / (16 * U0baud)) - 1;
 *myUCSR0A = 0x20;
 *myUCSR0B = 0x18;
 *myUCSR0C = 0x06;
 *myUBRR0  = tbaud;
}

unsigned char U0kbhit() // Read USART0 RDA status bit and return non-zero true if set
{
  if(*myUCSR0A & 0x80) {
    return 1;
  }
  else {
    return 0;
  }
}

unsigned char U0getchar() // Read input character from USART0 input buffer
{
  return *myUDR0;
}

void U0putchar(unsigned char U0pdata) // Wait for USART0 TBE to be set then write character to transmit buffer
{
  while(!(*myUCSR0A & 0x20)) {};
  *myUDR0 = U0pdata;
}

void U0putString(String U0string) {
  for(int i = 0; i < U0string.length(); i++) {
    U0putchar(U0string[i]);
  }
}

void U0putStringLn(String U0string) {
  for(int i = 0; i < U0string.length(); i++) {
    U0putchar(U0string[i]);
  }
  U0putchar('\n');
}

void U0putint(int U0int) {
  int temp = U0int;
  unsigned char U0pdata;
  int digits = 1, power = 1;
  while(temp > 9) {
    temp /= 10;
    digits++;
    power *= 10;
  }
  temp = U0int;
  unsigned char value;
  for(int i = digits; i > 0; i--) {
    value = (temp / power) + '0';
    U0putchar(value);
    temp = temp % power;
    power /= 10;
  }
}

void U0putVoltage(float voltage_float) {
  unsigned char voltage_char[4];
  float_to_char(voltage_float, voltage_char);

  for(int i = 0; i < 4; i++) {
    U0putchar(voltage_char[i]);
  }
  U0putchar('\n');
}

void adc_init()
{
  // setup the A register
  *my_ADCSRA |= 0b10000000; // set bit   7 to 1 to enable the ADC
  *my_ADCSRA &= 0b11011111; // clear bit 6 to 0 to disable the ADC trigger mode
  *my_ADCSRA &= 0b11110111; // clear bit 5 to 0 to disable the ADC interrupt
  *my_ADCSRA &= 0b11111000; // clear bit 0-2 to 0 to set prescaler selection to slow reading
  // setup the B register
  *my_ADCSRB &= 0b11110111; // clear bit 3 to 0 to reset the channel and gain bits
  *my_ADCSRB &= 0b11111000; // clear bit 2-0 to 0 to set free running mode
  // setup the MUX Register
  *my_ADMUX  &= 0b01111111; // clear bit 7 to 0 for AVCC analog reference
  *my_ADMUX  |= 0b01000000; // set bit   6 to 1 for AVCC analog reference
  *my_ADMUX  &= 0b11011111; // clear bit 5 to 0 for right adjust result
  //*my_ADMUX  |= 0b00100000; // left adjust
  *my_ADMUX  &= 0b11100000; // clear bit 4-0 to 0 to reset the channel and gain bits
}

unsigned int adc_read(unsigned char adc_channel_num)
{
  // clear the channel selection bits (MUX 4:0)
  *my_ADMUX  &= 0b11100000;
  // clear the channel selection bits (MUX 5)
  *my_ADCSRB &= 0b11110111;
  // set the channel number
  if(adc_channel_num > 7)
  {
    // set the channel selection bits, but remove the most significant bit (bit 3)
    adc_channel_num -= 8;
    // set MUX bit 5
    *my_ADCSRB |= 0b00001000;
  }
  // set the channel selection bits
  *my_ADMUX  += adc_channel_num;
  // set bit 6 of ADCSRA to 1 to start a conversion
  *my_ADCSRA |= 0x40;
  // wait for the conversion to complete
  while((*my_ADCSRA & 0x40) != 0);
  // return the result in the ADC data register
  return *my_ADC_DATA;
}

void float_to_char(float num_float, unsigned char num_char[]) { //quick function to output float (1.23) through serial
  int digit2 = num_float / 1;
  num_char[0] = digit2 + '0';
  num_char[1] = '.';
  int digit1 = (int(num_float * 10)) % 10;
  num_char[2] = digit1 + '0';
  int digit0 = (int(num_float * 100)) % 10;
  num_char[3] = digit0 + '0';
}