/*
Copyright 2010 Sean Cross. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this list of
      conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice, this list
      of conditions and the following disclaimer in the documentation and/or other materials
      provided with the distribution.

THIS SOFTWARE IS PROVIDED BY SEAN CROSS ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SEAN CROSS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those of the
authors and should not be interpreted as representing official policies, either expressed
or implied, of Sean Cross.
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

struct reg_info {
    char *name;
    int offset;
    int size;
};

struct reg_info regs[] = {
#ifdef CNPLATFORM_stormwind
    #include "regutil_stormwind.h"
#elif defined(CNPLATFORM_silvermoon)
    #include "regutil_silvermoon.h"
#elif defined(CNPLATFORM_icecrown)
    #include "regutil_icecrown.h"
#elif defined(CNPLATFORM_falconwing)
    #include "regutil_falconwing.h"
#elif defined(CNPLATFORM_ironforge)
    #include "regutil_ironforge.h"
#else
    #warning Unrecognized CNPLATFORM set
#endif
    {NULL, 0}
};



//
// Converts from a friendly name (e.g. HW_PINCTRL_IRQSTAT2) to a register
// address.  On some platforms, also supports specialized control
// registers.
unsigned int register_address_from_name(char *name, int *size) {
    char             name_copy[strlen(name)+1];
    char             previous_character = '\0';
    int              offset_extra       = 0;
    char            *suffix;
    struct reg_info *reg = regs;

    // We make a copy of the name because there are special considerations
    // on platforms like the i.MX21, where every register has three extra
    // registers with suffixes of _SET, _CLR, and _TOG, that we'd like to
    // virtualize.
    strcpy(name_copy, name);

#ifdef CHUMBY_CONFIGNAME_falconwing
    if((suffix=strstr(name_copy, "_SET"))) {
        previous_character = '_';
        *suffix = '\0';
        offset_extra = 4;
    }
    else if((suffix = strstr(name_copy, "_CLR"))) {
        previous_character = '_';
        *suffix = '\0';
        offset_extra = 8;
    }
    else if((suffix = strstr(name_copy, "_TOG"))) {
        previous_character = '_';
        *suffix = '\0';
        offset_extra = 12;
    }
#endif



    while(reg->name) {
        if(!strcmp(reg->name, name_copy)) {
            if(size)
                *size = reg->size;
            return reg->offset+offset_extra;
        }
        reg++;
    }

    return 0;
}

static int fd = 0;
static int   *mem_32 = 0;
static short *mem_16 = 0;
static char  *mem_8  = 0;
static int *prev_mem_range = 0;


int read_kernel_memory(long offset, int virtualized, int size) {
    int result;

    // On falconwing, registers are located at 0xXXXXXXX0, the SET register
    // is located at 0xXXXXXXX4, the CLR register is at 0xXXXXXXX8, and the
    // TOG register is at 0xXXXXXXXC.  To set, clear, or toggle a bit,
    // write to the corresponding register.  These are write-only, so remap
    // reads to these registers on this platform to the root register.
#ifdef CHUMBY_CONFIGNAME_falconwing
// Disabled 3 May 2009 SMC - It's a nice feature in theory, but it turns
// out to cause more trouble than it's worth.
//    offset = offset & 0xFFFFFFF0;
#endif

    int *mem_range = (int *)(offset & ~0xFFFF);
    if( mem_range != prev_mem_range ) {
//        fprintf(stderr, "New range detected.  Reopening at memory range %p\n", mem_range);
        prev_mem_range = mem_range;

        if(mem_32)
            munmap(mem_32, 0xFFFF);
        if(fd)
            close(fd);

        if(virtualized) {
            fd = open("/dev/kmem", O_RDWR);
            if( fd < 0 ) {
                perror("Unable to open /dev/kmem");
                fd = 0;
                return -1;
            }
        }
        else {
            fd = open("/dev/mem", O_RDWR);
            if( fd < 0 ) {
                perror("Unable to open /dev/mem");
                fd = 0;
                return -1;
            }
        }

        mem_32 = mmap(0, 0xffff, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset&~0xFFFF);
        if( -1 == (int)mem_32 ) {
            perror("Unable to mmap file");

            if( -1 == close(fd) )
                perror("Also couldn't close file");

            fd=0;
            return -1;
        }
        mem_16 = (short *)mem_32;
        mem_8  = (char  *)mem_32;
    }

    int scaled_offset = (offset-(offset&~0xFFFF));
//    fprintf(stderr, "Returning offset 0x%08x\n", scaled_offset);
    if(size==1)
        result = mem_8[scaled_offset/sizeof(char)];
    else if(size==2)
        result = mem_16[scaled_offset/sizeof(short)];
    else
        result = mem_32[scaled_offset/sizeof(long)];

    return result;
}

int write_kernel_memory(long offset, long value, int virtualized, int size) {
    int old_value = read_kernel_memory(offset, virtualized, size);
    int scaled_offset = (offset-(offset&~0xFFFF));
    if(size==1)
        mem_8[scaled_offset/sizeof(char)]   = value;
    else if(size==2)
        mem_16[scaled_offset/sizeof(short)] = value;
    else
        mem_32[scaled_offset/sizeof(long)]  = value;
    return old_value;
}






void dumpregs() {
    struct reg_info *reg = regs;
    while(reg->name) {
#ifdef CHUMBY_CONFIGNAME_falconwing
        // Ignore SCT registers on falconwing when dumping.
        if(strstr(reg->name, "_TOG") 
        || strstr(reg->name, "_CLR")
        || strstr(reg->name, "_SET")) {
            reg++;
            continue;
        }
#endif
        printf("0x%08x: 0x%08x  %s\n",
                reg->offset, read_kernel_memory(reg->offset, 0, reg->size), reg->name);
        fflush(stdout);
        reg++;
    }
    return;
}


void print_usage(char *progname) {
    printf("Usage:\n"
        "%s [-d] [-v 1|0] [-w offset=value] [-r offset] [-x start end]\n"
        "\t-v  Start (or stop) regutil from using virtual memory\n"
        "\t-d  Dump all known registers\n"
        "\t-w  Set the register at offset [offset] to [value]\n"
        "\t-s  Set the bits in the register at offset [offset] to [value]\n"
        "\t-c  Clear the bits in the register at offset [offset] to [value]\n"
        "\t-r  Return the register at offset [offset]\n"
        "\t-x  Dump data from offset [start] to [end] inclusive\n"
        "\t-h  This help message\n"
        "", progname);
}


static inline int swab(int arg) {
    return ((arg&0xff)<<24) | ((arg&0xff00)<<8) | ((arg&0xff0000)>>8) | ((arg&0xff000000)>>24);
}

int main(int argc, char **argv) {
    int          dump_registers = 0;
    unsigned int read_offset    = 0;
    unsigned int write_offset   = 0;
    unsigned int write_value    = 0;
    int          virtualized    = 0;
    int          ch;

    char *prog = argv[0];
    argv++;
    argc--;

    if(!argc) {
        print_usage(prog);
        return 1;
    }


    while(argc > 0) {
        if(!strcmp(*argv, "-d")) {
            argc--;
            argv++;
            dumpregs();
        }
        else if(!strcmp(*argv, "-v")) {
            argc--;
            argv++;
            if(argc <= 0) {
                fprintf(stderr, "Error: Must set -v 0 or -v 1 to disable or enable virtualization\n");
                return 1;
            }
            virtualized = strtol(*argv, NULL, 0);
            argc--;
            argv++;
        }
        else if(!strcmp(*argv, "-w") 
             || !strcmp(*argv, "-c")
             || !strcmp(*argv, "-s")) {
            char *offset;
            char *value;
            int operation;
            int size = 4;
            int previous_value;
            if(!strcmp(*argv, "-c"))
                operation = 1;
            if(!strcmp(*argv, "-s"))
                operation = 2;

            argc--;
            argv++;
            if(argc <= 0) {
                fprintf(stderr, "Error: -r requires an argument\n");
                return 1;
            }

            offset = value = *argv;
            argv++;
            argc--;

            // Convert "name=value" to "name" and "value".
            while(*value && (*value)!='=')
                value++;
            if((*value)=='=') {
                (*value)='\0';
                value++;
            }
            else {
                fprintf(stderr, "Invalid offset=value\n");
                return 1;
            }
            write_offset = strtoul(offset, NULL, 0);
            write_value  = strtoul(value, NULL, 0);

            // If the offset is invalid, try a textual version.
            if(!write_offset)
                write_offset = register_address_from_name(offset, &size);

            // If it's still invalid, the user made a mistake.
            if(!write_offset) {
                fprintf(stderr, "Invalid write offset \"%s\"\n",offset);
                return 1;
            }

            write_offset &= ~3L;

            // Grab the previous value, and determine which operation to
            // perform.
            previous_value = read_kernel_memory(write_offset, virtualized, size);
            if(operation == 1)
                write_value = previous_value & ~write_value;
            else if(operation == 2)
                write_value = previous_value | write_value;

            printf("Setting 0x%08x: 0x%08x -> ", write_offset, previous_value);
            fflush(stdout);
            write_kernel_memory(write_offset, write_value, virtualized, size);
            printf("0x%08x ok\n", read_kernel_memory(write_offset, virtualized, size));
            fflush(stdout);
        }
        else if(!strcmp(*argv, "-r")) {
            int size = 4;
            argc--;
            argv++;
            if(argc <= 0) {
                fprintf(stderr, "Error: -w requires an argument\n");
                return 1;
            }
            read_offset = strtoul(*argv, NULL, 0);
            if(read_offset)
                read_offset &= ~3L;
            else
                read_offset = register_address_from_name(*argv, &size);

            if(!read_offset) {
                fprintf(stderr, "Invalid read offset \"%s\"\n", *argv);
                return 1;
            }
            argc--;
            argv++;


            printf("Value at 0x%08x: 0x%08x\n", read_offset,
                    read_kernel_memory(read_offset, virtualized, size));
            fflush(stdout);
        }

        else if(!strcmp(*argv, "-x")) {
            unsigned int start, end;
            argc--;
            argv++;

            if(argc < 2) {
                fprintf(stderr, "Need a start and an end offset\n");
                return 1;
            }

            start = strtoul(*argv, NULL, 0);
            argv++;
            argc--;
            end = strtoul(*argv, NULL, 0);
            argv++;
            argc--;

            start &= ~3L;
            end   &= ~3L;
            
            while(start <= end) {
                int data = swab(read_kernel_memory(start, virtualized, 4));
                start += 4;
                write(1, &data, sizeof(data));
            }
        }
        else {
            print_usage(prog);
            return 1;
        }
    }

    return 0;
}
