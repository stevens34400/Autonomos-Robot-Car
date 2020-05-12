#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include "import_registers.h"
#include "gpio.h"
#include "pwm.h"
#include "spi.h"
#include "LSM9DS1.h"

#define APB_CLOCK 250000000

#define DEN_PIN   18
#define CS_M_PIN  19
#define CS_AG_PIN 20

#define ROUND_DIVISION(x,y) (((x) + (y)/2)/(y))

struct pcm_register
{
    ; /* empty structure */
};

struct io_peripherals
{
  uint8_t               unused0[0x200000];
  struct gpio_register  gpio;             /* offset = 0x200000, width = 0x84 */
  uint8_t               unused1[0x3000-sizeof(struct gpio_register)];
  struct pcm_register   pcm;              /* offset = 0x203000, width = 0x24 */
  uint8_t               unused2[0x1000-sizeof(struct pcm_register)];
  struct spi_register   spi;              /* offset = 0x204000, width = 0x18 */
  uint8_t               unused3[0x8000-sizeof(struct spi_register)];
  struct pwm_register   pwm;              /* offset = 0x20c000, width = 0x28 */
};

void transact_SPI(                                /* send/receive SPI data */
    uint8_t const *                 write_data,   /* the data to send to a SPI device */
    uint8_t *                       read_data,    /* the data read from the SPI device */
    size_t                          data_length,  /* the length of data to send/receive */
    int                             CS_pin,       /* the pin to toggle when communicating */
    volatile struct gpio_register * gpio,         /* the GPIO address */
    volatile struct spi_register *  spi )         /* the SPI address */
{
  size_t  write_count;  /* used to index through the bytes to be sent/received */
  size_t  read_count;   /* used to index through the bytes to be sent/received */

  /* clear out anything in the RX FIFO */
  while (spi->CS.field.RXD != 0)
  {
    (void)spi->FIFO;
  }

  /* enable the chip select on the device */
  GPIO_CLR( gpio, CS_pin );
  usleep( 10 );

  /* see section 10.6.1 of the BCM2835 datasheet
   * Note that the loop below is a busy-wait loop and may burn a lot of clock cycles, depending on the amount of data to be transferred
   */
  spi->CS.field.TA = 1;
  write_count = 0;
  read_count  = 0;
  do
  {
    /* transfer bytes to the device */
    while ((write_count != data_length) &&
           (spi->CS.field.TXD != 0))
    {
      spi->FIFO = (uint32_t)*write_data;

      write_data++;
      write_count++;
    }

    /* drain the RX FIFO */
    while ((read_count != data_length) &&
           (spi->CS.field.RXD != 0))
    {
      if (read_data != NULL)
      {
        *read_data = spi->FIFO;

        read_data++;
        read_count++;
      }
      else
      {
        (void)spi->FIFO;

        read_count++;
      }
    }
  } while (spi->CS.field.DONE == 0);
  spi->CS.field.TA = 0;

  /* disable the chip select on the device */
  usleep( 10 );
  GPIO_SET( gpio, CS_pin );

  return;
}

