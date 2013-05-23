CC= gcc
CFLAGS= -Wall -O2

all:	ntpstat

ntpstat:	ntpstat.c

clean:
	rm ntpstat

