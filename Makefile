all:

TARGET=smart-builder

CPPFLAGS=-Wall -g
LDFLAGS=-Wall

${TARGET}:	main.o
	${CXX} ${LDFLAGS} -o $@ $^

all:	${TARGET}

clean:
	rm -f *.o ${TARGET}
