/* Some prototypes for extern functions. */

#include <linux/types.h>

#if !defined(__GNUC__) && !defined(__attribute__)
#define __attribute__(x)
				   compiler-advice feature. */
#endif

/* identify() is the only extern function used across two source files.  The
   others, though, were declared in hdparm.c with global scope; since other
   functions in that file have static (file) scope, I assume the difference is
   intentional. */
extern void identify (__u16 *id_supplied);

extern void usage_error(int out)    __attribute__((noreturn));
extern int main(int argc, char **argv) __attribute__((noreturn));
extern void no_scsi (void);
extern void no_xt (void);
extern void process_dev (char *devname);

struct local_hd_big_geometry {
       unsigned char	heads;
       unsigned char	sectors;
       unsigned int	cylinders;
       unsigned long	start;
};

struct hd_geometry {
      unsigned char	heads;
      unsigned char	sectors;
      unsigned short	cylinders;
      unsigned long	start;
};

