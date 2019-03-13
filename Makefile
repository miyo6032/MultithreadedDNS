CC = gcc
INCLUDE = /usr/lib
LIBS = -lpthread
OBJS = 

multi-lookup: multi-lookup.c util.c
	$(CC) -Wall -Wextra -o multi-lookup multi-lookup.c util.c $(CFLAGS) $(LIBS)

clean:
	rm -f multi-lookup