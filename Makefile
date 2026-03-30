CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g
LDFLAGS = -lm

compare: compare.c
	$(CC) $(CFLAGS) compare.c -o compare $(LDFLAGS)

clean:
	rm -f compare