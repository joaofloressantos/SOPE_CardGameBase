#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef struct {
	char name[200];
	char cardsInHand[27][4];
	char fifoPath[2048];
	int nr;
	int inGame;
} playerStats;

typedef struct {
	pthread_mutex_t joinGameLock;
	pthread_mutex_t waitForPlayersLock;
	pthread_mutex_t waitForDealCardsLock;
	pthread_mutex_t waitForTurnLock;
	pthread_cond_t playerWaitCond;
	pthread_cond_t playerWaitForDealCardsCond;
	pthread_cond_t playerWaitForTurnCond;
	char dealer[200];
	playerStats players[52];
	char cardsOnDeck[53][4];
	char cardsOnTable[53][4];
	char previousCardsOnTable[53][4];
	char tempPreviousCardsOnTable[53][4];
	int roundNumber;
	int nPlayers;
	int nCardsPlayed;
	int nPreviousCardsPlayed;
	int lastLoggedPlayer;
	int playerTurn;
	int cardsDealt;
	char logPath[PATH_MAX];
} sharedFields;

//Card array used when creating cards
char cards[53][4];
//Shared memory pointer
sharedFields* shm;
//Player number (3rd player to join is 2)
int playerNumber;
//Player name
char playerName[512];
//Individual player's fifo path
char fifoPath[2048];
//Player's hand card number
int playerHandSize;
//Shared memory file descriptor
int shmfd = 0;
//Table name holder used for fifo declaration
char tableName[128];
//Fifo file descriptor
int fifofd = 0;
//Time variables
time_t rawtime;
struct tm * timeinfo;
//strings para escrever para o ficheiro
char when[20];
char who[14];
char what[15];
char result[500];
//Log file
FILE* file;
int firstPrint = 1;

/* Mutex variables */
//join game mutex
pthread_mutex_t *mptr;
//wait for players mutex
pthread_mutex_t *wptr;
//wait for cards mutex
pthread_mutex_t *wCardsptr;
//wait for turn to play mutex
pthread_mutex_t *wTurnptr;
//wait for players conditional variable
pthread_cond_t *wCondPtr;
//wait for cards conditional variable
pthread_cond_t *wCardsCondPtr;
//wait for turn conditional variable
pthread_cond_t *wTurnCondPtr;
//join game mutex attribute
pthread_mutexattr_t mattr;
//wait for players attribute
pthread_mutexattr_t wattr;
//wait for cards attribute
pthread_mutexattr_t wCardsattr;
//wait for turn attribute
pthread_mutexattr_t wTurnattr;
//wait for players attribute
pthread_condattr_t wcond;
//wait for cards attribute
pthread_condattr_t wCardscond;
//wait for turn attribute
pthread_condattr_t wTurncond;

void orderCards() {
	int i, nc = 0, nh = 0, nd = 0, ns = 0;
	char clubs[14][4];
	char hearts[14][4];
	char diamonds[14][4];
	char spades[14][4];
	for (i = 0; i < playerHandSize; i++) 
	{
		switch (shm->players[playerNumber].cardsInHand[i][2]) 
		{
			case 'c':
			strcpy(clubs[nc], shm->players[playerNumber].cardsInHand[i]);
			nc++;
			break;
			case 'h':
			strcpy(hearts[nh], shm->players[playerNumber].cardsInHand[i]);
			nh++;
			break;
			case 'd':
			strcpy(diamonds[nd], shm->players[playerNumber].cardsInHand[i]);
			nd++;
			break;
			case 's':
			strcpy(spades[ns], shm->players[playerNumber].cardsInHand[i]);
			ns++;
			break;
		}
	}
	int temp = 0;
	for (i = 0; i < nc; i++, temp++) 
	{
		strcpy(shm->players[playerNumber].cardsInHand[temp], clubs[i]);
	}
	for (i = 0; i < nh; i++, temp++)
	{
		strcpy(shm->players[playerNumber].cardsInHand[temp], hearts[i]);
	}
	for (i = 0; i < nd; i++, temp++) {

		strcpy(shm->players[playerNumber].cardsInHand[temp], diamonds[i]);
	}
	for (i = 0; i < ns; i++, temp++) 
	{
		strcpy(shm->players[playerNumber].cardsInHand[temp], spades[i]);
	}
}

