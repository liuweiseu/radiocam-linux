obj-m += radiocam.o

KDIR:=/home/wei/Projects/opi5/orangepi-build/kernel/orange-pi-6.1-rk35xx

PWD := $(shell pwd)

#   make           → Turn off debug
#   make DEBUG=1   → Turn on debug
ifeq ($(DEBUG),1)
    ccflags-y += -DDEBUG
endif

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
