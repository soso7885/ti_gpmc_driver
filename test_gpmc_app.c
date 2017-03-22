#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#define DEV "/dev/pseudo"

int main(void)
{
	char buf[32];

	int fd = open(DEV, O_RDWR);
	if(fd == -1){
		printf("Open failed: %s\n", strerror(errno));
		return -1;
	}
	printf("Open success, fd = %d\n", fd);

	write(fd, "ABCDEFGH", 8);
	sleep(1);
	read(fd, buf, 32);
	printf("read : %s\n", buf);
	
	return 0;
}
