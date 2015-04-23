CC=g++
CPFLAGS=-g -Wall
LDFLAGS= -lcrypto -pthread


SRC= bt_client.cpp bt_lib.cpp bt_setup.cpp bt_parser.cpp
OBJ=$(SRC:.cpp=.o)
BIN=bt_client

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CPFLAGS) $(LDFLAGS) -o $(BIN) $(OBJ) 


%.o:%.cpp
	$(CC) -c $(CPFLAGS) -o $@ $<  

$(SRC):

clean:
	rm -rf $(OBJ) $(BIN)
