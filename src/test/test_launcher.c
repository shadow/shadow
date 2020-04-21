/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

int run_test(int argc, char *argv[]);

int main(int argc, char *argv[]) {
    /* if any children fail, this test fails */
    return run_test(argc, argv);
}