void initialize_accelerometer_and_gyroscope(
    volatile struct spi_register *spi,
    volatile struct gpio_register*gpio )
{
  union  LSM9DS1_transaction  transaction;

  /*
   * read the who_am_i register... just for kicks
   * in CTRL_REG_G, enable the gyro (low speed, low rate is fine)
   * in CTRL_REG_G, set the output to the data register
   * the status register has flags for whether data is available or not... I probably need to wait for boot complete
   * in CTRL_REG, enable the accel (low speed, low G is fine), BLE set to Little Endian (just in case)
   */
  /* print WHO_AM_I */
  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_WHO_AM_I;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.WHO_AM_I), CS_AG_PIN, gpio, spi );
  printf( "WHOAMI (0x68) = 0x%2.2X\n", transaction.field.body.WHO_AM_I.SIX_EIGHT );

  /* in CTRL_REG1_G, enable the gyro (low speed, low rate is fine)
   * in CTRL_REG2_G, set the output to the data register
   */
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG1_G;
  transaction.field.body.CTRL_REG1_G.BW_G         = 0;
  transaction.field.body.CTRL_REG1_G.FS_G         = 0;
  transaction.field.body.CTRL_REG1_G.ODR_G        = 1;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG1_G), CS_AG_PIN, gpio, spi );
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG2_G;
  transaction.field.body.CTRL_REG2_G.OUT_SEL      = 0;
  transaction.field.body.CTRL_REG2_G.INT_SEL      = 0;
  transaction.field.body.CTRL_REG2_G.zero         = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG2_G), CS_AG_PIN, gpio, spi );

  /* in CTRL_REG, enable the accel (low speed, low G is fine), set block data update (race conditions are bad), BLE set to Little Endian (just in case), disable I2C
   */
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG4;
  transaction.field.body.CTRL_REG4.zero1          = 0;
  transaction.field.body.CTRL_REG4.Zen_G          = 1;
  transaction.field.body.CTRL_REG4.Yen_G          = 1;
  transaction.field.body.CTRL_REG4.Xen_G          = 1;
  transaction.field.body.CTRL_REG4.zero0          = 0;
  transaction.field.body.CTRL_REG4.LIR_XL1        = 0;
  transaction.field.body.CTRL_REG4.FOURD_XL1      = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG4), CS_AG_PIN, gpio, spi );
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG5_XL;
  transaction.field.body.CTRL_REG5_XL.DEC         = 0;
  transaction.field.body.CTRL_REG5_XL.Zen_XL      = 1;
  transaction.field.body.CTRL_REG5_XL.Yen_XL      = 1;
  transaction.field.body.CTRL_REG5_XL.Xen_XL      = 1;
  transaction.field.body.CTRL_REG5_XL.zero        = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG5_XL), CS_AG_PIN, gpio, spi );
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG6_XL;
  transaction.field.body.CTRL_REG6_XL.ODR_XL      = 1;
  transaction.field.body.CTRL_REG6_XL.FS_XL       = 0;
  transaction.field.body.CTRL_REG6_XL.BW_SCAL_ODR = 0;
  transaction.field.body.CTRL_REG6_XL.BW_XL       = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG6_XL), CS_AG_PIN, gpio, spi );
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG7_XL;
  transaction.field.body.CTRL_REG7_XL.HR          = 0;
  transaction.field.body.CTRL_REG7_XL.DCF         = 0;
  transaction.field.body.CTRL_REG7_XL.zero1       = 0;
  transaction.field.body.CTRL_REG7_XL.FDS         = 0;
  transaction.field.body.CTRL_REG7_XL.zero0       = 0;
  transaction.field.body.CTRL_REG7_XL.HPIS1       = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG7_XL), CS_AG_PIN, gpio, spi );
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG8;
  transaction.field.body.CTRL_REG8.BOOT           = 0;
  transaction.field.body.CTRL_REG8.BDU            = 0;
  transaction.field.body.CTRL_REG8.H_LACTIVE      = 0;
  transaction.field.body.CTRL_REG8.PP_OD          = 0;
  transaction.field.body.CTRL_REG8.SIM            = 0;
  transaction.field.body.CTRL_REG8.IF_ADD_INC     = 1;  /* you still have to set the M_S bit in the command */
  transaction.field.body.CTRL_REG8.BLE            = 0;  /* ARM processors default to Little Endian */
  transaction.field.body.CTRL_REG8.SW_RESET       = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG8), CS_AG_PIN, gpio, spi );
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG9;
  transaction.field.body.CTRL_REG9.zero1          = 0;
  transaction.field.body.CTRL_REG9.SLEEP_G        = 0;
  transaction.field.body.CTRL_REG9.zero0          = 0;
  transaction.field.body.CTRL_REG9.FIFO_TEMP_EN   = 0;
  transaction.field.body.CTRL_REG9.DRDY_mask_bit  = 0;
  transaction.field.body.CTRL_REG9.I2C_DISABLE    = 1;
  transaction.field.body.CTRL_REG9.FIFO_EN        = 0;
  transaction.field.body.CTRL_REG9.STOP_ON_FTH    = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG9), CS_AG_PIN, gpio, spi );
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG10;
  transaction.field.body.CTRL_REG10.zero1         = 0;
  transaction.field.body.CTRL_REG10.ST_G          = 0;
  transaction.field.body.CTRL_REG10.zero0         = 0;
  transaction.field.body.CTRL_REG10.ST_XL         = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG10), CS_AG_PIN, gpio, spi );

  return;
}

