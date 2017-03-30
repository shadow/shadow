/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

int run_test(int argc, char *argv[]);

int main(int argc, char *argv[]) {
    /* if any children fail, this test succeeds */
    int testresult = run_test(argc, argv);
    if(testresult == 0) {
        return -1;
    } else {
        return 0;
    }
}
