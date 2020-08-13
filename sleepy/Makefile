
all: sleepy sleepy_lib
.PHONY: all

sleepy: sleepy.c
	gcc -std=c11 -Og -g $^ -o $@

sleepy_lib: sleepy.c
	gcc -std=c11 -shared -fPIC -Og -g $^ -o $@

clean:
	rm -rf sleepy
.PHONY: clean