void initialize_magnetometer(
    volatile struct spi_register *spi,
    volatile struct gpio_register*gpio )
{
  union  LSM9DS1_transaction  transaction;

  /* print WHO_AM_I */
  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_WHO_AM_I_M;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.WHO_AM_I_M), CS_M_PIN, gpio, spi );
  printf( "WHOAMI_M (0x3D) = 0x%2.2X\n", transaction.field.body.WHO_AM_I_M.THREE_D);

  /*
   * in CTRL_REG1_M, no temperature compensation, enable X/Y, run in medium performance at 10Hz
   * in CTRL_REG2_M, use 4 gauss scale
   * in CTRL_REG3_M, run in continuous conversion, disable I2C, disable low-power (leave SIM default)
   * in CTRL_REG4_M, enable Z with Little Endian (just in case)
   * in CTRL_REG5_M, enable block data updates (race conditions are bad)
   */
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG1_M;
  transaction.field.body.CTRL_REG1_M.TEMP_COMP    = 0;
  transaction.field.body.CTRL_REG1_M.OM           = 1;
  transaction.field.body.CTRL_REG1_M.DO           = 4;
  transaction.field.body.CTRL_REG1_M.ST           = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG1_M), CS_M_PIN, gpio, spi );
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG2_M;
  transaction.field.body.CTRL_REG2_M.zero2        = 0;
  transaction.field.body.CTRL_REG2_M.FS           = 0;
  transaction.field.body.CTRL_REG2_M.zero1        = 0;
  transaction.field.body.CTRL_REG2_M.REBOOT       = 0;
  transaction.field.body.CTRL_REG2_M.SOFT_RST     = 0;
  transaction.field.body.CTRL_REG2_M.zero0        = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG2_M), CS_M_PIN, gpio, spi );
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG3_M;
  transaction.field.body.CTRL_REG3_M.I2C_DISABLE  = 1;
  transaction.field.body.CTRL_REG3_M.zero1        = 0;
  transaction.field.body.CTRL_REG3_M.LP           = 0;
  transaction.field.body.CTRL_REG3_M.zero0        = 0;
  transaction.field.body.CTRL_REG3_M.SIM          = 0;
  transaction.field.body.CTRL_REG3_M.MD           = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG3_M), CS_M_PIN, gpio, spi );
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG4_M;
  transaction.field.body.CTRL_REG4_M.zero1        = 0;
  transaction.field.body.CTRL_REG4_M.OMZ          = 1;
  transaction.field.body.CTRL_REG4_M.BLE          = 0;
  transaction.field.body.CTRL_REG4_M.zero0        = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG4_M), CS_M_PIN, gpio, spi );
  transaction.field.command.READ                  = 0;
  transaction.field.command.M_S                   = 0;
  transaction.field.command.AD                    = LSM9DS1_REGISTER_CTRL_REG5_M;
  transaction.field.body.CTRL_REG5_M.zero1        = 0;
  transaction.field.body.CTRL_REG5_M.BDU          = 1;
  transaction.field.body.CTRL_REG5_M.zero0        = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.CTRL_REG5_M), CS_M_PIN, gpio, spi );

  return;
}

void read_accelerometer(
    volatile struct spi_register *spi,
    volatile struct gpio_register*gpio )
{
  union  LSM9DS1_transaction  transaction;
  union uint16_to_2uint8      OUT_XL_X;
  union uint16_to_2uint8      OUT_XL_Y;
  union uint16_to_2uint8      OUT_XL_Z;

  /*
   * poll the status register and it tells you when it is done
   * Once it is done, read the data registers and the next conversion starts
   */
  do
  {
    usleep( 10000 );  /* sleeping a little too long should not hurt */

    transaction.field.command.READ  = 1;
    transaction.field.command.M_S   = 0;
    transaction.field.command.AD    = LSM9DS1_REGISTER_STATUS_REG;
    transaction.value[1]            = 0;
    transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.STATUS_REG), CS_AG_PIN, gpio, spi );
  } while (transaction.field.body.STATUS_REG.XLDA == 0);

  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_X_L_XL;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_X_L_XL), CS_AG_PIN, gpio, spi );
  OUT_XL_X.field.L                = transaction.value[1];
  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_X_H_XL;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_X_H_XL), CS_AG_PIN, gpio, spi );
  OUT_XL_X.field.H                = transaction.value[1];

  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_Y_L_XL;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_Y_L_XL), CS_AG_PIN, gpio, spi );
  OUT_XL_Y.field.L                = transaction.value[1];
  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_Y_H_XL;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_Y_H_XL), CS_AG_PIN, gpio, spi );
  OUT_XL_Y.field.H                = transaction.value[1];

  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_Z_L_XL;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_Z_L_XL), CS_AG_PIN, gpio, spi );
  OUT_XL_Z.field.L                = transaction.value[1];
  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_Z_H_XL;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_Z_H_XL), CS_AG_PIN, gpio, spi );
  OUT_XL_Z.field.H                = transaction.value[1];

  printf( "Accel X: %.2f m/s^2\ty=%.2f m/s^2\tz=%.2f m/s^2\n",
      OUT_XL_X.signed_value*2.0/32768.0*9.80665,  /* 2g range, 16-bit signed fixed-point */
      OUT_XL_Y.signed_value*2.0/32768.0*9.80665,
      OUT_XL_Z.signed_value*2.0/32768.0*9.80665 );

  return;
}

