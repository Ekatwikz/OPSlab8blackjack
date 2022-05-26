#define ERR_MULTIPROCESS 0
#define USAGE_STRING "serverAddress clientAddress port"
#include "katwikOpsys.h"

//#define SERVER_ADDRESS "127.0.4.20"
//#define CLIENT_ADDRESS "127.0.0.69"

#define DESIRED_PORT 3500
#define BUFSIZE 50

#define THREAD_COUNT 2

volatile sig_atomic_t sigint_received = 0;
void sigint_handler(int sig) {
	UNUSED(sig);
	sigint_received = 1;
}

typedef struct ThreadArgs_ {
	int clientSock;

	bool* gameOver;
	pthread_mutex_t* gameOverMutex;
} ThreadArgs;

void* senderThread(void* voidArgs) {
	ThreadArgs* args = (ThreadArgs*) voidArgs;

	while (true) {
		pause(); // tmp!!

		pthread_mutex_lock_(args->gameOverMutex);
		if (*args->gameOver) {
			pthread_mutex_unlock_(args->gameOverMutex);
			break;
		} else pthread_mutex_unlock_(args->gameOverMutex);

		// send something to the server
		char sendBuf[BUFSIZE] = {0};
		sprintf_(sendBuf, "Hello from client!\n");
		send_(args->clientSock, sendBuf, strlen(sendBuf), 0);
	}

	return NULL;
}

void* receiverThread(void* voidArgs) {
	ThreadArgs* args = (ThreadArgs*) voidArgs;

	while (true) {
		// receive something from the server and clean it up
		char recvBuf[BUFSIZE] = {0};

		if (!recv_(args->clientSock, recvBuf, BUFSIZE, 0)) {
			printf_("Server DC, receiver thread exiting\n");
			break;
		} else if (!strcmp(removeNewline(recvBuf), "BYE"))
		{ // replace ^that with actual game-end condition
			printf_("Game over, receiver thread exiting\n");

			pthread_mutex_lock_(args->gameOverMutex);
			*args->gameOver = true;
			pthread_mutex_unlock_(args->gameOverMutex);

			break;
		}

		printf_("Received: \"%s\"\n", removeNewline(recvBuf));
	}

	return NULL;
}

int main(int argc, char** argv) {
	USAGE(argc == 4);
	int port = atoi(argv[3]);
	USAGE(port > 0);

	// setup the address we'll connect to
	struct sockaddr_in serverAddr = make_sockaddr_in( AF_INET, htons(port),
			inet_addr_(argv[1])
			);

	// setup the address we'll connect to
	struct sockaddr_in clientAddr = make_sockaddr_in( AF_INET, htons(port),
			inet_addr_(argv[2])
			);

	// setup our socket, we'll also use this to connect
	int clientSock = socket_(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// make the server socket reusable
	int reuse = 1;
	setsockopt_(clientSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));

	// bind our socket to our address
	bind_(clientSock, (struct sockaddr *) &clientAddr, sizeof(clientAddr));

	// conect to the server through our socket
	ERR_NEG1(connect(clientSock, (struct sockaddr *) &serverAddr, sizeof(serverAddr)));

	// thread setup
	pthread_t* threads = malloc_(THREAD_COUNT * sizeof(pthread_t));
	pthread_attr_t threadAttr = pthread_attr_make();
	ThreadArgs* threadArgs = calloc_(1, THREAD_COUNT * sizeof(ThreadArgs));

	// messy but this its already late
	bool gameOver = false;
	pthread_mutex_t gameOverMutex = pthread_mutex_make();
	threadArgs[0].clientSock = clientSock;
	threadArgs[0].gameOver = &gameOver;
	threadArgs[0].gameOverMutex = &gameOverMutex;
	threadArgs[1].clientSock = clientSock;
	threadArgs[1].gameOver = &gameOver;
	threadArgs[1].gameOverMutex = &gameOverMutex;

	pthread_create_(&threads[0], &threadAttr, &senderThread, &threadArgs[0]);
	pthread_create_(&threads[1], &threadAttr, &receiverThread, &threadArgs[1]);

	// wait until either SIGINT
	// or game over
	while (!sigint_received) {
		pthread_mutex_lock_(&gameOverMutex);
		if (gameOver) {
			pthread_mutex_unlock_(&gameOverMutex);
			break;
		} else pthread_mutex_unlock_(&gameOverMutex);
	}

	// cancel threads and wait for them to finish
	for (int i = 0; i < THREAD_COUNT; ++i) {
		pthread_join_(threads[i], NULL);
	}

	// cleanup and exit
	FREE(threads);
	FREE(threadArgs);
	close_(clientSock);
	return EXIT_SUCCESS;
}
