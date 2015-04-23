#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip.h> //ip hdeader library (must come before ip_icmp.h)
#include <netinet/ip_icmp.h> //icmp header
#include <arpa/inet.h> //internet address library
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fstream>
#include <iostream>
#include <string>
#include <map>
#include <list>
#include <time.h>
#include <pthread.h>

#include <openssl/sha.h> 

#include "bt_lib.h"
#include "bt_setup.h"
#include "bt_parser.h"
	
using namespace std;

bt_info_t bt_info;
ofstream myLog;
map<string, bencodeData> torrentPrintData;
char * threadSeederId;
int  verboseSeeder;

/**
 * getInfoHash(char * infoHash) -> void
 *
 * Constructs the info part of torrent file from bt_info structure and computes hash for the info.
 **/

void getInfoHash(char * infoHash) {
  int infoMsgLength = FIXED_STR_LENGTH + strlen(bt_info.name) + strlen(bt_info.piece_hashes) + sizeof(bt_info.length) + sizeof(bt_info.piece_hashes);
  char infoMsg[infoMsgLength];

  sprintf(infoMsg, "name:%s,length:%d,piece length%d,pieces:%s", bt_info.name, bt_info.length, bt_info.piece_length, bt_info.piece_hashes);
  SHA1((unsigned char *) infoMsg, strlen(infoMsg), (unsigned char *) infoHash);
}

/**
 * printLog(ofstream logFile, char * msg) -> void
 *
 * Prints data to the log file
 *
 **/

void printLog(ofstream& logFile, char * msg) {
  //Using the code to print time from website - http://www.cplusplus.com/reference/ctime/strftime/
  time_t rawtime;
  struct tm * timeinfo;
  char timeStamp[80];

  time ( &rawtime );
  timeinfo = localtime ( &rawtime );
  strftime (timeStamp,80,"[ %D  %T] : ",timeinfo);
  logFile << timeStamp << msg << endl;
}

/**
 * initiateLeecherCleanUp() -> void
 *
 * Removes the downloaded data in case there is a connection problem with seeder during data transfer
 *
 **/

void initiateLeecherCleanUp(char *saveFolder) {
  char tempFile[1024];

  for (int i = 1; i <= bt_info.num_pieces; i++) {
    sprintf(tempFile, "./%s/%d%s", saveFolder, i, bt_info.name);
	remove(tempFile);
  }
  printLog(myLog, (char *) "LEECHER - Removed all the temporary files");
}

/**
 * startLeecher(bt_args_t * bt_args1, int sockfd) -> void
 *
 * starts the leecher process and initiates connection with the seeder
 *
 **/

