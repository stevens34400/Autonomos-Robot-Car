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

#define PWM_RANGE 100

struct pause_flag
{
  pthread_mutex_t lock;
  bool            pause;
};

struct done_flag
{
  pthread_mutex_t lock;
  bool            done;
};

struct thread_parameter
{
  volatile struct gpio_register * gpio;
  volatile struct pwm_register  * pwm;
  int                             pin;
  struct pause_flag *             pause;
  struct done_flag *              done;
};

struct key_thread_parameter
{
  struct done_flag *  done;
  struct pause_flag * pause1;
  struct pause_flag * pause2;
  struct pause_flag * pause3;
  struct pause_flag * pause4;
};

int  Tstep = 50;  /* PWM time resolution, number used for usleep(Tstep) */
int  Tlevel = 5;  /* repetition count of each light level, eg. repeat 12% light level for 5 times. */

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

//Control the speed of motor
void *ThreadSW( void * arg )
{
  int                       iterations; /* used to limit the number of dimming iterations */
  int                       Timeu;      /* dimming repetition of each level */
  int                       DLevel;     /* dimming level as duty cycle, 0 to 100 percent */
  struct thread_parameter * parameter = (struct thread_parameter *)arg;

  pthread_mutex_lock( &(parameter->done->lock) );
  while (!(parameter->done->done))
  {
    pthread_mutex_unlock( &(parameter->done->lock) );

    pthread_mutex_lock( &(parameter->pause->lock) );
    while (parameter->pause->pause)
    {
      pthread_mutex_unlock( &(parameter->pause->lock) );
         usleep( 10000 ); /* 10ms */
      pthread_mutex_lock( &(parameter->pause->lock) );
    }
    pthread_mutex_unlock( &(parameter->pause->lock) );

    DLevel = 0;  /* dim up, sweep the light level from 0 to 100 */
    while(DLevel<100) /* 2.5s */
    {
      Timeu = Tlevel;   /* repeat the dimming level for 5 times if Tlevel = 5 */
      while(Timeu>0)  /* 25ms */
      {
        DimLevUnit(DLevel, parameter->pin, parameter->gpio);  /* 5ms */
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
        DimLevUnit(DLevel, parameter->pin, parameter->gpio);
        Timeu = Timeu - 1;
      }
      DLevel = DLevel - 1;
    }
    pthread_mutex_lock( &(parameter->done->lock) );
  }
  pthread_mutex_unlock( &(parameter->done->lock) );

  return 0;
}

