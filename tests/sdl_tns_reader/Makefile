CC := gcc
CFLAGS := -Wall -Wextra -O2 $(shell pkg-config --cflags sdl2)
LDFLAGS := $(shell pkg-config --libs sdl2)

TARGET := sdl_tns_reader
SRC := sdl_tns_reader.c

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) $(CFLAGS) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) ../../resources/Brasilian_Skies.tns

clean:
	rm -f $(TARGET)
