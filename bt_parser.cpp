#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <iostream>
#include <fstream>
#include <map>
#include <list>
#include <stack>

#include "bt_setup.h"
#include "bt_lib.h"
#include "bt_parser.h"


extern bt_info_t bt_info;

map<string, bencodeData> torrentData;
stack<char> stak; //Keeps track of dictionaires, lists and integers

/**
 * parseTorrentFile(char fileName[]) -> map
 *
 * Parses the torrent file and stores data in the bencodeData structure.
 **/

map<string, bencodeData> parseTorrentFile(char fileName[]){
  streampos begin, end;
  pair<string, bencodeData> tempPair;
  int size, i;

  //Open the torrent file
  ifstream torrentFile (fileName, std::ifstream::in);
  if(!torrentFile) {
    perror("Error opening the torrent file");
	exit(1);
  }

  //Get the size of the data to be read from the file
  torrentFile.seekg (0, torrentFile.end);
  size = torrentFile.tellg();
  torrentFile.seekg (0, torrentFile.beg);
  
  //Read data from the file
  char * fileData = new char [size + 1];
  torrentFile.read (fileData,size);
  fileData[size] = '\0';

  //Close the file
  torrentFile.close();

  //Check for null values and replace it with _
  for (int k = 0; k < size; k++) {
    if( fileData[k] == '\0' && k <size )
	  fileData[k] = '_';
  }
  
  //Start Parsing the torrent file
  i = 0;
  if(fileData[i] == 'd' ){
    stak.push('d');
  }
  else {
    perror("Incorrect torrent file " );
	exit(1);
  }

  i += 1;

  while( !stak.empty()){
    if( i + 1 != size)
    {
      tempPair = readKeyValuePair(fileData, i);
	  torrentData[tempPair.first] = tempPair.second;
    }
    else {
      stak.pop();
    }
  }

  free(fileData);

  return torrentData;
}

/**
 * readKeyValuePair(char * data, int& offset) -> map
 *
 * Reads the key value pairs from the torrent file
 **/

pair<string, bencodeData> readKeyValuePair(char * data, int& offset) {
  string key;
  bencodeData_t bcData;
  static map<string, bencodeData> tempMap;
  static pair<string, bencodeData> dataPair;
  static string strVal;
  list<string> listVal;
  
  //Read key from the torrent file
  key = readString(data, offset);

  //Read values - i, s, l, d and call corresponding reading functions
  switch(data[offset]) {
	case 'd':
	  bcData.dataType ='d';
	  //Read the dictionary value
	  tempMap = readDictionary(data, offset);

	  if (key == "info")
	    extractUsefulInfo( tempMap);

	  bcData.data.dict = &(tempMap);
	  dataPair.first = key;
	  dataPair.second = bcData;
	  break;

	case 'i':
      bcData.dataType ='i';
	  //Read the integer value
	  bcData.data.int_data = readInteger(data, offset);
	  dataPair.first = key;
	  dataPair.second = bcData;
	  break;

	case 'l':
	  bcData.dataType ='l';
	  //Read the list value
	  listVal = readList(data, offset);
	  bcData.data.list_data = &(listVal);
	  listVal = std::list<string>();
	  dataPair.first = key;
	  dataPair.second = bcData;
	  break;

	case 'e':
	  stak.pop();
	  offset += 1;
	  break;

	default:
	  bcData.dataType ='s';
	  //Read the string value
	  strVal = readString(data, offset);
	  bcData.data.str_data = (char * )strVal.c_str();
	  int strSize = strlen((char * )strVal.c_str());
	  bcData.data.str_data[strSize] = '\0';
	  dataPair.first = key;
	  dataPair.second = bcData;
	  break;
	}

  return dataPair;
}

/**
 * readDictionary(char * data, int& offset) -> map
 *
 * Reads the nested dictionary values from the torrent file
 **/

map<string, bencodeData> readDictionary(char * data, int& offset) {
  static map<string, bencodeData> nestedDict;
  pair<string, bencodeData> tempDict;
  
  stak.push('d');
  offset += 1;
  while(data[offset] != 'e') {
    //Recursive call to readKeyValuePair to read the values in the dictionary
    tempDict = readKeyValuePair(data, offset);
	if (tempDict.first == "name")
	   strcpy(bt_info.name, tempDict.second.data.str_data);
	nestedDict[tempDict.first] = tempDict.second;	
  }

  offset += 1;
  stak.pop();

  return nestedDict;
}

/**
 * readInteger(char * data, int& offset) -> long int
 *
 * Reads the integer value from the torrent file
 **/

long int readInteger(char * data, int& offset) {
  char * strVal;
  long int intVal;
  int i, j;

  //Read the integer value from  the torrent file and storing it in a char array
  strVal = (char *)malloc(64);
  stak.push('i');
  for (i = offset + 1, j = 0; data[i] != 'e'; i++, j++ )
    strVal[j] = data[i];
  strVal[j] = '\0';
  offset = i + 1;
  stak.pop();

  //Converting the integer value stores in a char array to int type
  intVal = atol(strVal);
  free(strVal);

  return intVal;  
}

/**
 * readList(char * data, int& offset) -> list<string>
 *
 * Reads the list value from the torrent file
 **/

list<string> readList(char * data, int& offset) {
  static list<string> listVal;
  string strVal;

  stak.push('l');
  offset += 1;

  //Reads the string values from the torrent file and adds it to the list
  while (data[offset] != 'e') {
    strVal = readString(data, offset);
    listVal.push_back(strVal);
	offset += 1;
  }
  stak.pop();

  return listVal;
}

/**
 * readString(char * data, int& offset) -> string
 *
 * Reads the string(key/value) from the torrent file  and returns the string
 **/

string readString(char * data, int& offset) {  
  char * strLength;
  int length, i, j;

  if(data[offset] == ':') {
    perror("Torrent file corrupted");
	exit(1);
  }

  //Read the size of string to be read from the torrent file
  strLength = (char *)malloc(64);
  for (i = offset, j = 0; data[i] != ':'; i++, j++ ){
    strLength[j] = data[i];
  }
  strLength[j] = '\0';
  length = atoi(strLength);
  offset = i + length + 1;
  strLength[0] = '\0';
  free(strLength);

  //Read the particular string from the torrent file
  string strVal( data + i + 1 , length);
  /*cout<< "In readString: " << strVal << endl;
  cout<< "In readString: Length - " << strVal.length() << endl;
  fflush(stdout);*/
  return strVal;
}

/**
 * extractUsefulInfo(string key, map<string, bencodeData> infoMap) -> void
 *
 * Stores the necessary torrent info into bt_info
 **/

void extractUsefulInfo(map<string, bencodeData> infoMap) {
  //Storing the values of torrent file into extractUsefulInfo
  bt_info.piece_length = infoMap["piece length"].data.int_data;
  bt_info.length = infoMap["length"].data.int_data;
  bt_info.piece_hashes = infoMap["pieces"].data.str_data;
  bt_info.num_pieces = ceil((double)(infoMap["length"].data.int_data)/(infoMap["piece length"].data.int_data));
}