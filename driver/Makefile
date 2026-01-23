obj-m +=radiocam.o

KDIR:=/home/wei/Projects/opi5/orangepi-build/kernel/orange-pi-6.1-rk35xx

PWD?=${shell pwd}

all:
	make -C ${KDIR} M=${PWD} modules

clean:
	make -C ${KDIR} M=${PWD} clean
