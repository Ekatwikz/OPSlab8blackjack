#define ERR_MULTIPROCESS 0
#define USAGE_STRING "serverAddress port threadCount"

// to make a queue of Game structs
#define LIST_TYPE void*
#include "katwikOpsys.h"

#define BUFSIZE 50
#define BACKLOG 2

volatile sig_atomic_t sigint_received = 0;
void sigint_handler(int sig) {
	UNUSED(sig);
	sigint_received = 1;
}

typedef struct Game_ {
	int player1Socket, player2Socket;
	struct sockaddr_in *player1Addr, *player2Addr;
	int player1Msg, player2Msg;
} Game;

typedef struct ThreadArgs_ {
	int threadNum;
	MyList* gameQueue;
	pthread_mutex_t* gameQueueMutex;
	sem_t* newGameSem;
} ThreadArgs;

void* threadFunc(void* voidArgs) {
	pthread_setcanceltype_(PTHREAD_CANCEL_DEFERRED, NULL);
	ThreadArgs* args = (ThreadArgs*) voidArgs;

	// each thread will know in advance whether it handles
	// Player 1 or player 2
	bool handlesPlayer1;
	if (args->threadNum % 2) {
		handlesPlayer1 = true;
	} else {
		handlesPlayer1 = false;
	}

	while (true) {
		// wait for a new game
		sem_wait(args->newGameSem);

		// get info about the new game
		pthread_mutex_lock_(args->gameQueueMutex);
		Game* game = popFirstVal(args->gameQueue);
		pthread_mutex_unlock_(args->gameQueueMutex);

		if (handlesPlayer1) {
			printf_("Thread %d signaled to handle %s as player 1\n",
					args->threadNum, inet_ntoa(game->player1Addr->sin_addr));
		} else {
			printf_("Thread %d signaled to handle %s as player 2\n",
					args->threadNum, inet_ntoa(game->player2Addr->sin_addr));
		}

		// we'll just say that the player 1 handler thread
		// is responsibe for freeing, so that we don't get heap-use-after-free
		// issues
		if (handlesPlayer1) {
			FREE(game->player1Addr);
			FREE(game->player2Addr);
			FREE(game);
		}
	}
}

int main(int argc, char** argv) {
	sethandler(sigint_handler, SIGINT);

	// setup arguments
	USAGE(argc == 4);
	int port = atoi(argv[2]);
	USAGE(port > 0);
	int threadCount = atoi(argv[3]);
	USAGE(threadCount > 0);

	// setup the address we'll bind to
	struct sockaddr_in serverAddr = make_sockaddr_in(AF_INET, htons(port),
			inet_addr_(argv[1])
			);

	// setup our socket
	int serverSocket = socket_(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// make the server socket reusable and non-blocking
	int reuse = 1;
	setsockopt_(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));

	// bind our socket to our address
	bind_(serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr));

	// listen for connections
	listen_(serverSocket, BACKLOG);

	// queue setup
	MyList* gameQueue = newMyList();
	pthread_mutex_t gameQueueMutex = pthread_mutex_make();

	// thread 1, 3, 5, ... etc will get Sem1
	//    '   2, 4, 6, ...       '      Sem2
	// b/c both lab samples said that we gotta have thread
	// per client, so we'll have to wake up multiple threads for
	// each new game
	sem_t newGameSem1 = sem_make(0);
	sem_t newGameSem2 = sem_make(0);

	// thread setup
	pthread_t* threads = malloc_(threadCount * sizeof(pthread_t));
	pthread_attr_t threadAttr = pthread_attr_make();
	ThreadArgs* threadArgs = malloc_(threadCount * sizeof(ThreadArgs));
	for (int i = 0; i < threadCount; ++i) {
		ThreadArgs args = {
			.threadNum = i + 1,
			.gameQueue = gameQueue,
			.gameQueueMutex = &gameQueueMutex,
			.newGameSem = i % 2 ? &newGameSem1 : &newGameSem2,
		};

		threadArgs[i] = args;
		pthread_create_(&threads[i], &threadAttr, &threadFunc, &threadArgs[i]);
	}

	// accept loop
	while (!sigint_received) {
		socklen_t clientAddrLen = sizeof(struct sockaddr_in);

		// start setting up new game
		Game* game = malloc(sizeof(Game));

		// accept 1st player
		game->player1Addr = malloc(clientAddrLen);
		game->player1Socket = ERR_NEG1_(
				accept(serverSocket, (struct sockaddr*) game->player1Addr,
					&clientAddrLen),
				EINTR);

		// if we got interrupted here we assume it's by SIGINT and we stop the loop
		if (EINTR == errno) {
			FREE(game->player1Addr);
			FREE(game);
			break;
		}

		// print player info
		printf_("Main thread accepted %s as player 1\n",
				inet_ntoa(game->player1Addr->sin_addr));

		// accept 2nd player
		game->player2Addr = malloc(clientAddrLen);
		game->player2Socket = ERR_NEG1_(
				accept(serverSocket, (struct sockaddr*) game->player2Addr,
					&clientAddrLen),
				EINTR);
		printf_("Main thread accepted %s as player 2\n",
				inet_ntoa(game->player2Addr->sin_addr));

		if (EINTR == errno) {
			FREE(game->player1Addr);
			FREE(game->player2Addr);
			FREE(game);
			break;
		}

		// insert pointer to game and signal 2 threads
		// we insert it twice so that both threads that are woken receive it
		pthread_mutex_lock_(&gameQueueMutex);
		insertValLast(gameQueue, game);
		insertValLast(gameQueue, game);
		pthread_mutex_unlock_(&gameQueueMutex);
		sem_post_(&newGameSem1);
		sem_post_(&newGameSem2);
	}

	// cancel threads and wait for them to finish
	for (int i = 0; i < threadCount; ++i) {
		pthread_cancel_(threads[i]);
		pthread_join_(threads[i], NULL);
	}

	// cleanup and exit
	printf_("\n"); // slightly tidier exit

	// free any remaining games in queue
	for (Game* game; myListLength(gameQueue);) {
		game = popFirstVal(gameQueue);
		FREE(game);
	}
	deleteMyList(gameQueue);

	sem_destroy_(&newGameSem1);
	sem_destroy_(&newGameSem2);
	pthread_mutex_destroy_(&gameQueueMutex);

	// cleanup and exit
	close_(serverSocket);
	return EXIT_SUCCESS;
}
