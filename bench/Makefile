STANDARD_FLAGS=--std=c11 -g
WARNING_FLAGS=-Wall -pedantic
OPT_FLAGS=-O2 -fno-omit-frame-pointer

COMPILE_FLAGS:=${STANDARD_FLAGS} ${WARNING_FLAGS} ${OPT_FLAGS}
LINKER_FLAGS=-lm
TARGET=workload
SRC=workload.c

all: ${TARGET}
.PHONY: all

${TARGET}: ${SRC} Makefile
	gcc ${COMPILE_FLAGS} ${SRC} -o $@ ${LINKER_FLAGS}

clean:
	rm -rf ${TARGET}
.PHONY: clean
