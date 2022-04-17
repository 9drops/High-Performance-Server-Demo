#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_WORKERS 4
#define true 1
#define false 0
typedef char bool;
typedef struct sockaddr *SA;

#define LOG_DEBUG
#ifdef LOG_DEBUG
#define log_err(fmt, args...) \
printf(fmt, ##args)

#define log_info(fmt, args...) \
printf(fmt, ##args)

#define log_debug(fmt, args...) \
printf(fmt, ##args)

#define log_warn(fmt, args...) \
printf(fmt, ##args)
#else
#define log_err(fmt, args...)
#define log_info(fmt, args...)
#define log_debug(fmt, args...)
#endif

#define err_exit() \
do { \
    printf("%s:%d\n", __func__, __LINE__); \
    exit(-1); \
} while(0)

static volatile sig_atomic_t srv_shutdown = 0;
static volatile sig_atomic_t graceful_shutdown = 0;


static int open_dev_null(int fd)
{
	int tmpfd;
	close(fd);
	tmpfd = open("/dev/null", O_RDWR);

	if (tmpfd != -1 && tmpfd != fd) {
		dup2(tmpfd, fd);
		close(tmpfd);
	}
	
	return (tmpfd != -1) ? 0 : -1;
}

static void daemonize() 
{
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);

	if (0 != fork()) exit(0);

	if (-1 == setsid()) exit(0);

	signal(SIGHUP, SIG_IGN);

	if (0 != fork()) exit(0);

	if (0 != chdir("/")) exit(0);

}


static bool already_running(const char *pid_file)
{
	int fd_lock;
	char sz_pid[32];
	struct flock fl;

	fd_lock = open(pid_file, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (-1 == fd_lock) {
		log_err("open %s failed\n", pid_file);
		exit(EXIT_FAILURE);
	}

	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	if (-1 == fcntl(fd_lock, F_SETLK, &fl)) {
		if (EACCES == errno || EAGAIN == errno) {
			close(fd_lock);
			return true;
		}

		log_err("fcntl %s error\n", pid_file);
		exit(EXIT_FAILURE);
	}
	
	ftruncate(fd_lock, 0);
	snprintf(sz_pid, sizeof(sz_pid), "%d\n", getpid());
	write(fd_lock, sz_pid, strlen(sz_pid));
	return false;
}

static void signal_handler(int sig)
{
	switch (sig) {
		case SIGTERM:
			srv_shutdown = 1;
			break;
		case SIGINT:
			if (graceful_shutdown)
				srv_shutdown = 1;
			else
				graceful_shutdown = 1;
			break;
		case SIGCHLD:
			break;
		defalut:
			break;
			
	}
}


int network_init(const char *host, short port)
{
	int sockfd, reuse;
    struct sockaddr_in srv_addr;

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        err_exit();
    }   

    
#ifdef FD_CLOEXEC
	fcntl(sockfd, F_SETFD, FD_CLOEXEC);
#endif
    reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) {
        err_exit();
    }   




    bzero(&srv_addr, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);
	if (NULL == host)
		srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	else {
			struct hostent *he;
			if (NULL == (he = gethostbyname(host))) {
				log_err("gethostbyname failed\n");
				 err_exit();
			}

			if (he->h_addrtype != AF_INET) {
				log_err("he->h_addrtype != AF_INET\n");
				err_exit();
			}

			if (he->h_length != sizeof(struct in_addr)) {
				log_err("addr-length != sizeof(in_addr)\n");
				err_exit();
			}

			memcpy(&(srv_addr.sin_addr.s_addr), he->h_addr_list[0], he->h_length);
	}
		
    if (bind(sockfd, (const SA)&srv_addr, sizeof(srv_addr))) {
        err_exit();
    }   

    listen(sockfd, 1024);
	return sockfd;
}


int func(int sockfd) 
{
	ssize_t size;
	char buf[1024];
	if (-1 == (size = read(sockfd, buf, sizeof(buf)))) {
		log_err("read error\n");
		err_exit();
	}

	buf[size] = '\0';
	if (-1 == (write(sockfd, buf, size)))
	{
		log_err("write error\n");
		err_exit();
	}
	
	return 0;
}


int main(int argc, char **argv)
{
	short port;
	struct sockaddr_in cli_addr;
	int num_child, sockfd, connfd;
	socklen_t addr_len; 
	const char *host;

	if (argc != 2 && argc != 3) {
		printf("usage:%s port OR %s host port\n", argv[0], argv[0]);
		return 0;
	}

	if (2 == argc)
	{
		host = NULL;
		port = atoi(argv[1]);
	} else if (3 == argc) {
		host = argv[1];
		port = atoi(argv[2]);
	}

	open_dev_null(STDIN_FILENO);
	open_dev_null(STDOUT_FILENO);
	daemonize();

	if (already_running("/var/run/srv_arch.pid"))
		return 1;

	signal(SIGINT,  signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP,  signal_handler);
	signal(SIGCHLD, signal_handler);

	sockfd = network_init(host, port);
	num_child = MAX_WORKERS;

	if (num_child > 0) {
		int child = 0;
		while (!child && !graceful_shutdown && !srv_shutdown) {
			if (num_child > 0) {
				switch(fork()) {
					case -1:
						return -1;
					case 0:
						child = 1;
						break;
					default:
						num_child--;
						break;
				}
			} else {
				int status;
				if (-1 != wait(&status)) {
					/*one of our workers went away*/
					num_child++;
				} else {
					switch (errno) {
						case EINTR:
							kill(0, SIGHUP);
							break;
						default:
							break;
					} /*end of switch*/
				} /*end of else*/
			}
		} /*end of while (!child)*/
			
		/*for the parent this is the exit-point*/
		if (!child) {
			if (graceful_shutdown)
				kill(0, SIGINT);
			else if (srv_shutdown)
				kill(0, SIGTERM);

			return 0;
		}

	} /*if (num_child > 0)*/
	
	while (!srv_shutdown) {
		bzero(&cli_addr, sizeof(cli_addr));
		if ((connfd = accept(sockfd, (SA)&cli_addr, &addr_len)) < 0) {
			log_err("accept failed\n");
			err_exit();
		}   
	
	#ifdef O_NONBLOCK
		fcntl(sockfd, F_SETFD, O_NONBLOCK | O_RDWR);
	#endif

		func(connfd);
		close(connfd);
	} /*end of while (!srv_shutdown)*/
	
	return 0;
}
