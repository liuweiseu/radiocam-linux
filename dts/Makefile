DTC      ?= dtc
DTCFLAGS ?= -@ -I dts -O dtb

DTS := $(wildcard *.dts)
DTBO := $(DTS:.dts=.dtbo)

.PHONY: all clean

all: $(DTBO)

%.dtbo: %.dts
	$(DTC) $(DTCFLAGS) -o $@ $<

clean:
	rm -f *.dtbo
