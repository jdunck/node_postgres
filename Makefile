all: binding.node

CFLAGS := $(shell node_g --cflags)
LIBFLAGS := $(shell node_g --libs)

binding.o: binding.cc Makefile
	gcc ${CFLAGS} binding.cc -c -o binding.o

binding.node: binding.o Makefile
	gcc -shared -o binding.node  binding.o  \
		-rdynamic 													  \
		-Wl,-R/home/ryan/local/node/lib 			\
		-lpq

clean:
	rm -f binding.o binding.node

.PHONY: clean
