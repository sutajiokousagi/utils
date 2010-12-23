// Fades in OL2 in a "Quake-style" fade in.  Useful for flashy demos.
// It's a quick hack, and needs a lot of work.
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>


#define HW_PXP_OL0PARAM 0x8002a220L
#define HW_PXP_OL0SIZE  0x8002a210L
#define HW_PXP_OL1PARAM 0x8002a260L
#define HW_PXP_OL1SIZE  0x8002a250L
#define HW_PXP_OL2PARAM 0x8002a2a0L
#define HW_PXP_OL2SIZE  0x8002a290L

// This is basically the core from regutil.
// There is a register available on the i.MX233 that reports the number of
// msecs since poweron.  If we can use this, it will save us a trip to
// kernel space when looking for a timer source.
#define KM_UNINITIALIZED 0
#define KM_INITIALIZED   1
#define KM_ERROR         2
static unsigned long *regutil_mem = NULL;
static int kernel_memory_status = KM_UNINITIALIZED;

static unsigned long read_kernel_memory(unsigned long offset) {
    static int regutil_fd = 0;
    static int last_offset = 0;
    static int last_scaled_offset;
    static unsigned long *regutil_prev_mem_range = NULL;
    static unsigned long *mem_ptr;
    unsigned long *mem_range;

    // Place this first so it will be encountered first, as this will
    // happen nearly 100% of the time.
    if(offset == last_offset)
        return *mem_ptr;

    mem_range = (unsigned long *)(offset & 0xFFFF0000L);
    if( mem_range != regutil_prev_mem_range ) {
        regutil_prev_mem_range = mem_range;

        if(regutil_mem)
            munmap(regutil_mem, 0xFFFF);
        if(regutil_fd)
            close(regutil_fd);

        regutil_fd = open("/dev/mem", O_RDWR);
        if( regutil_fd < 0 ) {
            perror("Unable to open /dev/mem");
            regutil_fd = 0;
            kernel_memory_status = KM_ERROR;
            return -1;
        }

        regutil_mem = (unsigned long *)mmap(0, 0xFFFF, PROT_READ | PROT_WRITE,
                            MAP_SHARED, regutil_fd, offset & 0xFFFF0000L);
        if( -1 == ((int)regutil_mem) ) {
            perror("Unable to mmap file");

            if( -1 == close(regutil_fd) )
                perror("Also couldn't close file");

            regutil_fd  = 0;
            regutil_mem = NULL;
            kernel_memory_status = KM_ERROR;
            return -1;
        }
        kernel_memory_status = KM_INITIALIZED;
    }

    last_scaled_offset = (offset-(offset & 0xFFFF0000L))/sizeof(long);
    last_offset = offset;
    mem_ptr = regutil_mem+last_scaled_offset;

    return *mem_ptr;
}

static unsigned long write_kernel_memory(unsigned long offset,
                                         unsigned long value) {
    unsigned long old_value = read_kernel_memory(offset);
    unsigned int scaled_offset = (offset-(offset & 0xFFFF0000L));
    if(regutil_mem)
        regutil_mem[scaled_offset/sizeof(long)] = value;
    return old_value;
}



static int do_fade_step(int step, int steps, int scr_height, int ol) {
    int alpha     = step*255/steps;
    int height    = step*(scr_height)/steps;
    int cur_param;
    int cur_size;
    int on        = (alpha)?1:0;
    unsigned long ol_param = HW_PXP_OL0PARAM;
    unsigned long ol_size  = HW_PXP_OL0SIZE;

    if(ol == 1) {
        ol_param = HW_PXP_OL1PARAM;
        ol_size  = HW_PXP_OL1SIZE;
    }
    else if(ol == 0) {
        ol_param = HW_PXP_OL2PARAM;
        ol_size  = HW_PXP_OL2SIZE;
    }

    cur_param = read_kernel_memory(ol_param);
    cur_size  = read_kernel_memory(ol_size);
    cur_param &= 0xffff00ffL;
    cur_size  &= 0xffffff00L;

    write_kernel_memory(ol_param, cur_param | (alpha<<8) | on);
    write_kernel_memory(ol_size,  cur_size  | height);

    usleep(1000);
    return 0;
}

// Fades in direction [direction] for [speed] msecs.
int do_fade(int direction, int speed, int ol) {
    int steps = speed;
    int step;
    int scr_height = strtol(getenv("VIDEO_Y_RES"), NULL, 0);
    if(!scr_height)
        scr_height = 240;
    scr_height /= 8;

    if(!direction)
        for(step=0; step<=steps; step++)
            do_fade_step(step, steps, scr_height, ol);
    else
        for(step=steps; step>=0; step--)
            do_fade_step(step, steps, scr_height, ol);

    return 0;
}

int main(int argc, char **argv) {
    int direction = 0;
    int speed     = 300;
    int ol        = 2;

    if(argc > 1)
        direction = strtol(argv[1], NULL, 0);

    if(argc > 2)
        speed = strtol(argv[2], NULL, 0);

    if(argc > 3)
        ol = strtol(argv[3], NULL, 0);

    return do_fade(direction, speed, ol);
}
