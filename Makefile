CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iapi -std=c99
LIBS = -lm -lrt

SRCS = src/main.c \
       src/util.c \
       src/modo_arquivo.c \
       src/modo_desenho.c \
       src/modo_benchmark.c \
       driver/rotinas.s \
       driver/instrucoes.s

TARGET = aplicacao

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