void writeToFile()
{

	time ( &rawtime );
	timeinfo = localtime ( &rawtime );

	sprintf(when, "%.4d-%.2d-%.2d %.2d:%.2d:%.2d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
		timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

	char fileOutput[200];

	if ((file = fopen(shm->logPath, "a+")) == NULL) 
	{
		perror("Erro ao criar o ficheiro");
	}

	sprintf(fileOutput, "%-20s| %-14s| %-15s| %s", when, who, what, result);
	fprintf(file, "%s", fileOutput);
	fclose(file);
}


/**
 * Shuffles the deck
 */
 void shuffleDeck() 
 {
 	char t[4];
 	int p1, p2;
 	unsigned int j;
 	for (j = 0; j < 52; j++) 
 	{
 		p1 = rand() % 52;
 		p2 = rand() % 52;
 		strcpy(t, cards[p1]);
 		strcpy(cards[p1], cards[p2]);
 		strcpy(cards[p2], t);
 	}
 }

/**
 * Creates ordered full deck
 */
 void createDeck() {
 	char values[][3] = { " A", " 2", " 3", " 4", " 5", " 6", " 7", " 8", " 9",
 	"10", " V", " Q", " K" };
 	char suits[][2] = { "c", "s", "h", "d" };
 	int i, j, cardNr;
 	for (i = 0; i < 4; i++) 
 	{
 		for (j = 0; j < 13; j++) 
 		{
 			cardNr = (i * 13) + j;
 			strcpy(cards[cardNr], values[j]);
 			strcat(cards[cardNr], suits[i]);
 		}
 	}
 	strcpy(cards[52], "\0");
 	shuffleDeck();
 	for (i = 0; i < 53; i++) 
 	{
 		strcpy(shm->cardsOnDeck[i], cards[i]);
 	}
 }

 void fillStruct(int nPlayers) 
 {
 	shm->lastLoggedPlayer = 0;
 	shm->roundNumber = 0;
 	shm->nPlayers = nPlayers;
 	shm->nCardsPlayed = 0;
 	shm->nPreviousCardsPlayed = 0;
 	shm->playerTurn = 0;
 	playerNumber = shm->lastLoggedPlayer;
 	shm->players[playerNumber].nr = playerNumber;
 	shm->players[playerNumber].inGame = 1;
 	playerHandSize = 0;
 	strcpy(shm->dealer, playerName);
 	strcpy(shm->players[shm->lastLoggedPlayer].name, playerName);
 	char fifoName[2048];
 	strcpy(fifoName, playerName);
 	strcpy(fifoPath, fifoName);
 	strcpy(shm->players[playerNumber].fifoPath, fifoPath);
 	fifofd = mkfifo(fifoName, S_IRUSR | S_IWUSR);
 	if ((fifofd = open(fifoPath, O_RDONLY | O_CREAT | O_NONBLOCK | O_TRUNC), 0666) == -1) 
 	{
 		perror("Erro ao abrir o fifo");
 	}
 }

 void createLogFile() 
 {
 	char cwd[1024];

 	if (getcwd(cwd, sizeof(cwd)) == NULL ) 
 	{
 		perror("Erro ao ir buscar o diretorio atual");
 	}

 	strcat(cwd, "/");
 	strcat(cwd, tableName);
 	strcat(cwd, ".log");

 	strcpy(shm->logPath, cwd);

 	strcpy(when, "when");
 	strcpy(who, "who");
 	strcpy(what, "what");
 	strcpy(result, "result\n");
 	writeToFile();
 }

/**
 * Creates and maps sets the size shared memory structure. Only creates if the player is a dealer.
 * Fills all necessary initial data and subsequent calls only add players.
 * Creates deck and stores it in the shared memory.
 * Creates log file.
 * @param shmSize    Shared memory size
 * @param nPlayers   Number of players
 */
 void createSharedMemory(int shmSize, int nPlayers) 
 {
	//Open memory
 	shmfd = shm_open(tableName, O_CREAT | O_RDWR | O_EXCL, 0666);

	//Check for error opening memory
 	if (shmfd < 0) 
 	{
 		if (errno == EEXIST) 
 		{
			//Opening without create
 			shmfd = shm_open(tableName, O_RDWR | O_EXCL, 0666);

			//Mapping memory
 			shm = (sharedFields*) mmap(NULL, shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);

			//Check for errors in memory mapping
 			if (shm == NULL ) 
 			{
 				perror("Erro a mapear memoria partilhada");
 				exit(-1);
 			}
 		} else 
 		{
 			perror("Erro a abrir memoria");
 			exit(-1);
 		}
 	} else 
 	{
		//Set memory size
 		if (ftruncate(shmfd, shmSize) < 0) 
 		{
 			perror("Erro a definir espaco de memoria");
 			exit(-1);
 		}

		//Mapping memory
 		shm = (sharedFields*) mmap(NULL, shmSize, PROT_READ | PROT_WRITE,
 			MAP_SHARED, shmfd, 0);

		//Check for errors in memory mapping
 		if (shm == NULL ) 
 		{
 			perror("Erro a mapear memoria partilhada");
 			exit(-1);
 		}

		//Fill struct with attributes
 		createLogFile(tableName);
 		fillStruct(nPlayers);
 	}
 }

 void initSyncedObjects(char* playerName) 
 {
	//Init mutexes
	//Join game mutex
 	if (strcmp(shm->dealer, playerName) == 0) 
 	{
 		pthread_mutexattr_init(&mattr);
 		pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
 		mptr = &(shm->joinGameLock);
 		if (pthread_mutex_init(mptr, &mattr) != 0) 
 		{
 			perror("Erro a inicializar mutex");
 			exit(-1);
 		}
		//Wait for all players mutex
 		pthread_mutexattr_init(&wattr);
 		pthread_mutexattr_setpshared(&wattr, PTHREAD_PROCESS_SHARED);
 		wptr = &(shm->waitForPlayersLock);
 		if (pthread_mutex_init(wptr, &wattr) != 0) 
 		{
 			perror("Erro a inicializar mutex de espera de jogadores");
 			exit(-1);
 		}
		//Wait for cards mutex
 		pthread_mutexattr_init(&wCardsattr);
 		pthread_mutexattr_setpshared(&wCardsattr, PTHREAD_PROCESS_SHARED);
 		wCardsptr = &(shm->waitForDealCardsLock);
 		if (pthread_mutex_init(wCardsptr, &wCardsattr) != 0) 
 		{
 			perror("Erro a inicializar mutex de espera da entrega das cartas");
 			exit(-1);
 		}
		//Wait for turn to play mutex
 		pthread_mutexattr_init(&wTurnattr);
 		pthread_mutexattr_setpshared(&wTurnattr, PTHREAD_PROCESS_SHARED);
 		wTurnptr = &(shm->waitForTurnLock);
 		if (pthread_mutex_init(wTurnptr, &wTurnattr) != 0) 
 		{
 			perror("Erro a inicializar mutex de espera da entrega das cartas");
 			exit(-1);
 		}
		//wait for players conditional variable
 		pthread_condattr_init(&wcond);
 		pthread_condattr_setpshared(&wcond, PTHREAD_PROCESS_SHARED);
 		wCondPtr = &(shm->playerWaitCond);
 		pthread_cond_init(&shm->playerWaitCond, &wcond);
		//wait for cards conditional variable
 		pthread_condattr_init(&wCardscond);
 		pthread_condattr_setpshared(&wCardscond, PTHREAD_PROCESS_SHARED);
 		wCardsCondPtr = &(shm->playerWaitForDealCardsCond);
 		pthread_cond_init(&shm->playerWaitForDealCardsCond, &wCardscond);
		//wait for turn to play conditional variable
 		pthread_condattr_init(&wTurncond);
 		pthread_condattr_setpshared(&wTurncond, PTHREAD_PROCESS_SHARED);
 		wTurnCondPtr = &(shm->playerWaitForTurnCond);
 		pthread_cond_init(&shm->playerWaitForTurnCond, &wTurncond);
 	}
 }

 void joinGame(char* playerName) 
 {
	//Locking shared memory
 	pthread_mutex_lock(&(shm->joinGameLock));

	//If it's not the dealer
 	if (strcmp(shm->dealer, playerName) != 0) 
 	{
 		shm->lastLoggedPlayer++;
 		strcpy(shm->players[shm->lastLoggedPlayer].name, playerName);
 		playerNumber = shm->lastLoggedPlayer;
 		shm->players[playerNumber].nr = playerNumber;
 		shm->players[playerNumber].inGame = 1;
 		char fifoName[2048];
 		strcpy(fifoName, playerName);
 		strcpy(fifoPath, fifoName);
 		strcpy(shm->players[playerNumber].fifoPath, fifoPath);
 		fifofd = mkfifo(fifoName, S_IRUSR | S_IWUSR);
 		if ((fifofd = open(fifoPath, O_RDONLY | O_CREAT | O_NONBLOCK | O_TRUNC), 0666) == -1) 
 		{
 			perror("Erro ao abrir o fifo");
 		}
 	}
	//Unlocking shared memory
 	pthread_cond_broadcast(&(shm->playerWaitCond));
 	pthread_mutex_unlock(&(shm->joinGameLock));
 }

 void waitForAllPlayers() 
 {
 	printf("Waiting for all players...\n");
 	pthread_mutex_lock(&(shm->waitForPlayersLock));
 	while (shm->lastLoggedPlayer != shm->nPlayers - 1) 
 	{
 		pthread_cond_wait(&(shm->playerWaitCond), &(shm->waitForPlayersLock));
 	}
 	pthread_mutex_unlock(&(shm->waitForPlayersLock));
 	if (playerNumber == 0) 
 	{
 		pthread_cond_destroy(&shm->playerWaitCond);
 	}
 }

 void showCardsInHand()
 {
 	int i;
 	char previousSuit;
 	printf("Cartas na mao:\n");
 	previousSuit = shm->players[playerNumber].cardsInHand[0][2];
 	printf("%s", shm->players[playerNumber].cardsInHand[0]);
 	for (i = 1; i < playerHandSize; i++) 
 	{
 		if(shm->players[playerNumber].cardsInHand[i][2] != previousSuit)
 		{
 			previousSuit = shm->players[playerNumber].cardsInHand[i][2];
 			printf(" / ");
 		}
 		else
 		{
 			printf(" - ");
 		}
 		printf("%s", shm->players[playerNumber].cardsInHand[i]);
 	}
 }

 void getPlayerHand(int number)
 {
 	int i;
 	char previousSuit;
 	char tempCard[4];
 	strcpy(tempCard, shm->players[number].cardsInHand[0]);

 	if (tempCard[2] == '\0') 
 	{
 		tempCard[0] = tempCard[1];
 		tempCard[1] = tempCard[2];
 		tempCard[2] = '\0';
 	}

 	sprintf(result, "%s", tempCard);

 	previousSuit = shm->players[number].cardsInHand[0][2];
 	for (i = 1; i < playerHandSize; i++) 
 	{
 		if(shm->players[number].cardsInHand[i][2] != previousSuit)
 		{
 			previousSuit = shm->players[number].cardsInHand[i][2];
 			strcat(result, "/");
 		}
 		else
 		{
 			strcat(result, "-");
 		}
 		strcpy(tempCard, shm->players[number].cardsInHand[i]);

 		if (tempCard[0] == ' ') 
 		{
 			tempCard[0] = tempCard[1];
 			tempCard[1] = tempCard[2];
 			tempCard[2] = '\0';
 		}
 		strcat(result, tempCard);
 	}
 	strcat(result, "\n");
 }


 void dealCards() 
 {
 	if (playerNumber == 0) 
 	{
 		int i = 0, j = 0;
 		for (j = 0; j < 52; j++) 
 		{
 			if ((fifofd = open(shm->players[i].fifoPath, O_WRONLY | O_APPEND), 0666) == -1) 
 			{
 				perror("Erro ao abrir o fifo");
 			}

 			if ((write(fifofd, shm->cardsOnDeck[j], 4)) == -1) 
 			{
 				perror("Erro ao escrever para o fifo");
 			}

 			if ((close(fifofd)) == -1) 
 			{
 				perror("Erro a fechar fifo");
 			}

 			i++;

 			if (i == shm->nPlayers) 
 			{
 				i = 0;
 			}
 		}
 		sprintf(who, "Dealer-%s", shm->players[0].name);
 		sprintf(what, "deal");
 		sprintf(result, "-\n");
 		writeToFile();

		//sinalizar os processos que ja podem sair do ciclo de espera e receber as cartas
 		shm->roundNumber++;//determinar o proximo jogador a jogar -> de forma random
		int nextPlayer = 0;
		nextPlayer = 1;//rand() % shm->nPlayers;
		shm->playerTurn = nextPlayer;
 		pthread_cond_broadcast(&(shm->playerWaitForDealCardsCond));	
 	}
 }

 void receiveCards() 
 {
 	char line[4];

	//esperar até todas as cartas serem dadas
 	if (shm->nPlayers != 0) 
 	{
 		pthread_mutex_lock(&(shm->waitForDealCardsLock));

 		while (shm->roundNumber == 0) 
 		{
 			pthread_cond_wait(&(shm->playerWaitForDealCardsCond), &(shm->waitForDealCardsLock));
 		}
 		pthread_mutex_unlock(&(shm->waitForDealCardsLock));
 		if (playerNumber == 0) 
 		{
 			pthread_cond_destroy(&shm->playerWaitForDealCardsCond);
 		}
 	}

 	if ((fifofd = open(fifoPath, O_RDONLY | O_NONBLOCK, 0666)) == -1) 
 	{
 		perror("Erro ao abrir o fifo");
 	}

 	int i = 0;
 	while ((read(fifofd, line, 4)) == 4) 
 	{
 		strcpy(shm->players[playerNumber].cardsInHand[i], line);
 		i++;
 		playerHandSize++;
 	}
 }


 void printReceivedCards()
 { 	
 	strcpy(what, "receive_cards");
 	strcpy(who, "");
 	sprintf(who, "Player%d-%s", shm->players[playerNumber].nr, shm->players[playerNumber].name);
 	getPlayerHand(playerNumber);
 	writeToFile();
 }

 void printHand()
 { 	
 	strcpy(what, "hand");
 	strcpy(who, "");
 	sprintf(who, "Player%d-%s", shm->players[playerNumber].nr, shm->players[playerNumber].name);
 	getPlayerHand(playerNumber);
 	writeToFile();
 }

 void printPlay(char* card)
 { 	
 	strcpy(what, "play");
 	strcpy(who, "");
 	sprintf(who, "Player%d-%s", shm->players[playerNumber].nr, shm->players[playerNumber].name);
 	strcpy(result, card);
 	strcat(result, "\n");
 	writeToFile();
 }

 void* playerTurn(void* arg)
 { 	int i = 0;
 	int op = 0;
 	char x;
 	int exists = 0;
 	char cardPlayed[4];
 	time_t start, stop;
 	time(&start);
 	do
 	{
 		do
 		{
 			system("clear");
 			printf("1 - Jogar uma carta\n");
 			printf("2 - Mostrar as cartas na mesa\n");
 			printf("3 - Mostrar as cartas na mao\n");
 			printf("4 - Mostrar as cartas jogadas na ronda anterior\n");
 			printf("Escreva o numero correspondente a opcao desejada: ");
 			scanf("%d", &op);

 			if (op <= 0 || op > 4)
 			{
 				system("clear");
 				showCardsInHand();
 				printf("Opcao invalida!\n");
 			}

 		}while(op < 1 || op > 4);
 		switch(op)
 		{
 			case 1:
 			do 
 			{
 				system("clear");
 				showCardsInHand();
 				printf("\nCarta a jogar: ");
 				scanf("%s", cardPlayed);

 				if (cardPlayed[2] == '\0') 
 				{
 					cardPlayed[3] = cardPlayed[2];
 					cardPlayed[2] = cardPlayed[1];
 					cardPlayed[1] = cardPlayed[0];
 					cardPlayed[0] = ' ';
 				}

				//Procura carta jogada na mão do jogador
 				for (i = 0; i < playerHandSize; i++) 
 				{
 					if (strcmp(cardPlayed, shm->players[playerNumber].cardsInHand[i]) == 0) 
 					{
 						if(firstPrint == 1)
 						{ 		
 							firstPrint = 0;
 							printReceivedCards();
 						}
 						int j;
 						//Quando encontra a carta elimina-a do array de cartas do jogador fazendo shift a esquerda
 						for (j = i; j < 53; j++) 
 						{
 							strcpy(shm->players[playerNumber].cardsInHand[j], shm->players[playerNumber].cardsInHand[j + 1]);
 							if (shm->players[playerNumber].cardsInHand[j][0] == '\0') 
 							{
 								break;
 							}
 						}
 						exists = 1;
 						playerHandSize--;
 						break;
 					}
 				}

 				if (exists == 0)
 				{
 					system("clear");
 					printf("Carta nao existente na mao!\n");
 				}
 			} while (exists == 0);

 			strcpy(shm->cardsOnTable[shm->nCardsPlayed], cardPlayed);
 			shm->nCardsPlayed++;

 			strcpy(shm->tempPreviousCardsOnTable[shm->nPreviousCardsPlayed], cardPlayed);
 			shm->nPreviousCardsPlayed++;

 			printPlay(cardPlayed);
 			printHand();

 			if (shm->nPreviousCardsPlayed == shm->nPlayers)
 			{
 				shm->nPreviousCardsPlayed = 0;
 				int i;
 				for (i = 0; i < shm->nPlayers; i++)
 				{
 					strcpy(shm->previousCardsOnTable[i], shm->tempPreviousCardsOnTable[i]); 
 				}
 			}
 			break;
 			case 2:
 			system("clear");
 			printf("\nCartas na mesa:\n");
 			for (i = 0; shm->cardsOnTable[i][0] != '\0'; i++) 
 			{
 				printf("%s - ", shm->cardsOnTable[i]);
 			}
 			printf("\nClique enter para continuar!\n");
 			read(STDIN_FILENO, &x, 1);
 			break;
 			case 3:
 			system("clear");
 			showCardsInHand();
 			printf("\nClique enter para continuar!\n");
 			read(STDIN_FILENO, &x, 1);
 			break;
 			case 4:
 			if (shm->roundNumber >= 2)
 			{	
 				system("clear");
 				printf("\nCartas da ronda anterior:\n");
 				for (i = 0; shm->previousCardsOnTable[i][0] != '\0'; i++) 
 				{
 					printf("%s - ", shm->previousCardsOnTable[i]);
 				}
 				printf("\nClique enter para continuar!\n");
 				read(STDIN_FILENO, &x, 1);
 			}
 			break;
 		}
 	}while(op != 1);
 	time(&stop);
 	int timeElapsed = difftime(stop,start);
 	system("clear");
 	printf("Jogada executada em %d segundos\n", timeElapsed);
 	return NULL;
 }
 
 void* playGame(void* arg) 
 {

 	while (playerHandSize > 0) 
 	{
 		printf("Waiting for turn to play...\n");
 		pthread_t pmtid;
 		pthread_mutex_lock(&(shm->waitForTurnLock));

 		while (shm->playerTurn != playerNumber) 
 		{	
 			pthread_cond_wait(&(shm->playerWaitForTurnCond), &(shm->waitForTurnLock));
 		}
 		pthread_create(&pmtid, NULL, playerTurn, NULL );

 		int i = 0;
 		int exists = 0;

		//Determinar qual e o proximo jogador a jogar -> o proximo jogador a jogar é o jogador a seguir ao atual que ainda tenha cartas na mao
 		for (i = shm->playerTurn; i < shm->nPlayers - 1; i++) 
 		{
 			if (shm->players[i + 1].inGame == 1) 
 			{
 				shm->playerTurn = shm->players[i + 1].nr;
 				exists = 1;
 				break;
 			}
 		}

 		i = 0;
		//se nenhum jogador para a frente do atual (com um id maior) esta em jogo ele verifica os que estao para tras (id menor), comecando do inicio
 		if (exists == 0) 
 		{
 			for (i = 0; i < shm->playerTurn; i++) 
 			{
 				if (shm->players[i].inGame == 1) 
 				{
 					shm->playerTurn = shm->players[i].nr;
 					exists = 1;
 					break;
 				}
 			}
 		}

 		pthread_join(pmtid, NULL);
 		pthread_detach(pmtid);
 		pthread_cond_broadcast(&(shm->playerWaitForTurnCond)); 		
 		if (shm->playerTurn == shm->nPlayers - 1 || exists == 0 || shm->playerTurn == 0)
 		{
 			shm->roundNumber++;
 		}
 		pthread_mutex_unlock(&(shm->waitForTurnLock));
 	}

 	shm->nPlayers = shm->nPlayers - 1;
 	shm->players[playerNumber].inGame = 0;

 	return NULL ;
 }

 void destroySharedMemory(sharedFields* shm, int shmSize) 
 {
 	if ((unlink(shm->players[playerNumber].fifoPath)) == -1) 
 	{
 		printf("%s\n", shm->players[playerNumber].fifoPath);
 		perror("Erro no unlink do FIFO");
 	}

	//A ORDEM INTERESSA

 	if (playerNumber == 0) 
 	{
 		if (munmap(shm, shmSize) < 0) 
 		{
 			perror("Erro no munmap");
 			exit(-1);
 		}

 		if 
 			(shm_unlink(tableName) < 0) 
 		{
 			perror("Erro no shm_unlink");
 			exit(-1);
 		}
 	}
 }

 int main(int argc, char *argv[]) 
 {
 	srand(time(NULL ));
 	strcpy(playerName, argv[1]);
 	strcat(tableName, argv[2]);
 	createSharedMemory(sizeof(sharedFields), atoi(argv[3]));

 	if (shm == NULL ) 
 	{
 		perror("Erro a criar memoria partilhada");
 		exit(-1);
 	}

 	initSyncedObjects(argv[1]);
 	joinGame(argv[1]);
 	waitForAllPlayers();
 	createDeck();
 	dealCards();
 	receiveCards();
 	orderCards();
	//Main game thread id
 	pthread_t mtid;
 	pthread_create(&mtid, NULL, playGame, NULL );
 	pthread_join(mtid, NULL );
 	destroySharedMemory(shm, sizeof(sharedFields));
 	exit(0);
 }