#define ERR_MULTIPROCESS 0
#define USAGE_STRING "serverAddress port threadCount"

// to make a queue of Game structs
#define LIST_TYPE void*
#include "katwikOpsys.h"

#define MSG_LEN 50
#define BACKLOG 2

// if your question says "alternating moves is done using cond variables"
// or something similar, set this to 1,
// otherwise set 0
#define ROTATE_TURNS 0

// if your question says you should reject clients
// when there aren't enough threads, then you
// should set this to 1
// otherwise 0
// doesn't matter for us big boiz since we're using
// queues anyway, but just give them want they want lol
#define REJECT_EXCESS 1

volatile sig_atomic_t sigint_received = 0;
void sigint_handler(int sig) {
	UNUSED(sig);
	sigint_received = 1;
}

typedef struct Game_ {
	// we'll represent player messages with macro-d up ints
	int player1Msg, player2Msg;
#define MSG_DC -1 // client DC should be handled too
#define MSG_A 1
#define MSG_B 2

	int player1Socket, player2Socket;
	struct sockaddr_in *player1Addr, *player2Addr;

	// we'll sync games at various points
	pthread_barrier_t* barrier;

	// we might have to order game turns
	// and not just get messages in any order,
	// in that case we need a cond variable
#if ROTATE_TURNS
	int turn;
	pthread_mutex_t* turnMutex;
	pthread_cond_t* turnCond;
#endif // ROTATE_TURNS
} Game;

typedef struct ThreadArgs_ {
	int threadNum;

	MyList* gameQueue;
	pthread_mutex_t* gameQueueMutex;
	sem_t* newGameSem;

	int* availableThreads;
	pthread_mutex_t* availableThreadsMutex;
} ThreadArgs;

