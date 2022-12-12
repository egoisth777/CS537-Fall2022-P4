CC     := gcc
CFLAGS := -Wall -Werror 

SRCS   := client.c \
	server.c 

OBJS   := ${SRCS:c=o}
PROGS  := ${SRCS:.c=}

.PHONY: all
all: ${PROGS}

${PROGS} : % : %.o Makefile
	${CC} $< -o $@ udp.c

clean:
	rm -f ${PROGS} ${OBJS}

%.o: %.c Makefile
	${CC} ${CFLAGS} -c $<

tst:
	gcc -fPIC -g -c -Wall fscli.c -o libmfs
	gcc -shared -Wl,-soname,libmfs.so -o libmfs.so libmfs -lc
	gcc fsserv.c -o server
	rm -f test.img
	./mkfs -f test.img