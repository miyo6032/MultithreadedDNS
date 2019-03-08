CC = gcc
INCLUDE = /usr/lib
LIBS = -lpthread
OBJS = 

pa3: multi-lookup.c util.c
	$(CC) -o pa3 multi-lookup.c util.c $(CFLAGS) $(LIBS)

clean:
	rm -f pa3