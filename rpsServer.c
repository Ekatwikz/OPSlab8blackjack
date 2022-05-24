#define ERR_MULTIPROCESS 0
#define USAGE_STRING "serverAddress"
#include "katwikOpsys.h"

#define DESIRED_PORT 3500
#define BUFSIZE 50
#define BACKLOG 1

int main(int argc, char** argv) {
	USAGE(argc == 2);

	// setup the address we'll bind to
	struct sockaddr_in serverAddr = make_sockaddr_in(AF_INET, htons(DESIRED_PORT),
			inet_addr_(argv[1])
			);

	// setup our socket
	int serverSock = socket_(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// bind our socket to our address
	bind_(serverSock, (struct sockaddr *) &serverAddr, sizeof(serverAddr));

	// listen for up to 1 connection
	listen_(serverSock, BACKLOG);

	// accept a connection and printf some info about it
	struct sockaddr_in clientAddr = {0};
	socklen_t clientAddrLen = sizeof(struct sockaddr_in);
	int clientSock = accept_(serverSock, (struct sockaddr*) &clientAddr, &clientAddrLen);
	printf("Accepted %s\n", inet_ntoa(clientAddr.sin_addr));

	// send something to the client
	char sendBuf[BUFSIZE] = {0};
	sprintf(sendBuf, "HTTP/1.0 200 OK\r\n\r\nHello from server!");
	send_(clientSock, sendBuf, strlen(sendBuf), 0); // receive cient's message
	char recvBuf[BUFSIZE] = {0};
	recv_(clientSock, recvBuf, BUFSIZE, 0);
	printf("Received: \"%s\"\n", recvBuf);

	// cleanup and exit
	close_(clientSock);
	close_(serverSock);
	return EXIT_SUCCESS;
}
