CC=gcc
CFLAGS=-O2

datalink: datalink.o protocol.o lprintf.o crc32.o getopt.o
	gcc datalink.o protocol.o lprintf.o crc32.o getopt.o -o datalink.exe -lm -lwsock32

clean:
	del *.o datalink.exe *.log