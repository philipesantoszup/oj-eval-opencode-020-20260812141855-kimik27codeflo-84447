CC = gcc
override CFLAGS += -w

.PHONY: all clean
all:
	$(CC) $(CFLAGS) -o code main.c buddy.c
clean:
	rm -f code test *.o
