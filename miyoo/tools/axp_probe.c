/* axp_probe: reads the Miyoo Mini Plus's power chip (AXP221 on /dev/i2c-1, address 0x34) the way
 * Miyoo's /customer/app/axp_test does (I2C_RDWR: register number, then one byte back), read
 * only: no register is written.
 *
 *   axp_probe [samples] [interval_ms]
 *
 * Prints per sample: power status (0x00), charge status (0x01), ADC enable (0x82), battery %
 * (0xB9), battery voltage (0x78/0x79, 12 bit, 1.1 mV), and the raw charge (0x7A/0x7B) and
 * discharge (0x7C/0x7D) current ADC words, 12 and 13 bit readings, to find the scale.
 *
 *   axp_probe avg <samples> <interval_ms>
 *
 * One line: average battery mV, average / min / max discharge mA, average charge mA, over the
 * samples (power_log.sh). The current words are 12 bit, 1 mA each (AXP22x; the low byte uses
 * 4 bits): ~480 mA in Onion's menu with Wi-Fi on (2026-10-22). */
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define AXP_ADDR 0x34

static int fd;

static int rd(unsigned char reg)
{
   unsigned char val = 0;
   struct i2c_msg msgs[2] = {
      { AXP_ADDR, 0, 1, &reg },
      { AXP_ADDR, I2C_M_RD, 1, &val },
   };
   struct i2c_rdwr_ioctl_data d = { msgs, 2 };
   if (ioctl(fd, I2C_RDWR, &d) < 0)
      return -1;
   return val;
}

int main(int argc, char **argv)
{
   int avg = argc > 1 && !strcmp(argv[1], "avg");
   int n = argc > 1 + avg ? atoi(argv[1 + avg]) : 1;
   int ms = argc > 2 + avg ? atoi(argv[2 + avg]) : 1000;
   int i;

   fd = open("/dev/i2c-1", O_RDWR);
   if (fd < 0)
   {
      perror("/dev/i2c-1");
      return 1;
   }
   ioctl(fd, I2C_TIMEOUT, 5);
   ioctl(fd, I2C_RETRIES, 1);
   if (avg)
   {
      double mv = 0, ma = 0, cma = 0;
      int mn = 1 << 30, mx = 0, got = 0;
      for (i = 0; i < n; i++)
      {
         int vh = rd(0x78), vl = rd(0x79);
         int ch = rd(0x7A), cl = rd(0x7B), dh = rd(0x7C), dl = rd(0x7D);
         if (vh >= 0 && vl >= 0 && ch >= 0 && cl >= 0 && dh >= 0 && dl >= 0)
         {
            int d = (dh << 4) | (dl & 0x0f);
            mv += ((vh << 4) | (vl & 0x0f)) * 1.1;
            ma += d;
            cma += (ch << 4) | (cl & 0x0f);
            if (d < mn)
               mn = d;
            if (d > mx)
               mx = d;
            got++;
         }
         if (i + 1 < n)
            usleep(ms * 1000);
      }
      if (!got)
         return 1;
      printf("%.0f %.0f %d %d %.0f\n", mv / got, ma / got, mn, mx, cma / got);
      close(fd);
      return 0;
   }
   printf("t_ms,st00,st01,adc82,pct,vbat_mv,chg_hi,chg_lo,dis_hi,dis_lo,chg12,dis12,dis13\n");
   for (i = 0; i < n; i++)
   {
      struct timespec ts;
      int s0 = rd(0x00), s1 = rd(0x01), a = rd(0x82), p = rd(0xB9);
      int vh = rd(0x78), vl = rd(0x79);
      int ch = rd(0x7A), cl = rd(0x7B), dh = rd(0x7C), dl = rd(0x7D);
      clock_gettime(CLOCK_MONOTONIC, &ts);
      printf("%ld,0x%02x,0x%02x,0x%02x,%d,%.1f,0x%02x,0x%02x,0x%02x,0x%02x,%d,%d,%d\n",
            (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000), s0, s1, a, p & 0x7f,
            ((vh << 4) | (vl & 0x0f)) * 1.1, ch, cl, dh, dl,
            (ch << 4) | (cl & 0x0f), (dh << 4) | (dl & 0x0f), (dh << 5) | (dl & 0x1f));
      fflush(stdout);
      if (i + 1 < n)
         usleep(ms * 1000);
   }
   close(fd);
   return 0;
}
