TOP := $(shell /bin/pwd)

LIBS   = -lm -lz 
#CC     = c++
CC     = gcc
CFLAGS = -g -Wall
RM     = rm -f
INC    = -I$(TOP)


default: all

all: scfastq

scfastq: scfastq.c
	$(CC) $(CFLAGS) $(LIBS) -o scfastq scfastq.c

clean :
	$(RM) scfastq
	$(RM) -r scfastq.dSYM


