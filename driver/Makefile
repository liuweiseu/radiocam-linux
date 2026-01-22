obj-m +=radiocam.o

KDIR:=/home/wei/Projects/opi5/linux-orangepi

PWD?=${shell pwd}

all:
	make -C ${KDIR} M=${PWD} modules

clean:
	make -C ${KDIR} M=${PWD} clean
