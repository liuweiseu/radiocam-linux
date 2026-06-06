CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CPPFLAGS ?=
LDFLAGS ?=

TOOLS := radiocam_udp_sender radiocam_udp_recv

.PHONY: all clean

all: $(TOOLS)

radiocam_udp_sender: radiocam_udp_sender.c radiocam_udp.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ radiocam_udp_sender.c $(LDFLAGS)

radiocam_udp_recv: radiocam_udp_recv.c radiocam_udp.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ radiocam_udp_recv.c $(LDFLAGS)

clean:
	rm -f $(TOOLS)