void read_gyroscope(
    volatile struct spi_register *spi,
    volatile struct gpio_register*gpio )
{
  union  LSM9DS1_transaction  transaction;
  union uint16_to_2uint8      OUT_X_G;
  union uint16_to_2uint8      OUT_Y_G;
  union uint16_to_2uint8      OUT_Z_G;

  /*
   * poll the status register and it tells you when it is done
   * Once it is done, read the data registers and the next conversion starts
   */
  do
  {
    usleep( 10000 );  /* sleeping a little too long should not hurt */

    transaction.field.command.READ  = 1;
    transaction.field.command.M_S   = 0;
    transaction.field.command.AD    = LSM9DS1_REGISTER_STATUS_REG;
    transaction.value[1]            = 0;
    transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.STATUS_REG), CS_AG_PIN, gpio, spi );
  } while (transaction.field.body.STATUS_REG.GDA == 0);

  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_X_L_G;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_X_L_XL), CS_AG_PIN, gpio, spi );
  OUT_X_G.field.L                = transaction.value[1];
  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_X_H_G;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_X_H_XL), CS_AG_PIN, gpio, spi );
  OUT_X_G.field.H                = transaction.value[1];

  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_Y_L_G;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_Y_L_XL), CS_AG_PIN, gpio, spi );
  OUT_Y_G.field.L                = transaction.value[1];
  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_Y_H_G;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_Y_H_XL), CS_AG_PIN, gpio, spi );
  OUT_Y_G.field.H                = transaction.value[1];

  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_Z_L_G;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_Z_L_XL), CS_AG_PIN, gpio, spi );
  OUT_Z_G.field.L                = transaction.value[1];
  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_Z_H_G;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_Z_H_XL), CS_AG_PIN, gpio, spi );
  OUT_Z_G.field.H                = transaction.value[1];

  printf( "Gyro X: %.2f dps\ty=%.2f dps\tz=%.2f dps\n",
      OUT_X_G.signed_value*245.0/32768.0,  /* 245dps range, 16-bit signed fixed-point */
      OUT_Y_G.signed_value*245.0/32768.0,
      OUT_Z_G.signed_value*245.0/32768.0 );

  return;
}

void read_magnetometer(
    volatile struct spi_register *spi,
    volatile struct gpio_register*gpio )
{
  union  LSM9DS1_transaction  transaction;
  union uint16_to_2uint8      OUT_X_M;
  union uint16_to_2uint8      OUT_Y_M;
  union uint16_to_2uint8      OUT_Z_M;

  /*
   * poll the status register and it tells you when it is done
   * Once it is done, read the data registers and the next conversion starts
   */
  do
  {
    usleep( 10000 );  /* sleeping a little too long should not hurt */

    transaction.field.command.READ  = 1;
    transaction.field.command.M_S   = 0;
    transaction.field.command.AD    = LSM9DS1_REGISTER_STATUS_REG_M;
    transaction.value[1]            = 0;
    transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.STATUS_REG_M), CS_M_PIN, gpio, spi );
  } while (transaction.field.body.STATUS_REG_M.ZYXDA == 0);

  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_X_L_M;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_X_L_M), CS_M_PIN, gpio, spi );
  OUT_X_M.field.L                = transaction.value[1];
  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_X_H_M;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_X_H_M), CS_M_PIN, gpio, spi );
  OUT_X_M.field.H                = transaction.value[1];

  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_Y_L_M;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_Y_L_M), CS_M_PIN, gpio, spi );
  OUT_Y_M.field.L                = transaction.value[1];
  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_Y_H_M;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_Y_H_M), CS_M_PIN, gpio, spi );
  OUT_Y_M.field.H                = transaction.value[1];

  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_Z_L_M;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_Z_L_M), CS_M_PIN, gpio, spi );
  OUT_Z_M.field.L                = transaction.value[1];
  transaction.field.command.READ  = 1;
  transaction.field.command.M_S   = 0;
  transaction.field.command.AD    = LSM9DS1_REGISTER_OUT_Z_H_M;
  transaction.value[1]            = 0;
  transact_SPI( transaction.value, transaction.value, sizeof(transaction.field.command)+sizeof(transaction.field.body.OUT_Z_H_M), CS_M_PIN, gpio, spi );
  OUT_Z_M.field.H                = transaction.value[1];

  printf( "Mag X: %.2f gauss\ty=%.2f gauss\tz=%.2f gauss\n",
      OUT_X_M.signed_value*4.0/32768.0,  /* 4 gauss range, 16-bit signed fixed-point */
      OUT_Y_M.signed_value*4.0/32768.0,
      OUT_Z_M.signed_value*4.0/32768.0 );

  return;
}

