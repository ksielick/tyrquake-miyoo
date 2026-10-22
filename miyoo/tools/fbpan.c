/* fbpan: shows /dev/fb0 from the given y offset (default 0): FBIOPAN_DISPLAY, nothing else.
 *   fbpan [yoffset]
 * Prints the offset before and after. */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
   struct fb_var_screeninfo v;
   int fd = open("/dev/fb0", O_RDWR);
   if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &v))
   {
      perror("/dev/fb0");
      return 1;
   }
   printf("before: %ux%u virtual %ux%u offset %u,%u\n", v.xres, v.yres, v.xres_virtual,
         v.yres_virtual, v.xoffset, v.yoffset);
   v.xoffset = 0;
   v.yoffset = argc > 1 ? (unsigned)atoi(argv[1]) : 0;
   if (ioctl(fd, FBIOPAN_DISPLAY, &v))
   {
      perror("FBIOPAN_DISPLAY");
      return 1;
   }
   ioctl(fd, FBIOGET_VSCREENINFO, &v);
   printf("after: offset %u,%u\n", v.xoffset, v.yoffset);
   close(fd);
   return 0;
}