//Contrls direction of motor
void *ThreadHW( void * arg )
{
  int                       iterations; /* used to limit the number of dimming iterations */
  int                       Timeu;      /* dimming repetition of each level */
  int                       DLevel;     /* dimming level as duty cycle, 0 to 100 percent */
  struct thread_parameter * parameter = (struct thread_parameter *)arg;

  pthread_mutex_lock( &(parameter->done->lock) );
  while (!(parameter->done->done))
  {
    pthread_mutex_unlock( &(parameter->done->lock) );

    pthread_mutex_lock( &(parameter->pause->lock) );
    while (parameter->pause->pause)
    {
      pthread_mutex_unlock( &(parameter->pause->lock) );
      usleep( 10000 ); /* 10ms */
      pthread_mutex_lock( &(parameter->pause->lock) );
    }
    pthread_mutex_unlock( &(parameter->pause->lock) );

    DLevel = 0;  /* dim up, sweep the light level from 0 to 100 */
    while(DLevel<PWM_RANGE)
    {
      if (parameter->pin == 12)
      {
        parameter->pwm->DAT1 = DLevel;
      }
      else
      {
        parameter->pwm->DAT2 = DLevel;
      }
      usleep( Tlevel * Tstep * 100 );
      DLevel = DLevel + 1;
    }

    

    DLevel = PWM_RANGE;  /* dim down, sweep the light level from 100 to 0 */
    while(DLevel>0)
    {
      if (parameter->pin == 12)
      {
        parameter->pwm->DAT1 = DLevel;
      }
      else
      {
        parameter->pwm->DAT2 = DLevel;
      }
      usleep( Tlevel * Tstep * 100 );
      DLevel = DLevel - 1;
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
      case 'q':
        done = true;
        /* unpause everything */
        pthread_mutex_lock( &(thread_key_parameter->pause1->lock) );
        thread_key_parameter->pause1->pause = false;
        pthread_mutex_unlock( &(thread_key_parameter->pause1->lock) );
        pthread_mutex_lock( &(thread_key_parameter->pause2->lock) );
        thread_key_parameter->pause2->pause = false;
        pthread_mutex_unlock( &(thread_key_parameter->pause2->lock) );
        pthread_mutex_lock( &(thread_key_parameter->pause3->lock) );
        thread_key_parameter->pause3->pause = false;
        pthread_mutex_unlock( &(thread_key_parameter->pause3->lock) );
        pthread_mutex_lock( &(thread_key_parameter->pause4->lock) );
        thread_key_parameter->pause4->pause = false;
        pthread_mutex_unlock( &(thread_key_parameter->pause4->lock) );

        /* indicate that it is time to shut down */
        pthread_mutex_lock( &(thread_key_parameter->done->lock) );
        thread_key_parameter->done->done = true;
        pthread_mutex_unlock( &(thread_key_parameter->done->lock) );
        break;
      case '1':
        pthread_mutex_lock( &(thread_key_parameter->pause1->lock) );
        thread_key_parameter->pause1->pause = !(thread_key_parameter->pause1->pause);
        printf( "thread 1 is %s\n", thread_key_parameter->pause1->pause ? "paused" : "unpaused" );
        pthread_mutex_unlock( &(thread_key_parameter->pause1->lock) );
        break;
      case '2':
        pthread_mutex_lock( &(thread_key_parameter->pause2->lock) );
        thread_key_parameter->pause2->pause = !(thread_key_parameter->pause2->pause);
        printf( "thread 2 is %s\n", thread_key_parameter->pause2->pause ? "paused" : "unpaused" );
        pthread_mutex_unlock( &(thread_key_parameter->pause2->lock) );
        break;
      case '3':
        pthread_mutex_lock( &(thread_key_parameter->pause3->lock) );
        thread_key_parameter->pause3->pause = !(thread_key_parameter->pause3->pause);
        printf( "thread 3 is %s\n", thread_key_parameter->pause3->pause ? "paused" : "unpaused" );
        pthread_mutex_unlock( &(thread_key_parameter->pause3->lock) );
        break;
      case '4':
        pthread_mutex_lock( &(thread_key_parameter->pause4->lock) );
        thread_key_parameter->pause4->pause = !(thread_key_parameter->pause4->pause);
        printf( "thread 4 is %s\n", thread_key_parameter->pause4->pause ? "paused" : "unpaused" );
        pthread_mutex_unlock( &(thread_key_parameter->pause4->lock) );
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
  volatile struct io_peripherals *io;
  pthread_t                       thread12_handle;
  pthread_t                       thread13_handle;
  pthread_t                       thread18_handle;
  pthread_t                       thread19_handle;
  pthread_t                       thread_key_handle;
  struct done_flag                done   = {PTHREAD_MUTEX_INITIALIZER, false};
  struct pause_flag               pause1 = {PTHREAD_MUTEX_INITIALIZER, false};
  struct pause_flag               pause2 = {PTHREAD_MUTEX_INITIALIZER, false};
  struct pause_flag               pause3 = {PTHREAD_MUTEX_INITIALIZER, false};
  struct pause_flag               pause4 = {PTHREAD_MUTEX_INITIALIZER, false};
  struct thread_parameter         thread12_parameter;
  struct thread_parameter         thread13_parameter;
  struct thread_parameter         thread18_parameter;
  struct thread_parameter         thread19_parameter;
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
    io->gpio.GPFSEL1.field.FSEL8 = GPFSEL_OUTPUT;
    io->gpio.GPFSEL1.field.FSEL9 = GPFSEL_OUTPUT;

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
    thread12_parameter.pause = &pause1;
    thread13_parameter.pin = 13;
    thread13_parameter.pwm = &(io->pwm);
    thread13_parameter.gpio = &(io->gpio);
    thread13_parameter.done = &done;
    thread13_parameter.pause = &pause2;
    thread18_parameter.pin = 18;
    thread18_parameter.pwm = &(io->pwm);
    thread18_parameter.gpio = &(io->gpio);
    thread18_parameter.done = &done;
    thread18_parameter.pause = &pause3;
    thread19_parameter.pin = 19;
    thread19_parameter.pwm = &(io->pwm);
    thread19_parameter.gpio = &(io->gpio);
    thread19_parameter.done = &done;
    thread19_parameter.pause = &pause4;
    thread_key_parameter.done = &done;
    thread_key_parameter.pause1 = &pause1;
    thread_key_parameter.pause2 = &pause2;
    thread_key_parameter.pause3 = &pause3;
    thread_key_parameter.pause4 = &pause4;
    pthread_create( &thread12_handle, 0, ThreadHW, (void *)&thread12_parameter );
    pthread_create( &thread13_handle, 0, ThreadHW, (void *)&thread13_parameter );
    pthread_create( &thread18_handle, 0, ThreadSW, (void *)&thread18_parameter );
    pthread_create( &thread19_handle, 0, ThreadSW, (void *)&thread19_parameter );
    pthread_create( &thread_key_handle, 0, ThreadKey, (void *)&thread_key_parameter );
    pthread_join( thread12_handle, 0 );
    pthread_join( thread13_handle, 0 );
    pthread_join( thread18_handle, 0 );
    pthread_join( thread19_handle, 0 );
    pthread_join( thread_key_handle, 0 );
  }
  else
  {
    ; /* warning message already issued */
  }

  return 0;
}