int main( void )
{
  volatile struct io_peripherals *io;
  unsigned int                    i;

  io = import_registers();
  if (io != NULL)
  {
    /* print where the I/O memory was actually mapped to */
    printf( "mem at 0x%8.8X\n", (unsigned int)io );

    /* set the pin function to alternate function 0 for GPIO09 (SPI, MISO) */
    /* set the pin function to alternate function 0 for GPIO10 (SPI, MOSI) */
    /* set the pin function to alternate function 0 for GPIO11 (SPI, SCK) */
    /* set the pin function to output for GPIO18 (DEN_AG) */
    /* set the pin function to output for GPIO19 (CS_M) */
    /* set the pin function to output for GPIO20 (CS_AG) */
    io->gpio.GPFSEL0.field.FSEL9 = GPFSEL_ALTERNATE_FUNCTION0;
    io->gpio.GPFSEL1.field.FSEL0 = GPFSEL_ALTERNATE_FUNCTION0;
    io->gpio.GPFSEL1.field.FSEL1 = GPFSEL_ALTERNATE_FUNCTION0;
    io->gpio.GPFSEL1.field.FSEL8 = GPFSEL_OUTPUT;
    io->gpio.GPFSEL1.field.FSEL9 = GPFSEL_OUTPUT;
    io->gpio.GPFSEL2.field.FSEL0 = GPFSEL_OUTPUT;

    /* set initial output state */
    GPIO_SET(&(io->gpio), DEN_PIN);
    GPIO_SET(&(io->gpio), CS_M_PIN);
    GPIO_SET(&(io->gpio), CS_AG_PIN);
    usleep( 100000 );

    /* set up the SPI parameters */
    io->spi.CLK.field.CDIV = ((ROUND_DIVISION(250000000,4000000))>>1)<<1; /* this number must be even, so shift the LSb into oblivion */
    io->spi.CS.field.CS       = 0;
    io->spi.CS.field.CPHA     = 1;  /* clock needs to idle high and clock in data on the rising edge */
    io->spi.CS.field.CPOL     = 1;
    io->spi.CS.field.CLEAR    = 0;
    io->spi.CS.field.CSPOL    = 0;
    io->spi.CS.field.TA       = 0;
    io->spi.CS.field.DMAEN    = 0;
    io->spi.CS.field.INTD     = 0;
    io->spi.CS.field.INTR     = 0;
    io->spi.CS.field.ADCS     = 0;
    io->spi.CS.field.REN      = 0;
    io->spi.CS.field.LEN      = 0;
    /* io->spi.CS.field.LMONO */
    /* io->spi.CS.field.TE_EN */
    /* io->spi.CS.field.DONE */
    /* io->spi.CS.field.RXD */
    /* io->spi.CS.field.TXD */
    /* io->spi.CS.field.RXR */
    /* io->spi.CS.field.RXF */
    io->spi.CS.field.CSPOL0   = 0;
    io->spi.CS.field.CSPOL1   = 0;
    io->spi.CS.field.CSPOL2   = 0;
    io->spi.CS.field.DMA_LEN  = 0;
    io->spi.CS.field.LEN_LONG = 0;

    initialize_accelerometer_and_gyroscope( &(io->spi), &(io->gpio) );
    initialize_magnetometer( &(io->spi), &(io->gpio) );
    do
    {
      read_accelerometer( &(io->spi), &(io->gpio) );
      read_magnetometer( &(io->spi), &(io->gpio) );
      read_gyroscope( &(io->spi), &(io->gpio) );
      printf( "\n" );
    } while (!wait_key( 100, 0 ));
  }
  else
  {
    ; /* warning message already issued */
  }

  return 0;
}
