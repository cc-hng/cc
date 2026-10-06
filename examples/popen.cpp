#include <stdio.h>
#include <sys/wait.h>

#if 0
int main() {
    FILE *fp = popen("./build/examples/request aa:bb:cc:dd:ee:ff Hello", "r");

    if (!fp) {
        return 1;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        printf("%s", buf);
    }

    pclose(fp);
    return 0;
}
#endif

int main() {
    FILE *fp = popen("ls /not_exist", "r");

    if (!fp) {
        return 1;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        printf("%s", buf);
    }

    int status = pclose(fp);
    printf("status: %d, exit code: %d\n", status, WEXITSTATUS(status));

    return status;
}
