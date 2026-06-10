CC = gcc

CFLAGS = -Wall -Wextra -O2 -std=c99 -Iapi

LIBS = -lm -lrt

TARGET = aplicacao

SRCS = \
	src/main.c \
	src/util.c \
	src/modo_arquivo.c \
	src/modo_desenho.c \
	src/modo_benchmark.c \
	driver/rotinas.s \
	driver/instrucoes.s

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
