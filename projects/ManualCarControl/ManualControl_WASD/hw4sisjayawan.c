/**************************************************
* CMPEN 473, Spring 2020, Penn State University
* 
* Homework 4
* Revision V1.1
* On 02/12/20
* By Steven Sisjayawan
* 
* Purpose: Created a program to run motors for a moving car vehicle with
* associated keyboard inputs. Functionalities including moving forward,
* backward, faster, slower and left/right are included within this project.
* 
* 
* Program information: Utilized pwm_threads_3 given to us as a sample program
* for pwm utilizing the built-in hardware within a Rpi3 board. Utilized multi-
* threading in this program for the speed of the motors, direction of the motors
* and finally the keyboard inputs entered by user. Adjusted already created structs
* to follow the functionalities of this homework that were specified.
* 
***************************************************/


#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <termios.h>
#include <fcntl.h>
#include "import_registers.h"
#include "cm.h"
#include "gpio.h"
#include "spi.h"
#include "pwm.h"
#include "io_peripherals.h"
#include "enable_pwm_clock.h"

#define PWM_RANGE 32

struct done_flag
{
  pthread_mutex_t lock;
  bool            done;
};

struct option_flag
{
  pthread_mutex_t lock;
  char            option;
};

struct level_flag
{
  pthread_mutex_t lock;
  int             level;
  bool            pause;    //Boolean variable to check whether the stop instruction 's' is utilized    
};

struct speed_flag
{
  pthread_mutex_t lock;
  int             speed;
  bool            increment;    //Boolean variable to check if increment of speed or decrement of speed is necessary
  bool            turn;   //Boolean variable to check if user wants to turn either left/right
};

struct thread_parameter
{
  volatile struct gpio_register * gpio;
  volatile struct pwm_register  * pwm;
  int                             pin;
  struct done_flag *              done;
  struct option_flag *            option;
  struct level_flag *             DLevel;   //Used only for the direction of the motors
  struct speed_flag *             speed;    //Used specifically to adjust the speed of motors
};

struct key_thread_parameter
{
  struct done_flag *  done;
  struct option_flag * option;
  struct level_flag *  DLevel3;
  struct level_flag *  DLevel4;
  struct level_flag *  DLevel5;
  struct level_flag *  DLevel6;
  struct speed_flag *  speed1;
};

int  Tstep = 50;  /* PWM time resolution, number used for usleep(Tstep) */
int  Tlevel = 1;  /* repetition count of each light level, eg. repeat 12% light level for 1 time. */
                  //Tlevel of 1 used to adjust from forward to backward in around 1sec.

//Dimming function
void DimLevUnit(int Level, int pin, volatile struct gpio_register *gpio)
{
  int ONcount, OFFcount;

  ONcount = Level;
  OFFcount = 100 - Level;

  /* create the output pin signal duty cycle, same as Level */
  GPIO_SET( gpio, pin ); /* ON LED at GPIO 18 */
  while (ONcount > 0)
  {
    usleep( Tstep );
    ONcount = ONcount - 1;
  }
  GPIO_CLR( gpio, pin ); /* OFF LED at GPIO 18 */
  while (OFFcount > 0)
  {
    usleep( Tstep );
    OFFcount = OFFcount - 1;
  }
}

//Control the direction of motor
//Only pins 18,19,22 and 23
void *ThreadSW( void * arg )
{
  int                       Timeu;      /* dimming repetition of each level */
  struct thread_parameter * parameter = (struct thread_parameter *)arg;

  pthread_mutex_lock( &(parameter->done->lock) );
  while (!(parameter->done->done))
  {
    pthread_mutex_unlock( &(parameter->done->lock) );

    switch (parameter->option->option)
    {
      //Quit case
      case 'q':
        pthread_mutex_lock(&(parameter->done->lock));
        parameter->done->done=true;
        pthread_mutex_unlock(&(parameter->done->lock));
        break;
      
      //Initial case  
      case 'a':
        pthread_mutex_lock( &(parameter->option->lock));
        pthread_mutex_unlock( &(parameter->option->lock));
        break;
      
      //Stop case  
      case 's':
        //If level<50%, increment up to 50%
        pthread_mutex_lock( &(parameter->DLevel->lock));
        
          if (parameter->DLevel->level < 50)
          {
            while(parameter->DLevel->level<50)
            {
              Timeu = Tlevel;   /* repeat the dimming level for 1 times if Tlevel = 1 */
              while(Timeu>0)  
              {
                DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio); 
                Timeu = Timeu - 1;
              }
              parameter->DLevel->level = parameter->DLevel->level + 1;
            }
          }
          //If level>50%, increment down to 50%
          else
          {
            while(parameter->DLevel->level>50)
            {
              Timeu = Tlevel;   /* repeat the dimming level for 1 time if Tlevel = 1 */
              while(Timeu>0)
              {
                DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio);
                Timeu = Timeu - 1;
              }
              parameter->DLevel->level = parameter->DLevel->level - 1;
            }
          }
          
          parameter->DLevel->pause = true;
          
        pthread_mutex_unlock( &(parameter->DLevel->lock));
        
        break;
        
      case 'f':
        //Dim up to 100% for pin18 & pin 22
        if (parameter->pin == 18 || parameter->pin == 22)
        {
          pthread_mutex_lock( &(parameter->DLevel->lock));
          //If level less than 50, make sure to dim gradually upwards
          if (parameter->DLevel->level < 50)
          {
            while(parameter->DLevel->level<100)
            {
              Timeu = Tlevel;   /* repeat the dimming level for 1 times if Tlevel = 1 */
              while(Timeu>0)
              {
                DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio);
                Timeu = Timeu - 1;
              }
              parameter->DLevel->level = parameter->DLevel->level + 1;
            }
          }
          //If level already greater than or equal to 50, set level=100 immediately
          //So that the time it takes to move forward is not too long
          else
          {
            printf("forward\n");
            parameter->DLevel->level=100;
            DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio);
          }
          pthread_mutex_unlock( &(parameter->DLevel->lock));
        }
        else
        //Dim down for pin19 & pin23
        {
          pthread_mutex_lock( &(parameter->DLevel->lock));
          if (parameter->DLevel->level > 50)
          {
            while(parameter->DLevel->level>0) 
            {
              Timeu = Tlevel;   /* repeat the dimming level for 1 times if Tlevel = 1 */
              while(Timeu>0)
              {
                DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio);
                Timeu = Timeu - 1;
              }
              parameter->DLevel->level = parameter->DLevel->level - 1;
            }
          }
          else
          {
            printf("forward\n");
            parameter->DLevel->level=0;
            DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio);
          }
          
          pthread_mutex_unlock( &(parameter->DLevel->lock));
        }
        
        //make sure to set parameter->..->pause will be used later for increase/decrease speed
        //Set to false so that the car will increase in speed while moving forward
        parameter->DLevel->pause = false;
        
        break;
        
      //Backwards case  
      //Same implementation as forward, but just pins 19 and 23 instead of pins 18 and 22 that are active
      case 'b':
        if (parameter->pin == 19 || parameter->pin == 23)
        {
          pthread_mutex_lock( &(parameter->DLevel->lock));
          if (parameter->DLevel->level < 50)
          {
            while(parameter->DLevel->level<100) 
            {
              Timeu = Tlevel;   /* repeat the dimming level for 1 times if Tlevel = 1 */
              while(Timeu>0)
              {
                DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio); 
                Timeu = Timeu - 1;
              }
              parameter->DLevel->level = parameter->DLevel->level + 1;
            }
          }
          else
          {
            printf("backward\n");
            parameter->DLevel->level=100;
            DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio);
          }
          pthread_mutex_unlock( &(parameter->DLevel->lock));
        }
        else
        //Dim down pins 18 and 22
        {
          pthread_mutex_lock( &(parameter->DLevel->lock));
          if (parameter->DLevel->level > 50)
          {
            while(parameter->DLevel->level>0) 
            {
              Timeu = Tlevel;   /* repeat the dimming level for 1 times if Tlevel = 1 */
              while(Timeu>0)  
              {
                DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio); 
                Timeu = Timeu - 1;
              }
              parameter->DLevel->level = parameter->DLevel->level - 1;
            }
          }
          else
          {
            printf("backward\n");
            parameter->DLevel->level=0;
            DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio);
          }
          
          pthread_mutex_unlock( &(parameter->DLevel->lock));
        }
        
        parameter->DLevel->pause = false;
        
        break;
        
      //Increase speed_Direction of motor case  
      case 'i':
        pthread_mutex_lock(&(parameter->DLevel->lock));
        
        //Check whether the car is in stop setting or not through (parameter->DLevel->pause)
        if(parameter->DLevel->pause == true)
        {
          //If paused make sure to set dim level as 0
          DimLevUnit(0, parameter->pin, parameter->gpio);
        }
        else
        {
          //If not make sure to set dim level based on direction of motor
          DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio);
        }
        
        pthread_mutex_unlock(&(parameter->DLevel->lock));
        break;
        
      //Decrease speed_Direction of motor
      //same implementation as increase speed_direction of motor  
      case 'j':
        pthread_mutex_lock(&(parameter->DLevel->lock));
        
        if(parameter->DLevel->pause == true)
        {
          DimLevUnit(0, parameter->pin, parameter->gpio);
        }
        else
        {
          DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio);
        }
        
        pthread_mutex_unlock(&(parameter->DLevel->lock));
        break;
        
      //Left and right functionalities will still have the same direction of motors  
      case 'l':
        pthread_mutex_lock(&(parameter->DLevel->lock));
        DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio);
        pthread_mutex_unlock(&(parameter->DLevel->lock));
        break;
        
      case 'r':
        pthread_mutex_lock(&(parameter->DLevel->lock));
        DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio);
        pthread_mutex_unlock(&(parameter->DLevel->lock));
        break;
        
      default:
        break;
    }

    pthread_mutex_lock( &(parameter->done->lock) );
  }
  pthread_mutex_unlock( &(parameter->done->lock) );

  return 0;
}

