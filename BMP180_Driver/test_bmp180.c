#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

int main() {
    int fd = open("/dev/bmp180", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    while (1) {
        int data[2]; // [0]: temperature, [1]: pressure

        if (ioctl(fd, 0, data) == -1) {
            perror("Failed to read data via ioctl");
            close(fd);
            return 1;
        }

        printf("Nhiệt độ: %.1f °C | Áp suất: %d Pa\n", data[0] / 10.0, data[1]);
        sleep(1);
    }

    close(fd);
    return 0;
}
