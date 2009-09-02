all: binding.node

CFLAGS := $(shell node_g --cflags) -I$(shell pg_config --includedir) -m32
LIBS := -L$(shell pg_config --libdir) -lpq

binding.o: binding.cc Makefile
	gcc ${CFLAGS} binding.cc -c -o binding.o

binding.node: binding.o Makefile
	gcc -shared -o binding.node  binding.o ${LIBS}

clean:
	rm -f binding.o binding.node

.PHONY: clean
