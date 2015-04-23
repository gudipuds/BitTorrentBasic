#ifndef _BT_PARSER_H
#define _BT_PARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <map>
#include <list>

#include "bt_setup.h"
#include "bt_lib.h"
#include "bt_parser.h"

using namespace std;

//Holds information read from a torrent file
typedef struct bencodeData{

  //Variable to identify the value stored in Bencode Value of <Key,Value> pair
  char dataType; //Stores value 'i' for int, 's' for string, 'd' for dictionary, 'l' for list

  union {
    long int int_data;
	char * str_data;
	map<string, bencodeData> * dict;
	list<string> * list_data;
   } data;

} bencodeData_t;


/**
 * parseTorrentFile(char fileName[]) -> map
 *
 * Parses the torrent file and stores data in the bencodeData structure.
 **/
map<string, bencodeData> parseTorrentFile(char fileName[]);

/**
 * readKeyValuePair(char * data, int& offset) -> map
 *
 * Reads the key value pairs from the torrent file
 **/
pair<string, bencodeData> readKeyValuePair(char * data, int& offset);

/**
 * readDictionary(char * data, int& offset) -> map
 *
 * Reads the nested dictionary values from the torrent file
 **/
map<string, bencodeData> readDictionary(char * data, int& offset);

/**
 * readList(char * data, int& offset) -> list<string>
 *
 * Reads the list value from the torrent file
 **/
list<string> readList(char * data, int& offset);

/**
 * readInteger(char * data, int& offset) -> long int
 *
 * Reads the integer value from the torrent file
 **/
long int readInteger(char * data, int& offset);

/**
 * readString(char * data, int& offset) -> string
 *
 * Reads the string(key/value) from the torrent file  and returns the string
 **/
string readString(char * data, int& offset);

/**
 * extractUsefulInfo(string key, map<string, bencodeData> infoMap) -> void
 *
 * Stores the 
 **/
void extractUsefulInfo(map<string, bencodeData> infoMap);

#endif