//Contrls speed of motor
//Only using Pin12 -> left motor speed
//Pin13 -> Right motor speed
void *ThreadHW( void * arg )
{
  int                       Timeu;      /* dimming repetition of each level */
  struct thread_parameter * parameter = (struct thread_parameter *)arg;

  pthread_mutex_lock( &(parameter->done->lock) );
  while (!(parameter->done->done))
  {
    pthread_mutex_unlock( &(parameter->done->lock) );

    switch (parameter->option->option)
    {
      //Cases already specified in the above thread function ThreadSW
      case 'q':
      //speed set to 0
        pthread_mutex_lock( &(parameter->option->lock));
        parameter->pwm->DAT1 = 0;
        parameter->pwm->DAT2 = 0;
        
        pthread_mutex_lock( &(parameter->done->lock));
        parameter->done->done = true;
        pthread_mutex_lock( &(parameter->done->lock));
        pthread_mutex_unlock( &(parameter->option->lock));
        break;
        
      case 'a':
      //speed set to 0  
        pthread_mutex_lock( &(parameter->option->lock));
        parameter->pwm->DAT1 = 0;
        parameter->pwm->DAT2 = 0;
        pthread_mutex_unlock( &(parameter->option->lock));
        break;
        
      case 's':
      //Save speed as soon as user inputs s and keep speed
        parameter->pwm->DAT1 = parameter->pwm->DAT1;
        parameter->pwm->DAT2 = parameter->pwm->DAT2;
        break;
        
      case 'f':
      //If user has no speed for both motors yet, set speed to half the max pwm_range
        if ((parameter->pwm->DAT1 == 0)&&(parameter->pwm->DAT2 == 0))
        {
          parameter->pwm->DAT1 = PWM_RANGE/2;
          parameter->pwm->DAT2 = PWM_RANGE/2;
        }
        else
      //If user already set speed, keep speed
        {
          parameter->pwm->DAT1 = parameter->pwm->DAT1;
          parameter->pwm->DAT2 = parameter->pwm->DAT2;
        }
        
        break;
        
      case 'b':
      //Same implementation as forward_speed of motor
        if ((parameter->pwm->DAT1 == 0)&&(parameter->pwm->DAT2 == 0))
        {
          parameter->pwm->DAT1 = PWM_RANGE/2;
          parameter->pwm->DAT2 = PWM_RANGE/2;
        }
        else
        {
          parameter->pwm->DAT1 = parameter->pwm->DAT1;
          parameter->pwm->DAT2 = parameter->pwm->DAT2;
        }
        
        break;
        
      case 'i':
        pthread_mutex_lock(&(parameter->speed->lock));
        //If clicked increment, left motor and right motor less than pwm range
        if(parameter->speed->increment==true && parameter->pwm->DAT1<PWM_RANGE && parameter->pwm->DAT2<PWM_RANGE)
        {
          printf("inc\n");
          parameter->pwm->DAT1=parameter->pwm->DAT1+2;
          parameter->pwm->DAT2=parameter->pwm->DAT2+2;
          printf("speed of left: %d\n",parameter->pwm->DAT1);
          printf("speed of right: %d\n",parameter->pwm->DAT2);
        }
        //If clicked increment, left or right already at max
        else if((parameter->speed->increment==true) && (parameter->pwm->DAT1>=PWM_RANGE || parameter->pwm->DAT2>=PWM_RANGE)) 
        {
          printf("Already at max speed\n");
          printf("speed of left: %d\n",parameter->pwm->DAT1);
          printf("speed of right: %d\n",parameter->pwm->DAT2);
        }
        parameter->speed->increment=false;
        pthread_mutex_unlock(&(parameter->speed->lock));
        break;
        
      case 'j':
        pthread_mutex_lock(&(parameter->speed->lock));
        //If clicked increment, left motor and right motor less than pwm range
        if((parameter->speed->increment==true) && (parameter->pwm->DAT1>0 || parameter->pwm->DAT2>0))
        {
          printf("dec\n");
          parameter->pwm->DAT1=parameter->pwm->DAT1-2;
          parameter->pwm->DAT2=parameter->pwm->DAT2-2;
          printf("speed of left: %d\n",parameter->pwm->DAT1);
          printf("speed of right: %d\n",parameter->pwm->DAT2);
        }
        //If clicked increment, left or right already 0
        else if((parameter->speed->increment==true) && (parameter->pwm->DAT1<=0 || parameter->pwm->DAT2<=0)) 
        {
          printf("Already at min speed\n");
          printf("speed of left: %d\n",parameter->pwm->DAT1);
          printf("speed of right: %d\n",parameter->pwm->DAT2);
        }
        //make sure to set parameter->speed->increment to false again so it does not decrement again
        parameter->speed->increment=false;
        pthread_mutex_unlock(&(parameter->speed->lock));
        break;
        
      case 'l':
        pthread_mutex_lock(&(parameter->speed->lock));
        
        //Make sure user set turn as true and that pwm is greater than 0
        if((parameter->speed->turn==true) && (parameter->pwm->DAT1>0))
        {
          printf("turnl\n");
          parameter->pwm->DAT1=parameter->pwm->DAT1-2;
          printf("speed of left: %d\n",parameter->pwm->DAT1);
          printf("speed of right: %d\n",parameter->pwm->DAT2);
        }
        parameter->speed->turn=false;
        pthread_mutex_unlock(&(parameter->speed->lock));
        break;
        
      //Same implementation as turning left  
      case 'r':
        pthread_mutex_lock(&(parameter->speed->lock));
        
        if((parameter->speed->turn==true) && (parameter->pwm->DAT2>0))
        {
          printf("turnr\n");
          parameter->pwm->DAT2=parameter->pwm->DAT2-2;
          printf("speed of left: %d\n",parameter->pwm->DAT1);
          printf("speed of right: %d\n",parameter->pwm->DAT2);
        }
        parameter->speed->turn=false;
        pthread_mutex_unlock(&(parameter->speed->lock));
        break;
        
      default:
        break;
    }

    pthread_mutex_lock( &(parameter->done->lock) );
  }
  pthread_mutex_unlock( &(parameter->done->lock) );

  return 0;
}

