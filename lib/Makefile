CC = gcc
CFLAGS = -fPIC
TARGET = libsdr.so
OBJS = sdr_config.o sdr_transfer.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -shared -o $(TARGET) $(OBJS)

sdr_config.o: sdr_config.c
	$(CC) -c $(CFLAGS) sdr_config.c -o sdr_config.o

sdr_transfer.o: sdr_transfer.c
	$(CC) -c $(CFLAGS) sdr_transfer.c -o sdr_transfer.o

clean:
	rm -f $(OBJS) $(TARGET)
