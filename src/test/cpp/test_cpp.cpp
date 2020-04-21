/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

typedef struct _Hello Hello;
struct _Hello {
    /* the epoll descriptor to which we will add our sockets.
     * we use this descriptor with epoll to watch events on our sockets. */
    int ed;

    /* track if our client got a response and we can exit */
    int isDone;

    /* storage for client mode */
    struct {
        int sd;
        char* serverHostName;
        int serverIP;
    } client;

    /* storage for server mode */
    struct {
        int sd;
    } server;
};
static struct _Hello my_hello = { 1,2, {3, 0, 4}, {5} };
static struct _Hello* my_hello_p = &my_hello;

class Test {
public:
   Test(int arg) : foo(2) {
     fromarg = arg;
   }
   int foo = 1;
   int fromarg = 3;
};

Test my_test(4);
Test* my_test_p = &my_test;

static int _test_init() {
    Test local_test(4);
    Test* local_test_p = &local_test;

    printf("hey %d\n", my_hello.server.sd);
	printf("hey %p %p\n", &my_hello, my_hello_p);
	printf("test %d %d\n", my_test.foo, my_test.fromarg);
	printf("test %p %p\n", &my_test, my_test_p);
	printf("local test %d %d\n", local_test.foo, local_test.fromarg);
	printf("local test %p %p\n", &local_test, local_test_p);

    if (my_hello.server.sd != 5) {
    	return EXIT_FAILURE;
    } else if (&my_hello != my_hello_p) {
    	return EXIT_FAILURE;
    } else if (my_test.foo != 2 || my_test.fromarg != 4) {
    	return EXIT_FAILURE;
    } else if (&my_test != my_test_p) {
    	return EXIT_FAILURE;
    } else if (local_test.foo != 2 || local_test.fromarg != 4) {
    	return EXIT_FAILURE;
    } else if (&local_test != local_test_p) {
    	return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int _test_endl() {
	std::string correct ("Line 1\nLine 2\nLine 3\n");

	std::stringstream test;
	test << "Line 1" << std::endl << "Line 2" << std::endl << "Line 3\n";

	std::cout << correct;
	std::cout << test.str();
	std::cout << "Line 1" << std::endl << "Line 2" << std::endl << "Line 3\n";
	std::cout << std::endl;

	if (correct.compare(test.str()) == 0) {
		return EXIT_SUCCESS;
	} else {
		return EXIT_FAILURE;
	}
}

static int _test_time() {
	auto t0 = std::chrono::system_clock::now();
	sleep(1);
	auto t1 = std::chrono::system_clock::now();

	if (std::chrono::duration_cast<std::chrono::seconds>(t1 - t0).count() == 0) {
		return EXIT_FAILURE;
	} else {
		return EXIT_SUCCESS;
	}
}

int main(void) {
    fprintf(stdout, "########## cpp test starting ##########\n");

    fprintf(stdout, "########## running test: _test_init()\n");

    if(_test_init() == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_init() failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## running test: _test_endl()\n");

    if(_test_endl() == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_endl() failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## running test: _test_time()\n");

    if(_test_time() == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_time() failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## cpp test passed! ##########\n");

    return EXIT_SUCCESS;
}
