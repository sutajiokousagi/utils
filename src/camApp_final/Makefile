CC=arm-linux-gcc
LD=arm-linux-ld
AS=arm-linux-as

all: camApp_final

clean:
	rm -f camApp_final camApp_final.o

camApp_final: camApp_final.o
	$(CC) $(CFLAGS) -lpthread -I. -o $@ $? 

