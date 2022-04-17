#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define err_exit() \
do { \
    printf("%s:%d:%s\n", __func__, __LINE__, strerror(errno)); \
    exit(-1); \
} while(0)


int main(int argc, char **argv)
{
	int n, sockfd;
	char buf[128];
	struct sockaddr_in srv_addr;
	socklen_t addr_len;
	addr_len = sizeof(srv_addr);

	if (argc < 3) {
		printf("Usage:%s serverip port\n", argv[0]);
		exit(0);
	}

		bzero(&srv_addr, addr_len);
		srv_addr.sin_family = AF_INET;
		inet_pton(AF_INET, argv[1], &srv_addr.sin_addr); 
		srv_addr.sin_port = htons(atoi(argv[2]));
	
	while (1) {
		if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			err_exit();
		}

		if (connect(sockfd, (const struct sockaddr *)&srv_addr, addr_len)) {
			err_exit();
		}

		if (-1 == (n = read(fileno(stdin), buf, 128)))
			err_exit();
		buf[n] = '\0';

		if (-1 == write(sockfd, buf, n)) 
			err_exit();

		if ((n = read(sockfd, buf, 128)) < 0) {
			err_exit();
		}
	
		buf[n] = '\0';
		printf("recv:%s\n", buf);
		close(sockfd);
	}

	return 0;
}


