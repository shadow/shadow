#include <unistd.h>
#include <stdio.h>


int main(int argc, char* argv[]) {
    char myname[255] = {0};
    if(gethostname(myname, 255) == 0){
        printf("%s\n", myname);
        return 0;
    } else {
        return 1;
    }
}
