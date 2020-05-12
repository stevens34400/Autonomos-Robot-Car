/**************************************************
* CMPEN 473, Spring 2020, Penn State University
* 
* Homework 3, Sample Program 1
* Revision V1.1
* On 02/06/20
* By Steven Sisjayawan
* 
* Purpose: Create program for LED dimming utilizing concepts of PWM.
* Utilizing 4 different color LEDs, the different LEDs will dim up-down
* to a respective brightness level with their respective times taken. 
* 
* Program information: Utilized the sample program using Linux pthreads
* for PWM LED control. Adjusted the struct thread_parameter to include
* two new attributes of Tstep and div. Tstep was to make sure that timing
* for the different LED colors were going to happen at different lengths
* while div corresponds to the correct brightness level for an LED. 
* 
***************************************************/

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include "import_registers.h"
#include "gpio.h"

struct io_peripherals
{
  uint8_t              unused[0x200000];
  struct gpio_register gpio;
};

struct thread_parameter
{
  volatile struct gpio_register * gpio;
  int                             pin;
  int Tstep;    //Tstep value based on how long dim-up will take
  double div;   //double value to make sure that brightness is to certain level
};

//int  Tstep = 160;  /* PWM time resolution, number used for usleep(Tstep) */ 
int  Tlevel = 1;  /* repetition count of each light level, eg. repeat 12% light level for 2 times. */

void DimLevUnit(int Level, int pin, volatile struct gpio_register *gpio, int Tstep)
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

void *ThreadSW( void * arg )
{
  int                       iterations; /* used to limit the number of dimming iterations */
  int                       Timeu;      /* dimming repetition of each level */
  int                       DLevel;     /* dimming level as duty cycle, 0 to 100 percent */
  struct thread_parameter * parameter = (struct thread_parameter *)arg;

  for (iterations = 0; iterations < 10; iterations++)
  {
    DLevel = 0;  /* dim up, sweep the light level from 0 to 100 */
    while(DLevel<100)
    {
      Timeu = Tlevel;   /* repeat the dimming level for 5 times if Tlevel = 5 */
      while(Timeu>0)
      {
        //DLevel/parameter.div will give you the brightness percentage necessary for each LED color
        DimLevUnit(DLevel/parameter->div, parameter->pin, parameter->gpio, parameter->Tstep);
        Timeu = Timeu - 1;
      }
      DLevel = DLevel + 1;
    }
    
    
    DLevel = 100;  /* dim down, sweep the light level from 100 to 0 */
    while(DLevel>0)
    {
      Timeu = Tlevel;   /* repeat the dimming level for 5 times if Tlevel = 5 */
      while(Timeu>0)
      {
        DimLevUnit(DLevel/parameter->div, parameter->pin, parameter->gpio, parameter->Tstep);
        Timeu = Timeu - 1;
      }
      DLevel = DLevel - 1;
    }
  }

  return 0;
}

int main( void )
{
  volatile struct io_peripherals *io;
  pthread_t                       thread22_handle;
  pthread_t                       thread23_handle;
  pthread_t                       thread18_handle;
  pthread_t                       thread19_handle;
  struct thread_parameter         thread22_parameter;
  struct thread_parameter         thread23_parameter;
  struct thread_parameter         thread18_parameter;
  struct thread_parameter         thread19_parameter;

  io = import_registers();
  if (io != NULL)
  {
    /* print where the I/O memory was actually mapped to */
    printf( "mem at 0x%8.8X\n", (unsigned long)io );

    /* set the pin function to GPIO for GPIO12, GPIO13, GPIO18, GPIO19 */
    io->gpio.GPFSEL2.field.FSEL2 = GPFSEL_OUTPUT; //blue LED
    io->gpio.GPFSEL2.field.FSEL3 = GPFSEL_OUTPUT; //green LED
    io->gpio.GPFSEL1.field.FSEL8 = GPFSEL_OUTPUT; //red LED
    io->gpio.GPFSEL1.field.FSEL9 = GPFSEL_OUTPUT; //yellow LED

#if 0
    Thread18( (void *)io );
#else
    //Initialize parameters for each LED color
    thread22_parameter.pin = 22;
    thread22_parameter.gpio = &(io->gpio);
    thread22_parameter.Tstep = 200;
    thread22_parameter.div=1.3;
    
    thread23_parameter.pin = 23;
    thread23_parameter.gpio = &(io->gpio);
    thread23_parameter.Tstep = 200;
    thread23_parameter.div=1;
    
    thread18_parameter.pin = 18;
    thread18_parameter.gpio = &(io->gpio);
    thread18_parameter.Tstep = 100;
    thread18_parameter.div=4;
    
    thread19_parameter.pin = 19;
    thread19_parameter.gpio = &(io->gpio);
    thread19_parameter.Tstep = 100;
    thread19_parameter.div=2;
    
    //Allow the dim up-down functionallity to happen individually
    pthread_create( &thread22_handle, 0, ThreadSW, (void *)&thread22_parameter );
    pthread_create( &thread23_handle, 0, ThreadSW, (void *)&thread23_parameter );
    pthread_create( &thread18_handle, 0, ThreadSW, (void *)&thread18_parameter );
    pthread_create( &thread19_handle, 0, ThreadSW, (void *)&thread19_parameter );
    pthread_join( thread22_handle, 0 );
    pthread_join( thread23_handle, 0 );  
    pthread_join( thread18_handle, 0 );
    pthread_join( thread19_handle, 0 );
#endif

    printf("  Dimming light levels done. \n \n");

  }
  else
  {
    ; /* warning message already issued */
  }

  return 0;
}