// easier to debug with lol:
#define SELF BWHITE"Thread %d"RESET_ESC
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

	// we handle new games until we're canceled
	// by main thread
	while (true) {
		// announce that this thread isn't busy
		pthread_mutex_lock_(args->availableThreadsMutex);
		++*args->availableThreads;
		pthread_mutex_unlock_(args->availableThreadsMutex);

		// wait for a new game
		sem_wait(args->newGameSem);

		// announce that this thread is now busy
		pthread_mutex_lock_(args->availableThreadsMutex);
		--*args->availableThreads;
		pthread_mutex_unlock_(args->availableThreadsMutex);

		// get the new game
		pthread_mutex_lock_(args->gameQueueMutex);
		Game* game = popFirstVal(args->gameQueue);
		pthread_mutex_unlock_(args->gameQueueMutex);

		// get info about the new game
		int playerSocket;
		struct sockaddr_in *playerAddr, *opponentAddr;
		if (handlesPlayer1) {
			playerSocket = game->player1Socket;
			playerAddr = game->player1Addr;
			opponentAddr = game->player2Addr;
		} else {
			playerSocket = game->player2Socket;
			playerAddr = game->player2Addr;
			opponentAddr = game->player1Addr;
		}

		printf_(SELF" signaled to handle %s as Player %d\n",
				args->threadNum, inet_ntoa(playerAddr->sin_addr),
				handlesPlayer1 ? 1 : 2);

		// send the player some info about the game,
		// eg: opponent IP here lol
		char msgBuf[MSG_LEN + 1] = {0}; // +1 extra byte for '\0'
		snprintf_(msgBuf, MSG_LEN, "Opponent addr: %s\n",
				inet_ntoa(opponentAddr->sin_addr));
		send_(playerSocket, msgBuf, MSG_LEN, 0);

		// main loop for game rounds
		// change condition to however the game should end
		// eg: here we """play""" 2 rounds of just receiving 'A' or 'B'
		for (int roundNum = 1; roundNum <= 2; ++roundNum) {
			// sync round start, might be useful
			pthread_barrier_wait_(game->barrier);
#if ROTATE_TURNS
			if (handlesPlayer1) {
				// player1 will be responsible for resetting round number
				// if we're using it
				game->turn = 1;
			}
#endif // ROTATE_TURNS
			pthread_barrier_wait_(game->barrier);

#if ROTATE_TURNS
			// for player 2, we've got to wait until it's our turn
			// this might need to be generalized if our question is changed
			// to have more than 2 players...
			if (!handlesPlayer1) {
				printf_(SELF" waiting to be signaled for turn\n",
						args->threadNum);

				pthread_mutex_lock_(game->turnMutex);
				while (game->turn != 2) {
					pthread_cond_wait(game->turnCond, game->turnMutex);
				}
				pthread_mutex_unlock_(game->turnMutex);

				printf_(SELF" signaled for turn\n",
						args->threadNum);
			}
#endif // ROTATE_TURNS

			// we'll loop until we get a usable message
			// from the client
			int playerMsg;
			while (true) {
				// receive message
				memset(msgBuf, '\0', MSG_LEN + 1);
				if (!recv_(playerSocket, msgBuf, MSG_LEN, 0)) {
					// player DC-d here
					playerMsg = MSG_DC;
					break;
				}

				// clean msg up a little
				// makes it easier to test with netcat/telnet
				removeNewline(msgBuf);
				printf_(SELF" received: \"%s\"\n",
						args->threadNum, msgBuf);

				// check the player's msg
				if (!strcmp(msgBuf, "A")) {
					playerMsg = MSG_A;
					break;
				} else if (!strcmp(msgBuf, "B")) {
					playerMsg = MSG_B;
					break;
				} else {
					printf_(SELF" requesting good message from player\n",
							args->threadNum);

					snprintf_(msgBuf, MSG_LEN, "Bruh send good msg lol\n");
					send_(playerSocket, msgBuf, MSG_LEN, 0);
				}
			}

			// ?announce? the player's message
			// so the other thread can see
			// NB: we DON'T need mutexes b/c the piles of barriers around here
			if (handlesPlayer1) {
				game->player1Msg = playerMsg;
			} else {
				game->player2Msg = playerMsg;
			}

#if ROTATE_TURNS
			// if we're sequencing turns,
			// player 1 has to tell player 2 to start
			// handling their guy, might also need to
			// generalize this
			if (handlesPlayer1) {
				printf_(SELF" signaling for next turn\n",
						args->threadNum);

				pthread_mutex_lock_(game->turnMutex);
				++game->turn;
				pthread_cond_broadcast_(game->turnCond);
				pthread_mutex_unlock_(game->turnMutex);
			}
#else // ROTATE_TURNS
			// sync player messages on barrier, if we're not using
			// cond variables
			printf_(SELF" waiting for other thread's msg\n",
					args->threadNum);
			pthread_barrier_wait_(game->barrier);
#endif // ROTATE_TURNS

			// check the opponents message
			int opponentMsg;
			if (handlesPlayer1) {
				opponentMsg = game->player2Msg;
			} else {
				opponentMsg = game->player1Msg;
			}

			// handle game state
			if (playerMsg == MSG_DC) {
				printf_(SELF"'s client DCd, game over\n",
						args->threadNum);

				break;
			} else if (opponentMsg == MSG_DC) {
				printf_(SELF"'s opponent DCd, game over\n",
						args->threadNum);

				snprintf_(msgBuf, MSG_LEN, "Opponent ragequit, gg lol\n");
				send_(playerSocket, msgBuf, MSG_LEN, 0);

				break;
			} else {
				snprintf_(msgBuf, MSG_LEN, "Round %d, You: %d, Opponent: %d\n",
						roundNum, playerMsg, opponentMsg);
				send_(playerSocket, msgBuf, MSG_LEN, 0);
			}
		}

		// cleanup should first sync
		pthread_barrier_wait_(game->barrier);

		// we'll just say that the player 1 handler thread
		// is responsibe for cleanup, so that we don't get heap-use-after-free
		// issues etc
		if (handlesPlayer1) {
			printf_(SELF": game ended, cleaning up\n",
					args->threadNum);

			close_(game->player1Socket);
			close_(game->player2Socket);
			pthread_barrier_destroy_(game->barrier);
#if ROTATE_TURNS
			pthread_mutex_destroy_(game->turnMutex);
			pthread_cond_destroy_(game->turnCond);
#endif // ROTATE_TURNS

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

	// make the server socket reusable
	int reuse = 1;
	setsockopt_(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));

	// bind our socket to our address
	bind_(serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr));

	// listen for connections
	listen_(serverSocket, BACKLOG);

	// game queue setup
	MyList* gameQueue = newMyList();
	pthread_mutex_t gameQueueMutex = pthread_mutex_make();

	// thread 1, 3, 5, ... etc will get Sem1
	//    '   2, 4, 6, ...       '      Sem2
	// b/c both lab samples said that we gotta have one thread
	// per client, so we'll have to wake up multiple threads for
	// each new game
	sem_t newGameSem1 = sem_make(0);
	sem_t newGameSem2 = sem_make(0);

	// we miiight need to count the threads that
	// aren't doing anything
	int availableThreads = 0;
	pthread_mutex_t availableThreadsMutex = pthread_mutex_make();

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
			.availableThreads = &availableThreads,
			.availableThreadsMutex = &availableThreadsMutex,
		};

		threadArgs[i] = args;
		pthread_create_(&threads[i], &threadAttr, &threadFunc, &threadArgs[i]);
	}

	// accept loop
	while (!sigint_received) {
		socklen_t clientAddrLen = sizeof(struct sockaddr_in);

		// start setting up new game
		Game* game = calloc(1, sizeof(Game));

		// accept 1st player
		game->player1Addr = malloc(clientAddrLen);
		game->player1Socket = ERR_NEG1_(
				accept(serverSocket, (struct sockaddr*) game->player1Addr,
					&clientAddrLen),
				EINTR);
		// if we got interrupted here we check if it's by SIGINT
		if (EINTR == errno) {
			FREE(game->player1Addr);
			FREE(game);

			continue;
		}
		printf_("Main thread accepting %s as Player 1\n",
				inet_ntoa(game->player1Addr->sin_addr));

		// accept 2nd player
		game->player2Addr = malloc(clientAddrLen);
		game->player2Socket
			= ERR_NEG1_(accept(serverSocket, (struct sockaddr*) game->player2Addr,
					&clientAddrLen),
				EINTR);
		if (EINTR == errno) {
			FREE(game->player1Addr);
			FREE(game->player2Addr);
			FREE(game);

			continue;
		}
		printf_("Main thread accepting %s as Player 2\n",
				inet_ntoa(game->player2Addr->sin_addr));

#if REJECT_EXCESS
		// reject these guys if we don't have enough threads
		pthread_mutex_lock_(&availableThreadsMutex);
		if (availableThreads < 2) {
			pthread_mutex_unlock_(&availableThreadsMutex);

			char msgBuf[MSG_LEN + 1] = {0}; // +1 extra byte for '\0'

			// reject Player 1
			printf_("Main thread rejecting Player 1: %s\n",
					inet_ntoa(game->player1Addr->sin_addr));
			snprintf_(msgBuf, MSG_LEN, "Rejected, not enough threads\n");
			send_(game->player1Socket, msgBuf, MSG_LEN, 0);
			close_(game->player1Socket);

			// reject Player 2
			printf_("Main thread rejecting Player 2: %s\n",
					inet_ntoa(game->player2Addr->sin_addr));
			snprintf_(msgBuf, MSG_LEN, "Rejected, not enough threads\n");
			send_(game->player2Socket, msgBuf, MSG_LEN, 0);
			close_(game->player2Socket);

			// cleanup
			FREE(game->player1Addr);
			FREE(game->player2Addr);
			FREE(game);

			continue;
		} else pthread_mutex_unlock_(&availableThreadsMutex);
#endif // REJECT_EXCESS

		// make new barrier for this game
		pthread_barrier_t gameBarrier = pthread_barrier_make(2);
		game->barrier = &gameBarrier;

		// setup game turns, if we need
#if ROTATE_TURNS
		game->turn = 1;
		pthread_mutex_t turnMutex = pthread_mutex_make();
		game->turnMutex = &turnMutex;
		pthread_cond_t turnCond = pthread_cond_make();
		game->turnCond = &turnCond;
#endif // ROTATE_TURNS

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

	printf_("\n"); // slightly tidier exit

	// free any remaining games in queue
	for (Game* game; myListLength(gameQueue);) {
		game = popFirstVal(gameQueue);
		FREE(game);
	}
	deleteMyList(gameQueue);

	FREE(threads);
	FREE(threadArgs);

	sem_destroy_(&newGameSem1);
	sem_destroy_(&newGameSem2);
	pthread_mutex_destroy_(&gameQueueMutex);

	// cleanup and exit
	close_(serverSocket);
	return EXIT_SUCCESS;
}
