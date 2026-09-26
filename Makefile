CC = gcc

CFLAGS = -std=gnu23 -O3 -fhardened -march=native \
	 -Wall -Wextra \
	 -Wstrict-prototypes -Wshadow \
	 -Wformat -Wformat-security \
	 -Warray-bounds -Wstringop-overflow \
	 -Wimplicit-fallthrough
#	 stb_ds is full of warnings, off for now
#	 -Wconversion -Wsign-conversion \
#	 -Wundef

CPPFLAGS = -Iinclude -I.

SRC = src/utils.c \
      src/server.c src/listener.c src/connection.c \
      src/http.c \
      src/stb_ds.c

OBJ = ${SRC:.c=.o}

TARGET = libowl.a

all: ${TARGET}
	
${TARGET}: ${OBJ}
	ar rcs $@ $^

.c.o:
	${CC} ${CPPFLAGS} ${CFLAGS} -c $< -o $@

clean:
	rm -f concert *.o src/*.o