int get_pressed_key(void)
{
  struct termios  original_attributes;
  struct termios  modified_attributes;
  int             ch;

  tcgetattr( STDIN_FILENO, &original_attributes );
  modified_attributes = original_attributes;
  modified_attributes.c_lflag &= ~(ICANON | ECHO);
  modified_attributes.c_cc[VMIN] = 1;
  modified_attributes.c_cc[VTIME] = 0;
  tcsetattr( STDIN_FILENO, TCSANOW, &modified_attributes );

  ch = getchar();

  tcsetattr( STDIN_FILENO, TCSANOW, &original_attributes );

  return ch;
}

void *ThreadKey( void * arg )
{
  struct key_thread_parameter *thread_key_parameter = (struct key_thread_parameter *)arg;
  bool done;

  do
  {
    switch (get_pressed_key())
    {
      
      //All the different cases to take in inputs from user
      case 'q':
        done = true;
      
        pthread_mutex_lock( &(thread_key_parameter->option->lock) );
        thread_key_parameter->option->option = 'q';
        printf("Exiting program \n");
        pthread_mutex_unlock( &(thread_key_parameter->option->lock) );
        break;
      case 'a':
        pthread_mutex_lock( &(thread_key_parameter->option->lock) );
        thread_key_parameter->option->option = 'a';
        printf("Car is moving forward \n");
        pthread_mutex_unlock( &(thread_key_parameter->option->lock) );
        break;
      case 'f':
        pthread_mutex_lock( &(thread_key_parameter->option->lock));
        thread_key_parameter->option->option = 'f';
        printf("Car is moving forward \n");
        pthread_mutex_unlock( &(thread_key_parameter->option->lock) );
        break;
      case 'b':
        pthread_mutex_lock( &(thread_key_parameter->option->lock) );
        thread_key_parameter->option->option = 'b';
        printf("Car is moving backward \n");
        pthread_mutex_unlock( &(thread_key_parameter->option->lock) );
        break;
        
      case 's':
        pthread_mutex_lock( &(thread_key_parameter->option->lock) );
        thread_key_parameter->option->option = 's';
        printf("Car is stopping \n");
        pthread_mutex_unlock( &(thread_key_parameter->option->lock) );
        break;
      
      //For increase and decrease make sure to change increment value to true when user hits key
      case 'i':
        pthread_mutex_lock( &(thread_key_parameter->option->lock) );
        pthread_mutex_lock(&(thread_key_parameter->speed1->lock));
        thread_key_parameter->option->option = 'i';
        thread_key_parameter->speed1->increment = true;
        printf("Car is increasing speed \n");
        pthread_mutex_unlock(&(thread_key_parameter->speed1->lock));
        pthread_mutex_unlock( &(thread_key_parameter->option->lock) );
        break;
      case 'j':
        pthread_mutex_lock( &(thread_key_parameter->option->lock) );
        pthread_mutex_lock(&(thread_key_parameter->speed1->lock));
        thread_key_parameter->option->option = 'j';
        thread_key_parameter->speed1->increment = true;
        printf("Car is decreasing speed \n");
        pthread_mutex_unlock(&(thread_key_parameter->speed1->lock));
        pthread_mutex_unlock( &(thread_key_parameter->option->lock) );
        break;
      
      //For turn left and right make sure to change turn value when user hits key  
      case 'l':
        pthread_mutex_lock( &(thread_key_parameter->option->lock) );
        pthread_mutex_lock( &(thread_key_parameter->speed1->lock));
        thread_key_parameter->option->option = 'l';
        thread_key_parameter->speed1->turn = true;
        printf("Car is turning left \n");
        pthread_mutex_unlock(&(thread_key_parameter->speed1->lock));
        pthread_mutex_unlock( &(thread_key_parameter->option->lock) );
        break;
        
      case 'r':
        pthread_mutex_lock( &(thread_key_parameter->option->lock) );
        pthread_mutex_lock( &(thread_key_parameter->speed1->lock));
        thread_key_parameter->option->option = 'r';
        thread_key_parameter->speed1->turn = true;
        printf("Car is turning right \n");
        pthread_mutex_unlock(&(thread_key_parameter->speed1->lock));
        pthread_mutex_unlock( &(thread_key_parameter->option->lock) );
        break;
        
      default:
        break;
    }
  } while (!done);
  printf( "key thread exiting\n" );

  return (void *)0;
}

