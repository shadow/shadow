/* this should never actually get called, it is here so the real version in shd-interposer.c
 * can properly intercept it.
 */
int interposer_setShadowIsLoaded(int isLoaded) {
    return -1;
}
