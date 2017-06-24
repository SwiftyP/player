TARGET: player master

CC = gcc
CFLAGS = -Wall -O2

player: player.o err.o
	$(CC) $(CFLAGS) $^ -o $@

master: master.o err.o
	$(CC) $(CFLAGS) $^ -o $@ -lssh2 -pthread

.PHONY: clean TARGETS
clean:
	rm -f player master *.o *~ *.bak