int main( void )
{
  //Set and initialize variables
  volatile struct io_peripherals *io;
  pthread_t                       thread12_handle;
  pthread_t                       thread13_handle;
  pthread_t                       thread18_handle;
  pthread_t                       thread19_handle;
  pthread_t                       thread22_handle;
  pthread_t                       thread23_handle;
  pthread_t                       thread_key_handle;
  struct done_flag                done   = {PTHREAD_MUTEX_INITIALIZER, false};
  struct option_flag              option = {PTHREAD_MUTEX_INITIALIZER, 'a'};
  struct level_flag               DLevel3 = {PTHREAD_MUTEX_INITIALIZER, 50, false};
  struct level_flag               DLevel4 = {PTHREAD_MUTEX_INITIALIZER, 50, false};
  struct level_flag               DLevel5 = {PTHREAD_MUTEX_INITIALIZER, 50, false};
  struct level_flag               DLevel6 = {PTHREAD_MUTEX_INITIALIZER, 50, false};
  struct speed_flag               speed1 = {PTHREAD_MUTEX_INITIALIZER, PWM_RANGE/2, false, false};
  struct thread_parameter         thread12_parameter;
  struct thread_parameter         thread13_parameter;
  struct thread_parameter         thread18_parameter;
  struct thread_parameter         thread19_parameter;
  struct thread_parameter         thread22_parameter;
  struct thread_parameter         thread23_parameter;
  struct key_thread_parameter     thread_key_parameter;

  io = import_registers();
  if (io != NULL)
  {
    /* print where the I/O memory was actually mapped to */
    printf( "mem at 0x%8.8X\n", (unsigned long)io );

    enable_pwm_clock( io );

    /* set the pin function to alternate function 0 for GPIO12 */
    /* set the pin function to alternate function 0 for GPIO13 */
    io->gpio.GPFSEL1.field.FSEL2 = GPFSEL_ALTERNATE_FUNCTION0;
    io->gpio.GPFSEL1.field.FSEL3 = GPFSEL_ALTERNATE_FUNCTION0;
    
    //Left motor
    io->gpio.GPFSEL1.field.FSEL8 = GPFSEL_OUTPUT;
    io->gpio.GPFSEL1.field.FSEL9 = GPFSEL_OUTPUT;
    
    //Right motor
    io->gpio.GPFSEL2.field.FSEL2 = GPFSEL_OUTPUT;
    io->gpio.GPFSEL2.field.FSEL3 = GPFSEL_OUTPUT;

    /* configure the PWM channels */
    io->pwm.RNG1 = PWM_RANGE;     /* the default value */
    io->pwm.RNG2 = PWM_RANGE;     /* the default value */
    io->pwm.CTL.field.MODE1 = 0;  /* PWM mode */
    io->pwm.CTL.field.MODE2 = 0;  /* PWM mode */
    io->pwm.CTL.field.RPTL1 = 1;  /* not using FIFO, but repeat the last byte anyway */
    io->pwm.CTL.field.RPTL2 = 1;  /* not using FIFO, but repeat the last byte anyway */
    io->pwm.CTL.field.SBIT1 = 0;  /* idle low */
    io->pwm.CTL.field.SBIT2 = 0;  /* idle low */
    io->pwm.CTL.field.POLA1 = 0;  /* non-inverted polarity */
    io->pwm.CTL.field.POLA2 = 0;  /* non-inverted polarity */
    io->pwm.CTL.field.USEF1 = 0;  /* do not use FIFO */
    io->pwm.CTL.field.USEF2 = 0;  /* do not use FIFO */
    io->pwm.CTL.field.MSEN1 = 1;  /* use M/S algorithm */
    io->pwm.CTL.field.MSEN2 = 1;  /* use M/S algorithm */
    io->pwm.CTL.field.CLRF1 = 1;  /* clear the FIFO, even though it is not used */
    io->pwm.CTL.field.PWEN1 = 1;  /* enable the PWM channel */
    io->pwm.CTL.field.PWEN2 = 1;  /* enable the PWM channel */

    thread12_parameter.pin = 12;
    thread12_parameter.gpio = &(io->gpio);
    thread12_parameter.pwm = &(io->pwm);
    thread12_parameter.done = &done;
    thread12_parameter.option = &option;
    thread12_parameter.speed = &speed1;
    thread13_parameter.pin = 13;
    thread13_parameter.pwm = &(io->pwm);
    thread13_parameter.gpio = &(io->gpio);
    thread13_parameter.done = &done;
    thread13_parameter.option = &option;
    thread13_parameter.speed = &speed1;
    thread18_parameter.pin = 18;
    thread18_parameter.pwm = &(io->pwm);
    thread18_parameter.gpio = &(io->gpio);
    thread18_parameter.done = &done;
    thread18_parameter.option = &option;
    thread18_parameter.DLevel = &DLevel3;
    thread19_parameter.pin = 19;
    thread19_parameter.pwm = &(io->pwm);
    thread19_parameter.gpio = &(io->gpio);
    thread19_parameter.done = &done;
    thread19_parameter.option = &option;
    thread19_parameter.DLevel = &DLevel4;
    thread22_parameter.pin = 22;
    thread22_parameter.pwm = &(io->pwm);
    thread22_parameter.gpio = &(io->gpio);
    thread22_parameter.done = &done;
    thread22_parameter.option = &option;
    thread22_parameter.DLevel = &DLevel5;
    thread23_parameter.pin = 23;
    thread23_parameter.pwm = &(io->pwm);
    thread23_parameter.gpio = &(io->gpio);
    thread23_parameter.done = &done;
    thread23_parameter.option = &option;
    thread23_parameter.DLevel = &DLevel6;
    thread_key_parameter.done = &done;
    thread_key_parameter.option = &option;
    thread_key_parameter.DLevel3 = &DLevel3;
    thread_key_parameter.DLevel4 = &DLevel4;
    thread_key_parameter.DLevel5 = &DLevel5;
    thread_key_parameter.DLevel6 = &DLevel6;
    thread_key_parameter.speed1 = &speed1;
    pthread_create( &thread12_handle, 0, ThreadHW, (void *)&thread12_parameter );
    pthread_create( &thread13_handle, 0, ThreadHW, (void *)&thread13_parameter );
    pthread_create( &thread18_handle, 0, ThreadSW, (void *)&thread18_parameter );
    pthread_create( &thread19_handle, 0, ThreadSW, (void *)&thread19_parameter );
    pthread_create( &thread22_handle, 0, ThreadSW, (void *)&thread22_parameter );
    pthread_create( &thread23_handle, 0, ThreadSW, (void *)&thread23_parameter );
    pthread_create( &thread_key_handle, 0, ThreadKey, (void *)&thread_key_parameter );
    pthread_join( thread12_handle, 0 );
    pthread_join( thread13_handle, 0 );
    pthread_join( thread18_handle, 0 );
    pthread_join( thread19_handle, 0 );
    pthread_join( thread22_handle, 0 );
    pthread_join( thread23_handle, 0 );
    pthread_join( thread_key_handle, 0 );
  }
  else
  {
    ; /* warning message already issued */
  }

  return 0;
}

