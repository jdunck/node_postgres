all: binding.node

CFLAGS := $(shell node_g --cflags)
LIBFLAGS := $(shell node_g --libs)

binding.o: binding.cc
	gcc ${CFLAGS} binding.cc -c -o binding.o

binding.node: binding.o
	gcc binding.o -o binding.node -shared -Wl,-Bdynamic -lpq

clean:
	rm -f binding.o binding.node

.PHONY: clean