void startLeecher(bt_args_t * bt_args1, int sockfd){  
  int readSize, randomPiece, pieceIndex, reqLength, offset;
  char leecher_hsMsg[HAND_SHAKE_MSG_LENGTH] = "", recvData[MAX_BUFF_SIZE];
  char requestMsg[MAX_BUFF_SIZE], seederID2[21], tempFile[FILE_NAME_MAX], piece_sha1[21], original_sha1[21], logMsg[200];
  char bt = 19;
  char * leecher_infoHash, * seed_infoHash;
  char * seederID, * leecherSendData, * leecherRecvData;
  char * seederBitField, * leecherBitField;
  bool seederStatus = true;

  //Connect to the seeder
  if(connect(sockfd, (struct sockaddr *) &bt_args1->peers[0]->sockaddr, sizeof(struct sockaddr_in) )< 0 ){
    fprintf(stderr,"Connect Error");
    printLog(myLog, (char *) "LEECHER - ERROR Connecting to Seeder");
    exit(1);
  }
  if(bt_args1->verbose)
    cout << "Connected to Seeder..." << endl;
  printLog(myLog, (char *) "LEECHER - Connected to the seeder");

  //Stores the value of leecher (self) peer id in a char array
  strcpy(seederID2, bt_args1->peers[0]->id);
  seederID2[20] = '\0';

  //Computes the hash of torrent file's info value
  leecher_infoHash = (char *)malloc(21);
  getInfoHash(leecher_infoHash);
  leecher_infoHash[20] = '\0';

  //Construct the handshake message
  sprintf(leecher_hsMsg, "%cBitTorrent protocol00000000%s%s", bt, leecher_infoHash,seederID2);

  //Write the handshake message to the socket
   if(bt_args1->verbose)
    cout << "Initiating Handshake with Seeder..." << endl;
  write(sockfd,leecher_hsMsg,strlen(leecher_hsMsg));
  printLog(myLog, (char *) "LEECHER - HandShake initiated with the seeder");

  //Read the acknowledgement handshake message from the seeder
  readSize = read(sockfd, recvData, 1024);
  recvData[readSize] = '\0';
  printLog(myLog, (char *) "LEECHER - HandShake Acknowledgement received from seeder");

  //Extracts the peer id of the seeder
  seederID = (char *)malloc(21);
  strncpy(seederID, recvData + 48, 20);
  seederID[20] = '\0';

  //Extracts the torrent file's info hash of the seeder
  seed_infoHash = (char *)malloc(21);
  strncpy(seed_infoHash, recvData + 28, 20);
  seed_infoHash[20] = '\0';

  //Validate peer id of seeder and info hash from the seeder
  if ((strcmp(seederID, seederID2) == 0) && (strcmp(leecher_infoHash, seed_infoHash) == 0)){
	cout << "Handshake complete..." << endl;
	fflush(stdout);
    printLog(myLog, (char *) "LEECHER - HandShake with the seeder complete");

	//Get data from socket that it is unchoked
	recvData[0] = '\0';
	readSize = read(sockfd, recvData, 1024);
	recvData[readSize] = '\0';

	if ((recvData[0] == '1') && bt_args1->verbose)
      cout << "Peer Set as UNCHOKED" << endl;
    printLog(myLog, (char *) "LEECHER - Peer set as UNCHOKED by the seeder");

	//Set the leecher bit field to all 0's
	leecherBitField = (char *)malloc(bt_info.num_pieces + 1);
	fill(leecherBitField, leecherBitField + bt_info.num_pieces, '0');
	leecherBitField[bt_info.num_pieces] = '\0';

	//Get bit field data from the seeder
	recvData[0] = '\0';
	readSize = read(sockfd, recvData, 1024);
	recvData[readSize] = '\0';
	seederBitField = (char *)malloc(bt_info.num_pieces + 1);
	if (recvData[0] == '5')
      getDataFromSockMsg(recvData, seederBitField, strlen(recvData));
	seederBitField[bt_info.num_pieces] = '\0';
    printLog(myLog, (char *) "LEECHER - BitField message of seeder received");

    //Send Interested Piece
	usleep(50000);
	leecherSendData = (char *)malloc(1040);
	sprintf(leecherSendData,"%d+1+",BT_INTERSTED);
	leecherSendData[4] = '\0';
	write(sockfd, leecherSendData, 5);
    printLog(myLog, (char *) "LEECHER - Interested message sent to seeder");

	srand(time(NULL));
	leecherRecvData = (char *)malloc(1040);

	//Create the save directory
	mkdir(bt_args1->save_file, 0777);

	//Request for a random piece from the seeder -- Loop until the file has all the pieces
	if(bt_args1->verbose)
      cout << "Requesting the data from Seeder..." << endl;
    printLog(myLog, (char *) "LEECHER - Requesting data from seeder");

	for(int z = 0; z < bt_info.num_pieces; z++ ){
	  while(1) {
       randomPiece = rand() % bt_info.num_pieces;
	   if(seederBitField[randomPiece] == '1' && leecherBitField[randomPiece] == '0')
	     break;
	  }

	  printProgress(bt_info.name, leecherBitField);
	  if(bt_args1->verbose)
        cout<< "Requesting Data for Piece: " << randomPiece + 1 << endl;
	  sprintf(logMsg, "LEECHER - Requesting data for piece: %d", randomPiece + 1);
	  logMsg[getDigits(randomPiece + 1) + 37] = '\0';
	  printLog(myLog, logMsg);

	  offset = 0;
	  //Create and open a temp file to store the piece data
	  sprintf(tempFile, "./%s/%d%s", bt_args1->save_file, randomPiece + 1, bt_info.name);
	  tempFile[strlen(bt_args1->save_file) + strlen(bt_info.name) + getDigits(randomPiece) + 3] = '\0';
	  ofstream pieceFile ( tempFile, ios::out | ios::binary);

	  //Request for a piece block from a piece
	  while(offset != bt_info.piece_length) {

	    //Construct the request message - index_Of_Piece | offset_Of_Piece_Block | length_Of_Data_To_Be_Transferred
	    pieceIndex = randomPiece * bt_info.piece_length ;
	    reqLength = getDigits(pieceIndex) + getDigits(offset) + 4 + 2 + 1;
	    sprintf(requestMsg, "%d|%d|%d", pieceIndex, offset, 1024);
	    requestMsg[reqLength - 1] ='\0';

	    //Construct the socket message - BT_REQUEST + (Length_Of_Request_Message+1) + Request_Message
	    sprintf(leecherSendData,"%d+%d+%s",BT_REQUEST,(int)strlen(requestMsg),requestMsg);
	    reqLength += 3 + getDigits(strlen(requestMsg));
	    leecherSendData[reqLength] = '\0';

		//Write the piece block request message to the socket
		usleep(50000);
	    write(sockfd, leecherSendData, strlen(leecherSendData));

		//Check if the file length is less than the length of requested data
		reqLength = 1024;
		if (pieceIndex + offset + reqLength > bt_info.length)
		  reqLength = bt_info.length - (pieceIndex + offset);

		/*//Read the piece message from the seeder
		pieceLength = 3 + getDigits(reqLength);
		readSize = read(sockfd, tempBuff, pieceLength);*/

		//Read the file data from seeder and write it to a temporary file
		usleep(60000);
		readSize = read(sockfd, leecherRecvData, reqLength);

		//Exit program if the read size is zero
		if ( readSize <= 0 )
		  seederStatus = false;

		//Write the data read fro, socket to temporary file
		pieceFile.write(leecherRecvData, reqLength);

		//Exit if the data of the next block requested is more than the length of the file
		if ( reqLength != 1024) {
		 seederStatus = true;
		 break;
		}

		//Increment the offset to request for next piece block
		offset += 1024;
		
		if(!seederStatus) {
		  //Remove all the temporary files
		  initiateLeecherCleanUp(bt_args1->save_file);
		  cout << "Seeder not responding, deleting all the temporary data" << endl;
		  printLog(myLog, (char *) "LEECHER - Seeder NOT RESPONDING, Deleting the temporary data");
		  break;
		}
	  }

	  if (seederStatus) {
	    //Close the temp file
	    pieceFile.close();

	    //Update the leecher bitfield
	    leecherBitField[randomPiece] = '1';

		// #LOGS
	    if(bt_args1->verbose)
          cout << "Received data for the piece " << randomPiece + 1 << endl;
	    sprintf(logMsg, "LEECHER - Received data for the piece: %d", randomPiece + 1);
	    logMsg[getDigits(randomPiece + 1) + 39] = '\0';
	    printLog(myLog, logMsg);
	  }
	  else
	    break;
	}

	if (seederStatus) {
	  printProgress(bt_info.name, leecherBitField);

	  //Send cancel message to the seeder
	  if(bt_args1->verbose)
        cout << "Data transfer complete... sending cancel request" << endl;
      printLog(myLog, (char *) "LEECHER - Data transfer complete, sending cancel request");
	  sprintf(leecherSendData,"%d+1+",BT_CANCEL);
	  leecherSendData[4] = '\0';
	  usleep(50000);
	  write(sockfd, leecherSendData, 5);

	  //Open the file to be written in Write mode
	  sprintf(tempFile, "./%s/%s", bt_args1->save_file, bt_info.name);
	  tempFile[strlen(bt_args1->save_file) + strlen(bt_info.name) + 3] = '\0';
	  ofstream mainFile ( tempFile, ios::out | ios::binary);

	  if(bt_args1->verbose)
          cout << "Writing the downloaded data to a file..." << endl;
      printLog(myLog, (char *) "LEECHER - Writing downloaded data to the file");

	  //Process the temporary files in an order and write it to original file
	  for(int x = 1; x <= bt_info.num_pieces; x++) {
	    sprintf(tempFile, "./%s/%d%s", bt_args1->save_file, x, bt_info.name);
	    tempFile[strlen(bt_args1->save_file) + strlen(bt_info.name) + getDigits(x) + 3] = '\0';

	    //Open the piece file in read mode
	    ifstream pieceFile (tempFile, ios::in | ios::binary);

	    //Get file size
	    pieceFile.seekg (0, pieceFile.end);
        readSize = pieceFile.tellg();
        pieceFile.seekg (0, pieceFile.beg);

	    //Read data from the file
        char * pieceData = new char [readSize + 1];
        pieceFile.read (pieceData,readSize);
        pieceData[readSize] = '\0';

	    //Compare hash to check if the piece data is valid
	    SHA1((unsigned char *) pieceData, readSize, (unsigned char *) piece_sha1);
	    piece_sha1[20] = '\0';

	    //Check for null values in sha1 of the piece and replace with _
	    for (int k = 0; k < 20; k++) {
          if( piece_sha1[k] == '\0' && k < 20 )
	        piece_sha1[k] = '_';
        }

	    //Get the actual SHA1 of the piece from the torrent file
	    strncpy(original_sha1, bt_info.piece_hashes + (x - 1) * 20 , 20);
	    original_sha1[20] = '\0';

	    //Write the data to the file
	    if (strcmp(piece_sha1, original_sha1) == 0)
  	      mainFile.write(pieceData,readSize);

	    //Close the piece file
	    pieceFile.close();

	    //Remove the piece file
	    remove(tempFile);

        //Free the allocated memory	 
	    free(pieceData);

		/*if(!seederStatus) {
		  cout << "SHA1 Digest not matching for piece " << x << endl;
		  printLog(myLog, (char *) "LEECHER - SHA1 digest of downloaded file not matching with original SHA1...Starting clean up");
		  initiateLeecherCleanUp(bt_args1->save_file);
		  cout << "Download FAILED... Try Downloading again" << endl;
		  break;
		}*/
	  }

	  //Close the file
	  mainFile.close();

	  //Free the allocated memory
	  free(leecherRecvData);
	  free(leecherSendData);
	  free(seederBitField);
	  free(leecherBitField);

	  if(seederStatus) {
	    cout << "File downloaded successfully... Exiting the program " << endl;
        printLog(myLog, (char*)"LEECHER - File download success!!!");
	  }
	}
  }
  else {
    fprintf(stderr,"Peer ID/Hash mismatch Error...Closing leecher connection\n");
    printLog(myLog, (char*)"LEECHER - PeerID/HASH MISMATCH ERROR, Closing leecher connection");
  }

  //Close socket connection
  close(sockfd);

  //Free the allocated memory
  free(seederID);
  free(leecher_infoHash);
  free(seed_infoHash);
}

/**
 * handlePeerConnection( void *threadArg) -> void*
 *
 * Handles the handshake and sending of file to leecher.
 **/

void  *handlePeerConnection( void *threadArg) {
  int readSize, msgLength, blockOffset, pieceIndex, length = 1024, sockLen;
  char bt = 19;
  char seeder_hsMsg[HAND_SHAKE_MSG_LENGTH] = "";
  char recvData[MAX_BUFF_SIZE], tempBuff[FILE_NAME_MAX + 1], logMsg[200];
  char * seeder_infoHash, * readHash, * sendData, * seedBitField;
  bool leecherStatus = true;
  int  * peerSock;

  peerSock = ( int *)threadArg;
  sockLen = getDigits(*peerSock);

  //Read the handshake message from the socket
  if(verboseSeeder)
      cout << "Handshake request received from leecher " << *peerSock << endl;
  sprintf(logMsg, "SEEDER - HandShake request received for Leecher %d", *peerSock);
  logMsg[sockLen + 48] = '\0';
  printLog(myLog, logMsg);
  readSize = read( * peerSock, recvData, 1024);
  recvData[readSize] = '\0';

  //Extract the torrent file's info hash value from the handshake message
  readHash = (char *)malloc(21);
  strncpy(readHash, recvData + 28, 20);
  readHash[20] = '\0';

  //Compute the info hash value for the seeder's torrent file
  seeder_infoHash = (char *)malloc(21);
  getInfoHash(seeder_infoHash);
  seeder_infoHash[20] = '\0';

  //validate the hash value of info from torrent file and leecher
  if (strcmp(readHash, seeder_infoHash) == 0){
    if(verboseSeeder)
      cout << "Sending hand shake acknowledgement to leecher " << *peerSock << endl;

	//Construct the acknowledgement handshake message to be sent to leecher
	sprintf(seeder_hsMsg, "%cBitTorrent protocol00000000%s%s", bt, seeder_infoHash,threadSeederId);
	seeder_hsMsg[68] ='\0';

	//Write the acknowledgement handshake message to the socket
	write(*peerSock,seeder_hsMsg,68);
	sprintf(logMsg, "SEEDER - HandShake Acknowledgement sent to Leecher %d", *peerSock);
    logMsg[ sockLen + 51] = '\0';
    printLog(myLog, logMsg);

	//Set the peer(leecher) as unchoked and construct the unchoked message
	usleep(50000);
	sendData = (char *)malloc(1040);
	sprintf(sendData,"%d+1+",BT_UNCHOKE);
	sendData[4] = '\0';

	//Write the unchoked message to the peer socket
    write(*peerSock, sendData, 5);
	sprintf(logMsg, "SEEDER - Leecher %d notified that it is set UNCHOKED", *peerSock);
    logMsg[ sockLen + 50] = '\0';
    printLog(myLog, logMsg);

	//Set the bitfield for all the pieces as '1'
	seedBitField = (char *)malloc(bt_info.num_pieces + 1);
	fill(seedBitField, seedBitField + bt_info.num_pieces, '1');
	seedBitField[bt_info.num_pieces] = '\0';

	//Construct the bit field message
	msgLength = 3 + getDigits(1 + bt_info.num_pieces) + strlen(seedBitField);
	sprintf(sendData, "%d+%d+%s", BT_BITFILED, ( 1 + bt_info.num_pieces), seedBitField);
	sendData[msgLength] = '\0';

	//Write the bitfield message to the socket
	usleep(50000);
	write(*peerSock, sendData, msgLength);
	sprintf(logMsg, "SEEDER - BitField message sent to leecher %d", *peerSock);
    logMsg[ sockLen + 42] = '\0';
    printLog(myLog, logMsg);

	//Check for interested piece
	recvData[0] = '\0';
	readSize = read(*peerSock, recvData, 1024);
	recvData[readSize] = '\0';
	if ((recvData[0] == '2') && verboseSeeder)
        cout << "Leecher " << *peerSock << " is interested in downloading file " << endl;
	sprintf(logMsg, "SEEDER - Reading interested message from the leecher %d", *peerSock);
    logMsg[ sockLen + 53] = '\0';
    printLog(myLog, logMsg);

	//Open the file in binary mode
	ifstream torrentFile ( bt_info.name, ifstream::binary);

	// #LOGS
    cout << "Initiating data transfer to leecher " << *peerSock << endl;
	sprintf(logMsg, "SEEDER - Initiating data transfer to leecher %d", *peerSock);
    logMsg[ sockLen + 45] = '\0';
    printLog(myLog, logMsg);

	while(1) {
	  //Read the data from the socket
	  readSize = read(*peerSock, recvData, 1024);
	  recvData[readSize] = '\0';

	  //Break the loop if leecher issues a cancel request
	  if(recvData[0] == '8') {
	    //Clear the buffer before exiting the loop
	    recvData[0] = '\0';
	    cout << "Leecher " << *peerSock << " issued a cancel request....data sent successfully" << endl;
		sprintf(logMsg, "SEEDER - Cancel request issued by Leecher %d", *peerSock);
        logMsg[ sockLen + 42] = '\0';
        printLog(myLog, logMsg);
		break;
	  }
	  else if ( recvData[0] == '6') {
	    //Re-initialise all values
		length = 1024;
		msgLength = 0;

	    //Get the requested message
	    getDataFromSockMsg(recvData, tempBuff, strlen(recvData));
	    tempBuff[getLengthFromSockMsg(recvData)] = '\0';

	    //Get the piece index and offset from the request message
	    pieceIndex = atoi(strtok(tempBuff, "|"));
	    blockOffset = atoi(strtok(strstr(tempBuff, "|") , "|"));

		if ( blockOffset == 0) {
		  if (verboseSeeder)
            cout << "Sending data to leecher "<< *peerSock << " for piece: " << (pieceIndex/bt_info.piece_length) + 1 << endl << endl;
		  // #LOGS
		  sprintf(logMsg, "SEEDER - Received request message, Transferring data to leecher %d for piece: %d", *peerSock, (pieceIndex/bt_info.piece_length) + 1);
		  logMsg[getDigits((pieceIndex/bt_info.piece_length) + 1) + sockLen + 76] = '\0';
		  printLog(myLog, logMsg);
		}

		//Check if the file length is less than the length of requested data
		if (pieceIndex + blockOffset + length > bt_info.length)
		  length = bt_info.length - (pieceIndex + blockOffset);

	    //Read the requested data from the file and store it in a temporary buffer
	    torrentFile.seekg(pieceIndex + blockOffset, torrentFile.beg);
	    tempBuff[0] = '\0';
		torrentFile.read(tempBuff, length);
		tempBuff[length] = '\0';

		//Construct the piece message
	    msgLength = 3 + getDigits(length);
	    sprintf(recvData, "%d+%d", BT_PIECE, length);
	    recvData[msgLength] = '\0';

		/*//Write the piece message to the socket
		usleep(10000);
		write(*peerSock, recvData, length);*/

        //Write the file data to the socket
		usleep(50000);
		write(*peerSock, tempBuff, length);

		//Clear the buffer after the data is sent
		recvData[0] = '\0';

		//Clear all the flags in file
		torrentFile.clear();
	  }
	  else {
	    leecherStatus = false;
	    cout << "Leecher " << *peerSock << " Not Responding - Terminating data transfer" << endl;
		sprintf(logMsg, "SEEDER - Leecher %d NOT RESPONDING, Terminating data transfer", *peerSock);
        logMsg[ sockLen + 59] = '\0';
        printLog(myLog, logMsg);
		break;
	  }
	}

	if (leecherStatus) {
	  cout << "File successfully sent to leecher " << *peerSock << "...closing the connection " << endl;
	  sprintf(logMsg, "SEEDER - File sent successfully to leecher %d, closing the connection", *peerSock);
      logMsg[ sockLen + 67] = '\0';
      printLog(myLog, logMsg);
	}

	//Close the file
	torrentFile.close();

	//Free the allocated memory
	free(seedBitField);
    free(sendData);
  }
  else{
    //Close the socket if there is a mismatch in hash of the info value
    fprintf(stderr,"Hash mismatch Error for leecher %d...Closing connection\n", *peerSock);
	sprintf(logMsg, "SEEDER - HASH MISMATCH ERROR for Leecher %d, closing the connection", *peerSock);
    logMsg[ sockLen + 65] = '\0';
    printLog(myLog, logMsg);
  }

  

  //Close socket connection
  close(*peerSock);
  
  //Free all the allocated memory
  free(readHash);
  free(seeder_infoHash);
  free(peerSock);
  pthread_exit(NULL);
}

/**
 * startSeeder(bt_args_t * bt_args, int sockfd) -> void
 *
 * starts the seeder process and sends the file requested by leecher
 **/

void startSeeder(bt_args_t * bt_args, int sockfd){
  int peerSock, peerAddrLen, rc, peerCount = -1;
  int *peerSockDesc[MAX_CONNECTIONS];
  struct sockaddr_in peerAddr;
  pthread_t threads[MAX_CONNECTIONS];
  char logMsg[200];

  if(bt_args->verbose)
    cout << "Waiting for connections..." << endl << endl;
  printLog(myLog, (char *) "SEEDER - Waiting to connect to a leecher");

  //Listen for connections
  listen(sockfd, 5);
  peerAddrLen = sizeof(peerAddr);

  //Multi-threading
  while( peerCount < MAX_CONNECTIONS - 1){

    //Accept a connection from the leecher
    peerSock = accept(sockfd, (struct sockaddr *) &peerAddr, (socklen_t*)&peerAddrLen);
    if( peerSock < 0){
	  fprintf(stderr,"Accept Error");
	  printLog(myLog, (char *) "SEEDER - ERROR accepting connection");
	  exit(1);
    }

	peerCount++;

	peerSockDesc[peerCount] = (int *)malloc(sizeof(int));
	* peerSockDesc[peerCount] = peerSock;
    threadSeederId = (char *) malloc( strlen(bt_args->bindPeer->id) + 1);
    threadSeederId = bt_args->bindPeer->id ;
    threadSeederId[strlen(bt_args->bindPeer->id)] ='\0';
	verboseSeeder = bt_args -> verbose ;

	// #LOGS
    if(bt_args->verbose)
      cout << "Leecher " << * peerSockDesc[peerCount] << " Connected - Initiating Data Transfer" << endl;
	sprintf(logMsg, "SEEDER - Connected to a Leecher %d, Initiating data transfer", * peerSockDesc[peerCount]);
	logMsg[ getDigits(* peerSockDesc[peerCount]) + 58] = '\0';
    printLog(myLog, logMsg);

	//Creating a thread for incoming request
    rc = pthread_create(&threads[peerCount], NULL, handlePeerConnection, (void *) peerSockDesc[peerCount]);
	if (rc){
      cout << "Error:unable to create thread," << rc << endl;
	  sprintf(logMsg, "SEEDER - Unable to create thread for leecher %d", * peerSockDesc[peerCount]);
	  logMsg[ getDigits(* peerSockDesc[peerCount]) + 45] = '\0';
      printLog(myLog, logMsg);
      exit(-1);
	}
  }

  //Joining threads
  for( int j=0; j < peerCount; j++){
    pthread_join(threads[j], NULL);
  }
}

int main (int argc, char * argv[]){
  bt_args_t bt_args;
  int sockfd;
  char logMsg[100];

  //Parse the input arguments from the command line
  parse_args(&bt_args, argc, argv);

  //Open the log file
  myLog.open(bt_args.log_file, std::ifstream::app);  

  if(bt_args.verbose){
    printf("Torrent File Args:\n");
	printf("Verbose: %d\n",bt_args.verbose);
    printf("Save File: %s\n",bt_args.save_file);
    printf("Log File: %s\n",bt_args.log_file);
    printf("Torrent File: %s\n\n", bt_args.torrent_file);

    /*for(i=0;i<MAX_CONNECTIONS;i++){
      if(bt_args.peers[i] != NULL)
        print_peer(bt_args.peers[i]);
    }*/
  }

  if(bt_args.verbose)
    cout << "Parsing the torrent file" << endl;
  printLog(myLog, (char*) "Parsing the torrent file.");

  //Parsing the torrent file
  torrentPrintData  = parseTorrentFile(bt_args.torrent_file);

  if(bt_args.verbose){
	cout << endl;
    cout << "*** Torrent File ***" << endl;
	cout << "Name: " << bt_info.name << endl;
	cout << "Piece Length: " << bt_info.piece_length << endl;
	cout << "Length: " << bt_info.length << endl;
	cout << "No of Pieces: " << bt_info.num_pieces << endl;
	cout << "Piece Hash: " << bt_info.piece_hashes << endl;
	cout << endl;

	fflush(stdout);
  }

  sprintf(logMsg, "Name of Torrent File: %s", bt_info.name );
  logMsg[strlen(logMsg)] = '\0';
  printLog(myLog, logMsg);

  sprintf(logMsg, "Number of Torrent File pieces: %d", bt_info.num_pieces );
  logMsg[getDigits(bt_info.num_pieces) + 31] = '\0';
  printLog(myLog, logMsg);

  //Opening a socket
  if(bt_args.verbose)
    cout << "Opening a socket connection..." << endl;
  printLog(myLog, (char *) "Opening a socket connection");
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  fflush(stdout);
  if(sockfd == -1 ){
    fprintf(stderr,"Error opening socket");
	printLog(myLog, (char *) "ERROR opening socket");
	exit(1);
  }

  //Bind socket to a port
  if (bind(sockfd, (struct sockaddr *) &bt_args.bindPeer->sockaddr , sizeof(struct sockaddr_in)) < 0 ){
    fprintf(stderr,"Bind Error");
	printLog(myLog, (char *) "ERROR Binding socket to an address");
	exit(1);
  }
  printLog(myLog, (char *) "Binding the socket to ip address and port number");

  //Check if the instance of program running is a leecher/seeder
  if(bt_args.leecher == 0){
    cout<<"In SEEDER..."<< endl;
    fflush(stdout);
    startSeeder(&bt_args, sockfd);
  }
  else{
    cout<<"In LEECHER..."<< endl;
    fflush(stdout);
    startLeecher(&bt_args, sockfd);
  }

  //Closing the socket connection
  printLog(myLog, (char *) "Closing the socket connection");
  close(sockfd);

  myLog.close();
  return 0;
}