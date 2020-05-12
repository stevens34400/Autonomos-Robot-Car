/**************************************************
* CMPEN 473, Spring 2020, Penn State University
* 
* Homework 5
* Revision V1.1
* On 02/20/20
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
#include <string.h>
#include "import_registers.h"
#include "cm.h"
#include "gpio.h"
#include "spi.h"
#include "pwm.h"
#include "io_peripherals.h"
#include "enable_pwm_clock.h"

#define PWM_RANGE 64

struct done_flag
{
  pthread_mutex_t lock;
  bool            done;
};

struct option_flag
{
  pthread_mutex_t lock;
  char            option[10];
  int             mode;
  char            action[2];
  int             index;    //index for last element
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
  bool            turnl;  //Boolean for IR sensor turning left
  bool            turnr;  //Boolean for IR sensor turning right
};

struct thread_parameter
{
  volatile struct gpio_register * gpio;
  volatile struct pwm_register  * pwm;
  int                             pin;
  struct done_flag *              done;
  struct option_flag *            option;   //Used for different cases
  struct level_flag *             DLevel;   //Used only for the direction of the motors
  struct speed_flag *             speed;    //Used specifically to adjust the speed of motors
};

struct control_thread_parameter
{
  struct done_flag * done;
  struct option_flag *option;
  struct speed_flag *speedcontrol;
};

struct IR_thread_parameter
{
  struct done_flag *done;
  struct speed_flag *speedIR;   //speed based on IR sensor
  struct option_flag *option;
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

    /*** initial mode before user input ***/
    if (parameter->option->mode == 0)
    {
    }
    
    /*** quit mode ***/
    else if(parameter->option->mode == 1)
    {
      parameter->DLevel->level = 0;
    }
    
    /*** mode m1 ***/
    else if(parameter->option->mode == 2)
    {
      switch (parameter->option->action[0])
      {
        
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
                Timeu = Tlevel;   // repeat the dimming level for 1 times if Tlevel = 1 
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
                Timeu = Tlevel;   // repeat the dimming level for 1 time if Tlevel = 1 
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
          if (parameter->pin == 05 || parameter->pin == 22)
          {
            pthread_mutex_lock( &(parameter->DLevel->lock));
            //If level less than 50, make sure to dim gradually upwards
            if (parameter->DLevel->level < 50)
            {
              while(parameter->DLevel->level<100)
              {
                Timeu = Tlevel;   /* repeat the dimming level for 1 times if Tlevel = 1*/ 
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
              //printf("forward\n");
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
              //printf("forward\n");
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
          if (parameter->pin == 06 || parameter->pin == 23)
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
              //printf("backward\n");
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
              //printf("backward\n");
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

    }
    
    /*** mode m2 ***/
    else if(parameter->option->mode == 3)
    {
      switch(parameter->option->action[0])
      {
        case 'f':
          //Dim up to 100% for pin18 & pin 22
          if (parameter->pin == 05 || parameter->pin == 22)
          {
            pthread_mutex_lock( &(parameter->DLevel->lock));
            //If level less than 50, make sure to dim gradually upwards
            if (parameter->DLevel->level < 50)
            {
              while(parameter->DLevel->level<100)
              {
                Timeu = Tlevel;   /* repeat the dimming level for 1 times if Tlevel = 1*/ 
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
              //printf("forward\n");
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
              //printf("forward\n");
              parameter->DLevel->level=0;
              DimLevUnit(parameter->DLevel->level, parameter->pin, parameter->gpio);
            }
            
            pthread_mutex_unlock( &(parameter->DLevel->lock));
          }
          
          //make sure to set parameter->..->pause will be used later for increase/decrease speed
          //Set to false so that the car will increase in speed while moving forward
          parameter->DLevel->pause = false;
          break;
        
        case 's':
          //If level<50%, increment up to 50%
          pthread_mutex_lock( &(parameter->DLevel->lock));
          
            if (parameter->DLevel->level < 50)
            {
              while(parameter->DLevel->level<50)
              {
                Timeu = Tlevel;   // repeat the dimming level for 1 times if Tlevel = 1 
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
                Timeu = Tlevel;   // repeat the dimming level for 1 time if Tlevel = 1 
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
        
        default:
          break;
      }
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

    /*** initial mode before user input ***/
    if(parameter->option->mode == 0)
    {
      parameter->pwm->DAT1 = 0;
      parameter->pwm->DAT2 = 0;
    }
    /*** mode quit ***/
    else if (parameter->option->mode == 1)
    {
      parameter->pwm->DAT1 = 0;
      parameter->pwm->DAT2 = 0;
    }
    /*** Mode m1 ***/
    else if(parameter->option->mode == 2)
    {
      //printf("mode m1 speed %c\n", parameter->option->action[0]);
      usleep(1000);
      switch (parameter->option->action[0])
      {
        //Cases already specified in the above thread function ThreadSW          
        case 'a':
        //speed set to 0  
          parameter->pwm->DAT1 = 0;
          parameter->pwm->DAT2 = 0;
          break;
          
        case 's':
        //Save speed as soon as user inputs s and keep speed
          parameter->pwm->DAT1 = parameter->pwm->DAT1;
          parameter->pwm->DAT2 = parameter->pwm->DAT2;
          break;
          
        case 'f':
        //If user has no speed for both motors yet, set speed to half the max pwm_range
          //printf("m1 actual forward \n");
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
            //printf("inc\n");
            parameter->pwm->DAT1=parameter->pwm->DAT1+4;
            parameter->pwm->DAT2=parameter->pwm->DAT2+4;
            printf("speed of left: %d\n",parameter->pwm->DAT1);
            printf("speed of right: %d\n\n",parameter->pwm->DAT2);
          }
          //If clicked increment, left or right already at max
          else if((parameter->speed->increment==true) && (parameter->pwm->DAT1>=PWM_RANGE || parameter->pwm->DAT2>=PWM_RANGE)) 
          {
            printf("Already at max speed\n");
            printf("speed of left: %d\n",parameter->pwm->DAT1);
            printf("speed of right: %d\n\n",parameter->pwm->DAT2);
          }
          parameter->speed->increment=false;
          pthread_mutex_unlock(&(parameter->speed->lock));
          break;
          
        case 'j':
          pthread_mutex_lock(&(parameter->speed->lock));
          //If clicked increment, left motor and right motor less than pwm range
          if((parameter->speed->increment==true) && (parameter->pwm->DAT1>0 || parameter->pwm->DAT2>0))
          {
            //printf("dec\n");
            parameter->pwm->DAT1=parameter->pwm->DAT1-4;
            parameter->pwm->DAT2=parameter->pwm->DAT2-4;
            printf("speed of left: %d\n",parameter->pwm->DAT1);
            printf("speed of right: %d\n\n",parameter->pwm->DAT2);
          }
          //If clicked increment, left or right already 0
          else if((parameter->speed->increment==true) && (parameter->pwm->DAT1<=0 || parameter->pwm->DAT2<=0)) 
          {
            printf("Already at min speed\n");
            printf("speed of left: %d\n",parameter->pwm->DAT1);
            printf("speed of right: %d\n\n",parameter->pwm->DAT2);
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
            //printf("turnl\n");
            parameter->pwm->DAT1=parameter->pwm->DAT1-4;
            printf("speed of left: %d\n",parameter->pwm->DAT1);
            printf("speed of right: %d\n\n",parameter->pwm->DAT2);
          }
          parameter->speed->turn=false;
          pthread_mutex_unlock(&(parameter->speed->lock));
          break;
          
        //Same implementation as turning left  
        case 'r':
          pthread_mutex_lock(&(parameter->speed->lock));
          
          if((parameter->speed->turn==true) && (parameter->pwm->DAT2>0))
          {
            //printf("turnr\n");
            parameter->pwm->DAT2=parameter->pwm->DAT2-4;
            printf("speed of left: %d\n",parameter->pwm->DAT1);
            printf("speed of right: %d\n\n",parameter->pwm->DAT2);
          }
          parameter->speed->turn=false;
          pthread_mutex_unlock(&(parameter->speed->lock));
          break;
          
        default:
          break;
      }
    }
    
    /*** mode m2 ***/
    else if (parameter->option->mode == 3)
    {
      usleep(1000);
      switch(parameter->option->action[0])
      {
        case 's':
        //Save speed as soon as user inputs s and keep speed
          parameter->pwm->DAT1 = parameter->pwm->DAT1;
          parameter->pwm->DAT2 = parameter->pwm->DAT2;
          break;
          
        case 'f':
          if ((parameter->pwm->DAT1 == 0)||(parameter->pwm->DAT2 == 0))
          {
            
            
            parameter->pwm->DAT1 = PWM_RANGE/2;
            parameter->pwm->DAT2 = PWM_RANGE/2;
          
          if(parameter->speed->turnl == true)
          {
            if((parameter->speed->turnl==true) && (parameter->pwm->DAT1>0))
              {
                printf("actual turn left");
                parameter->pwm->DAT1 = PWM_RANGE - 10;
                parameter->pwm->DAT2 = PWM_RANGE - 10;
                parameter->pwm->DAT1 = parameter->pwm->DAT1 - 4;
                pthread_mutex_lock(&(parameter->speed->lock));
                parameter->speed->turnl=false;
                pthread_mutex_unlock(&(parameter->speed->lock));
                usleep(1000);
              }
              //usleep(100000);
              //parameter->pwm->DAT1 = parameter->pwm->DAT2;
          }
          else if(parameter->speed->turnr == true)
          {
            if((parameter->speed->turnr==true) && (parameter->pwm->DAT2>0))
              {
                printf("actual turn right");
                parameter->pwm->DAT1 = PWM_RANGE - 10;
                parameter->pwm->DAT2 = PWM_RANGE - 10;
                parameter->pwm->DAT2 = parameter->pwm->DAT2 - 4;
                pthread_mutex_lock(&(parameter->speed->lock));
                parameter->speed->turnr=false;
                pthread_mutex_unlock(&(parameter->speed->lock));
                usleep(1000);
              }
              //usleep(100000);
              //parameter->pwm->DAT1 = parameter->pwm->DAT2;
          }
          /**else if((parameter->speed->turnl == false) && (parameter->speed->turnr == false))
          {
            if(parameter->pwm->DAT1 != parameter->pwm->DAT2)
            {
              parameter->pwm->DAT1 = parameter->pwm->DAT2;
            }
            else
            {
              if(parameter->pwm->DAT1 < PWM_RANGE-10)
              {
                parameter->pwm->DAT1 = parameter->pwm->DAT1 + 1;
                parameter->pwm->DAT2 = parameter->pwm->DAT2 + 1;
                usleep(100000);
              }
            }
          }**/
          
          /***
          
            if(parameter->speed->turnl == true)
            {
              if((parameter->speed->turnl==true) && (parameter->pwm->DAT1>0))
              {
                printf("actual turn left");
                parameter->pwm->DAT1 = parameter->pwm->DAT1 - 1;
                pthread_mutex_lock(&(parameter->speed->lock));
                parameter->speed->turnl=false;
                pthread_mutex_unlock(&(parameter->speed->lock));
                usleep(100000);
              }
              //usleep(100000);
              //parameter->pwm->DAT1 = parameter->pwm->DAT2;
            }
            else
            {
              parameter->pwm->DAT1 = parameter->pwm->DAT2;
            }
            
            if(parameter->speed->turnr == true)
            {
              if((parameter->speed->turnr==true) && (parameter->pwm->DAT2>0))
              {
                printf("actual turn right");
                parameter->pwm->DAT2 = parameter->pwm->DAT2 - 1;
                pthread_mutex_lock(&(parameter->speed->lock));
                parameter->speed->turnr=false;
                pthread_mutex_unlock(&(parameter->speed->lock));
                usleep(100000);
              }
              //usleep(100000);
              //parameter->pwm->DAT1 = parameter->pwm->DAT2;
            }
            else
            {
              parameter->pwm->DAT2 = parameter->pwm->DAT1;
            }
            
            ***/
          } 
          else
        //If user already set speed, keep speed
          {
            parameter->pwm->DAT1 = parameter->pwm->DAT1;
            parameter->pwm->DAT2 = parameter->pwm->DAT2;
            
            if(parameter->speed->turnl == true)
          {
            if((parameter->speed->turnl==true) && (parameter->pwm->DAT1>0))
              {
                printf("actual turn left");
                parameter->pwm->DAT1 = PWM_RANGE - 10;
                parameter->pwm->DAT2 = PWM_RANGE - 10;
                parameter->pwm->DAT1 = parameter->pwm->DAT1 - 4;
                pthread_mutex_lock(&(parameter->speed->lock));
                parameter->speed->turnl=false;
                pthread_mutex_unlock(&(parameter->speed->lock));
                usleep(1000);
              }
              //usleep(100000);
              //parameter->pwm->DAT1 = parameter->pwm->DAT2;
          }
          else if(parameter->speed->turnr == true)
          {
            if((parameter->speed->turnr==true) && (parameter->pwm->DAT2>0))
              {
                printf("actual turn right");
                parameter->pwm->DAT1 = PWM_RANGE - 10;
                parameter->pwm->DAT2 = PWM_RANGE - 10;
                parameter->pwm->DAT2 = parameter->pwm->DAT2 - 4;
                pthread_mutex_lock(&(parameter->speed->lock));
                parameter->speed->turnr=false;
                pthread_mutex_unlock(&(parameter->speed->lock));
                usleep(1000);
              }
              //usleep(100000);
              //parameter->pwm->DAT1 = parameter->pwm->DAT2;
          }
          /**else if((parameter->speed->turnl == false) && (parameter->speed->turnr == false))
          {
            if(parameter->pwm->DAT1 != parameter->pwm->DAT2)
            {
              parameter->pwm->DAT1 = parameter->pwm->DAT2;
            }
            else
            {
              if(parameter->pwm->DAT1 < PWM_RANGE-10)
              {
                parameter->pwm->DAT1 = parameter->pwm->DAT1 + 1;
                parameter->pwm->DAT2 = parameter->pwm->DAT2 + 1;
                usleep(100000);
              }
            }
          }**/
            
            /***
            if(parameter->speed->turnl == true)
            {
              if((parameter->speed->turnl==true) && (parameter->pwm->DAT1>0))
              {
                printf("actual turn left");
                parameter->pwm->DAT1 = parameter->pwm->DAT1 - 1;
                pthread_mutex_lock(&(parameter->speed->lock));
                parameter->speed->turnl=false;
                pthread_mutex_unlock(&(parameter->speed->lock));
                usleep(100000);
              }
              //usleep(100000);
              //parameter->pwm->DAT1 = parameter->pwm->DAT2;
            }
            else
            {
              parameter->pwm->DAT1 = parameter->pwm->DAT2;
            }
            
            if(parameter->speed->turnr == true)
            {
              if((parameter->speed->turnr==true) && (parameter->pwm->DAT2>0))
              {
                printf("actual turn right");
                parameter->pwm->DAT2 = parameter->pwm->DAT2 - 1;
                pthread_mutex_lock(&(parameter->speed->lock));
                parameter->speed->turnr=false;
                pthread_mutex_unlock(&(parameter->speed->lock));
                usleep(100000);
              }
              //usleep(100000);
              //parameter->pwm->DAT1 = parameter->pwm->DAT2;
            }
            else
            {
              parameter->pwm->DAT2 = parameter->pwm->DAT1;
            }
            ***/
            
          }
          break;
        
        default:
          break;
      }
    }
    pthread_mutex_lock( &(parameter->done->lock) );
  }
  pthread_mutex_unlock( &(parameter->done->lock) );



  return 0;
}

void *ThreadIR( void * arg)
{
  struct IR_thread_parameter *thread_IR_parameter = (struct IR_thread_parameter *)arg;
  volatile struct io_peripherals *io;
  
  io = import_registers();
  
  do
  {
    //usleep(1000);
    if(thread_IR_parameter->option->mode == 3)
    {
      
      if(thread_IR_parameter->option->action[0] == 'f')
      {
        if((GPIO_READ(&(io->gpio), 24)> 0)&&(GPIO_READ(&(io->gpio), 25)== 0))
        {
          pthread_mutex_lock(&(thread_IR_parameter->speedIR->lock));
          thread_IR_parameter->speedIR->turnl=true;
          ///printf("Need to turn left\n");
          pthread_mutex_unlock(&(thread_IR_parameter->speedIR->lock));
        }
        else if((GPIO_READ(&(io->gpio), 24)== 0)&&(GPIO_READ(&(io->gpio), 25)> 0))
        {
          pthread_mutex_lock(&(thread_IR_parameter->speedIR->lock));
          thread_IR_parameter->speedIR->turnr=true;
          ///printf("Need to turn right \n");
          pthread_mutex_unlock(&(thread_IR_parameter->speedIR->lock));
        }
        else if((GPIO_READ(&(io->gpio), 24)>0)&&(GPIO_READ(&(io->gpio), 25)> 0))
        {
          ///printf("Keep straight \n");
        }
        else if((GPIO_READ(&(io->gpio), 24)== 0)&&(GPIO_READ(&(io->gpio), 25)== 0))
        {
          ///printf("keep straight \n");
        }
      }
      
    }
  }while(!thread_IR_parameter->done->done);
  
  
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
  
  bool done = false;
  //printf("threadkey \n");
  

  do
  {
    
    for (int i = 0; i < thread_key_parameter->option->index; i++)
    {
      printf("%c\n", thread_key_parameter->option->option[i]);
    };
    /***
    printf("Print first element %c\n", thread_key_parameter->option->option[0]);
    printf("Print second element %c\n", thread_key_parameter->option->option[1]);
    ***/
    pthread_mutex_lock(&(thread_key_parameter->done->lock));
    done = thread_key_parameter->done->done;
    //printf("done_variable threadkey: %d\n ", done);
    pthread_mutex_unlock(&(thread_key_parameter->done->lock));
    
    
    pthread_mutex_lock( &(thread_key_parameter->option->lock) );
    if (done != true)
    {
      thread_key_parameter->option->option[thread_key_parameter->option->index] = get_pressed_key();
      ///printf("User input: %c\n", thread_key_parameter->option->option[thread_key_parameter->option->index]);
    }
    pthread_mutex_unlock( &(thread_key_parameter->option->lock) );

    pthread_mutex_lock( &(thread_key_parameter->option->lock) );
    //printf("Counter for index %d\n", thread_key_parameter->option->index);
    thread_key_parameter->option->index++;
    pthread_mutex_unlock( &(thread_key_parameter->option->lock) );
    
    usleep(10000);
  } while (!done);
  
  printf( "key thread exiting\n" );

  return (void *)0;
}

void *ThreadControl (void * arg)
{
  struct control_thread_parameter *thread_control_parameter = (struct control_thread_parameter *)arg;
  bool done = false;
  
  printf("Enter User input for mode: \n");
  
  do
  {
    /*** initial mode, before first user input ***/
    if(thread_control_parameter->option->mode==0) 
    {
      if (thread_control_parameter->option->index == 1)
      {
        //printf("has 1 character \n");
        if (thread_control_parameter->option->option[0] == 'q')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          thread_control_parameter->option->mode = 1;
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if (thread_control_parameter->option->option[0] == 'm')
        {
          //printf("hi \n");
          //break;
        }
        else 
        {
          printf("Invalid input, try again \n");
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          thread_control_parameter->option->index=0;
          memset(thread_control_parameter->option->option, 0, 10);
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
      }
      else if (thread_control_parameter->option->index == 2) //If char array has 2 elements
      {
        //printf("has 2 characters \n");
        if(thread_control_parameter->option->option[1] == '1') //Switch to mode m1
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          printf("M1> \n");
          thread_control_parameter->option->mode = 2;
          memset(thread_control_parameter->option->option, 0, 10);
          thread_control_parameter->option->index = 0;
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if(thread_control_parameter->option->option[1] == '2')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          printf("M2> \n");
          thread_control_parameter->option->mode = 3;
          memset(thread_control_parameter->option->option, 0, 10);
          thread_control_parameter->option->index = 0;
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else
        {
          printf("Invalid input, try again \n");
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          thread_control_parameter->option->index=0;
          memset(thread_control_parameter->option->option, 0, 10);
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
      }
    }
    
    /*** quit mode ***/
    else if (thread_control_parameter->option->mode == 1) 
    {
      done = true;
      pthread_mutex_lock( &(thread_control_parameter->option->lock) );
      memset(thread_control_parameter->option->option, 0 , 10);
      memset(thread_control_parameter->option->action, 0, 2);
      thread_control_parameter->option->index = 0;
      pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
      usleep(1000);
    }
    
    /*** M1 mode ***/
    else if (thread_control_parameter->option->mode == 2)
    {
      if(thread_control_parameter->option->index == 1)  //User inputs 1 character
      {
        if (thread_control_parameter->option->option[0] == 's')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          printf("M1> stop\n\n");
          thread_control_parameter->option->action[0]= 's';
          memset(thread_control_parameter->option->option, 0 ,10);
          thread_control_parameter->option->index=0;
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if (thread_control_parameter->option->option[0] == 'f')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          printf("M1> forward\n\n");
          thread_control_parameter->option->action[0]= 'f';
          memset(thread_control_parameter->option->option, 0 ,10);
          thread_control_parameter->option->index=0;
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if (thread_control_parameter->option->option[0] == 'b')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          printf("M1> backward\n\n");
          thread_control_parameter->option->action[0]= 'b';
          memset(thread_control_parameter->option->option, 0 ,10);
          thread_control_parameter->option->index=0;
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if (thread_control_parameter->option->option[0] == 'i')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          pthread_mutex_lock(&(thread_control_parameter->speedcontrol->lock));
          printf("M1> increment speed\n");
          thread_control_parameter->option->action[0]= 'i';
          memset(thread_control_parameter->option->option, 0 ,10);
          thread_control_parameter->option->index=0;
          thread_control_parameter->speedcontrol->increment = true;
          pthread_mutex_unlock(&(thread_control_parameter->speedcontrol->lock));
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if (thread_control_parameter->option->option[0] == 'j')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          pthread_mutex_lock(&(thread_control_parameter->speedcontrol->lock));
          printf("M1> decrement speed\n");
          thread_control_parameter->option->action[0]= 'j';
          memset(thread_control_parameter->option->option, 0 ,10);
          thread_control_parameter->option->index=0;
          thread_control_parameter->speedcontrol->increment=true;
          pthread_mutex_unlock(&(thread_control_parameter->speedcontrol->lock));
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if (thread_control_parameter->option->option[0] == 'l')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          pthread_mutex_lock(&(thread_control_parameter->speedcontrol->lock));
          printf("M1> left\n");
          thread_control_parameter->option->action[0]= 'l';
          memset(thread_control_parameter->option->option, 0 ,10);
          thread_control_parameter->option->index=0;
          thread_control_parameter->speedcontrol->turn=true;
          pthread_mutex_unlock(&(thread_control_parameter->speedcontrol->lock));
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if (thread_control_parameter->option->option[0] == 'r')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          pthread_mutex_lock(&(thread_control_parameter->speedcontrol->lock));
          printf("M1> right\n");
          thread_control_parameter->option->action[0]= 'r';
          memset(thread_control_parameter->option->option, 0 ,10);
          thread_control_parameter->option->index=0;
          thread_control_parameter->speedcontrol->turn=true;
          pthread_mutex_unlock(&(thread_control_parameter->speedcontrol->lock));
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if (thread_control_parameter->option->option[0] == 'q')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          printf("M1> quit\n");
          thread_control_parameter->option->mode=1;
          memset(thread_control_parameter->option->option, 0 ,10);
          thread_control_parameter->option->index=0;
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if (thread_control_parameter->option->option[0] == 'm')
        {
          usleep(1000);
          if (thread_control_parameter->option->index == 2)
          {
            if (thread_control_parameter->option->option[1]=='2')
            {
              pthread_mutex_lock( &(thread_control_parameter->option->lock) );
              printf("M1> change mode to m2 \n");
              thread_control_parameter->option->mode = 3;
              memset(thread_control_parameter->option->option, 0, 10);
              memset(thread_control_parameter->option->action, 0 , 2);
              thread_control_parameter->option->index= 0;
              pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
            }
            else
            {
              pthread_mutex_lock( &(thread_control_parameter->option->lock) );
              printf("Invalid input, try again \n");
              thread_control_parameter->option->index=0;
              memset(thread_control_parameter->option->option, 0, 10);
              pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
            }
          }
        }
        else
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          printf("Invalid input, try again \n");
          thread_control_parameter->option->index=0;
          memset(thread_control_parameter->option->option, 0, 10);
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
      }
    }
    
    /*** Mode m2 ***/
    else if (thread_control_parameter->option->mode == 3)
    {
      
      if(thread_control_parameter->option->index == 1)  //User inputs 1 character
      {
        if (thread_control_parameter->option->option[0] == 's')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          printf("M2> stop\n");
          thread_control_parameter->option->action[0]= 's';
          memset(thread_control_parameter->option->option, 0 ,10);
          thread_control_parameter->option->index=0;
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if (thread_control_parameter->option->option[0] == 'f')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          printf("M2> line tracing\n");
          thread_control_parameter->option->action[0]= 'f';
          memset(thread_control_parameter->option->option, 0 ,10);
          thread_control_parameter->option->index=0;
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if (thread_control_parameter->option->option[0] == 'q')
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          printf("M2> quit\n");
          thread_control_parameter->option->mode=1;
          memset(thread_control_parameter->option->option, 0 ,10);
          thread_control_parameter->option->index=0;
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
        else if (thread_control_parameter->option->option[0] == 'm')
        {
          usleep(1000);
          if (thread_control_parameter->option->index == 2)
          {
            
            printf("m2 2nd element %c", thread_control_parameter->option->option[1]);
            if (thread_control_parameter->option->option[1]== '1')
            {
              pthread_mutex_lock( &(thread_control_parameter->option->lock) );
              printf("m2 - change mode to m1 \n");
              thread_control_parameter->option->mode = 2;
              memset(thread_control_parameter->option->option, 0, 10);
              memset(thread_control_parameter->option->action, 0 , 2);
              thread_control_parameter->option->index= 0;
              pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
            }
            else
            {
              pthread_mutex_lock( &(thread_control_parameter->option->lock) );
              printf("Invalid input, try again \n");
              thread_control_parameter->option->index=0;
              memset(thread_control_parameter->option->option, 0, 10);
              pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
            }
          }
          else if (thread_control_parameter->option->index > 2)
          {
            pthread_mutex_lock(&(thread_control_parameter->option->lock));
            memset(thread_control_parameter->option->option, 0, 10);
            memset(thread_control_parameter->option->action, 0, 2);
            thread_control_parameter->option->index = 0;
            pthread_mutex_unlock(&(thread_control_parameter->option->lock));
          } 
        }
        else
        {
          pthread_mutex_lock( &(thread_control_parameter->option->lock) );
          printf("Invalid input, try again \n");
          thread_control_parameter->option->index=0;
          memset(thread_control_parameter->option->option, 0, 10);
          pthread_mutex_unlock( &(thread_control_parameter->option->lock) );
        }
      }
    }
  }while(!done);
  
  //Make sure to change done parameter to true
  pthread_mutex_lock(&(thread_control_parameter->done->lock));
  thread_control_parameter->done->done = true;
  pthread_mutex_unlock(&(thread_control_parameter->done->lock));
  
  ///printf("Done \n");
  return 0;
}

int main( void )
{
  //Set and initialize variables
  volatile struct io_peripherals *io;
  pthread_t                       thread12_handle;
  pthread_t                       thread13_handle;
  pthread_t                       thread05_handle;
  pthread_t                       thread06_handle;
  pthread_t                       thread22_handle;
  pthread_t                       thread23_handle;
  pthread_t                       thread_control_handle;  //Control thread
  pthread_t                       thread_key_handle;
  pthread_t                       thread_IR_handle;
  struct done_flag                done   = {PTHREAD_MUTEX_INITIALIZER, false};
  struct option_flag              option = {PTHREAD_MUTEX_INITIALIZER, 'a', 0, 'a', 0};
  struct level_flag               DLevel3 = {PTHREAD_MUTEX_INITIALIZER, 50, false};
  struct level_flag               DLevel4 = {PTHREAD_MUTEX_INITIALIZER, 50, false};
  struct level_flag               DLevel5 = {PTHREAD_MUTEX_INITIALIZER, 50, false};
  struct level_flag               DLevel6 = {PTHREAD_MUTEX_INITIALIZER, 50, false};
  struct speed_flag               speed1 = {PTHREAD_MUTEX_INITIALIZER, PWM_RANGE/2, false, false, false, false};
  struct thread_parameter         thread12_parameter;
  struct thread_parameter         thread13_parameter;
  struct thread_parameter         thread05_parameter;
  struct thread_parameter         thread06_parameter;
  struct thread_parameter         thread22_parameter;
  struct thread_parameter         thread23_parameter;
  struct key_thread_parameter     thread_key_parameter;
  struct control_thread_parameter thread_control_parameter;
  struct IR_thread_parameter      thread_IR_parameter;

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
    io->gpio.GPFSEL0.field.FSEL5 = GPFSEL_OUTPUT;
    io->gpio.GPFSEL0.field.FSEL6 = GPFSEL_OUTPUT;
    
    //Right motor
    io->gpio.GPFSEL2.field.FSEL2 = GPFSEL_OUTPUT;
    io->gpio.GPFSEL2.field.FSEL3 = GPFSEL_OUTPUT;
    
    //IR sensor
    io->gpio.GPFSEL2.field.FSEL4 = GPFSEL_INPUT;
    io->gpio.GPFSEL2.field.FSEL5 = GPFSEL_INPUT;

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
    thread05_parameter.pin = 05;
    thread05_parameter.pwm = &(io->pwm);
    thread05_parameter.gpio = &(io->gpio);
    thread05_parameter.done = &done;
    thread05_parameter.option = &option;
    thread05_parameter.DLevel = &DLevel3;
    thread06_parameter.pin = 06;
    thread06_parameter.pwm = &(io->pwm);
    thread06_parameter.gpio = &(io->gpio);
    thread06_parameter.done = &done;
    thread06_parameter.option = &option;
    thread06_parameter.DLevel = &DLevel4;
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
    thread_control_parameter.done = &done;
    thread_control_parameter.option = &option;
    thread_control_parameter.speedcontrol = &speed1;
    thread_IR_parameter.done = &done;
    thread_IR_parameter.speedIR = &speed1;
    thread_IR_parameter.option = &option;
    pthread_create( &thread12_handle, 0, ThreadHW, (void *)&thread12_parameter );
    pthread_create( &thread13_handle, 0, ThreadHW, (void *)&thread13_parameter );
    pthread_create( &thread05_handle, 0, ThreadSW, (void *)&thread05_parameter );
    pthread_create( &thread06_handle, 0, ThreadSW, (void *)&thread06_parameter );
    pthread_create( &thread22_handle, 0, ThreadSW, (void *)&thread22_parameter );
    pthread_create( &thread23_handle, 0, ThreadSW, (void *)&thread23_parameter );
    pthread_create( &thread_IR_handle, 0, ThreadIR, (void *)&thread_IR_parameter); //IR thread
    pthread_create( &thread_control_handle, 0, ThreadControl, (void *)&thread_control_parameter ); //Control thread
    pthread_create( &thread_key_handle, 0, ThreadKey, (void *)&thread_key_parameter );
    pthread_join( thread12_handle, 0 );
    pthread_join( thread13_handle, 0 );
    pthread_join( thread05_handle, 0 );
    pthread_join( thread06_handle, 0 );
    pthread_join( thread22_handle, 0 );
    pthread_join( thread23_handle, 0 );
    pthread_join( thread_key_handle, 0);
    pthread_join( thread_control_handle, 0 );
    pthread_join( thread_IR_handle, 0);
  }
  else
  {
    ; /* warning message already issued */
  }

  return 0;
}

