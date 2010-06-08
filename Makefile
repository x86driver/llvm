TARGET = jit
CC = g++
CFLAGS = -Wall

all:$(TARGET)

jit:jit.cpp
	$(CC) $(CFLAGS) -o $@ $< `llvm-config --ldflags --cflags --libs all`

clean:
	rm -rf $(TARGET)
