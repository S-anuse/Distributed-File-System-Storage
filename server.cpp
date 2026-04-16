#include <iostream>
#include <fstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>

using namespace std;

#define CHUNK_SIZE 512

int main(int argc, char* argv[]) {

    if (argc < 2) {
        cout << "Usage: ./server <port>\n";
        return 1;
    }
    int port = atoi(argv[1]);

    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    char buffer[CHUNK_SIZE];

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    	perror("Bind failed");
    	return 1;
    }
    listen(server_fd, 3);

    cout << "Server listening on port " << port << "...\n";

    new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
     

    int bytesRead;
    int chunkNumber ;
    int dataSize;
    char fileID[200];

    while (true) {
    	// 1. Read chunk number
	memset(fileID, 0, sizeof(fileID));  // clear buffer
	int bytes = read(new_socket, fileID, sizeof(fileID));
    	if (bytes <= 0) break;
	
	read(new_socket, &chunkNumber, sizeof(chunkNumber));

    	// 2. Read actual chunk size
	read(new_socket, &dataSize, sizeof(dataSize));

    	// 3. Read actual data
    	int totalRead = 0;
    	while (totalRead < dataSize) {
        	int r = read(new_socket, buffer + totalRead, dataSize - totalRead);
        	if (r <= 0) break;
        	totalRead += r;
    	}

    	// 4. Save chunk
    	string chunkFile = string(fileID) + "_chunk_" + to_string(chunkNumber) + ".txt";
    	ofstream chunkOut(chunkFile, ios::binary);

    	chunkOut.write(buffer, dataSize);
    	chunkOut.close();

    	cout << "[Port " << port << "] Received Chunk " 
     << chunkNumber << " for File " << fileID << endl;
	}


    close(new_socket);
    close(server_fd);

    return 0;
}
