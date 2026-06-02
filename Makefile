CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iapi -std=c99
LIBS = -lm -lrt

SRCS = src/main.c driver/rotinas.s driver/instrucoes.s

TARGET = aplicacao

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean