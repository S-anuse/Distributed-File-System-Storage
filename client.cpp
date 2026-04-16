#include <iostream>
#include <fstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <vector>
#include <ctime>

using namespace std;
#define CHUNK_SIZE 512 


int createConnection(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        cout << "Connection failed to port " << port << endl;
        return -1;
    }

    cout << "Connected to port " << port << endl;
    return sock;
}

int main() {
    vector<int> ports = {8080, 8081, 8082};
    vector<int> sockets;

    // Connect to all servers
    vector<int> activePorts;

    for (int port : ports) {
    	int sock = createConnection(port);

	if (sock != -1) {
        	sockets.push_back(sock);
	        activePorts.push_back(port);
    	} 
	else {
        	cout << "Skipping port " << port << endl;
    	}
     }
     
    // Connect to master server
    int masterSock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in master_addr;

    master_addr.sin_family = AF_INET;
    master_addr.sin_port = htons(9000);
    inet_pton(AF_INET, "127.0.0.1", &master_addr.sin_addr);

    if (connect(masterSock, (struct sockaddr *)&master_addr, sizeof(master_addr)) < 0) {
    	cout << "Connection to Master failed\n";
    	return -1;
    }

    cout << "Connected to Master Server\n";
    
    ifstream file("test.txt", ios::binary);
    string filename = "test.txt";

    // generate timestamp
    time_t now = time(0);

    // unique fileID
    string fileID = filename + "_" + to_string(now);
    ofstream index("file_index.txt", ios::app);
    index << filename << " " << fileID << endl;
    index.close();

    cout << "Generated FileID: " << fileID << endl;
    char buffer[CHUNK_SIZE];

    int chunkNumber = 0;
    int serverIndex = 0;

    while (file.read(buffer, CHUNK_SIZE)) {
        int primary = serverIndex;
	int secondary = (serverIndex + 1) % sockets.size();

	// Send to primary
	if (sockets[primary] != -1) {
    		int dataSize = CHUNK_SIZE;

		// send fileID
		send(sockets[primary], fileID.c_str(), fileID.size() + 1, 0);

		// send chunk number
		send(sockets[primary], &chunkNumber, sizeof(chunkNumber), 0);

		// send data size
		send(sockets[primary], &dataSize, sizeof(dataSize), 0);

		// send actual data
		send(sockets[primary], buffer, dataSize, 0);
	}

	// Send to secondary (replica)
	if (sockets[secondary] != -1) {
    		int dataSize = CHUNK_SIZE;

		// send fileID
		send(sockets[secondary], fileID.c_str(), fileID.size() + 1, 0); 

		// send chunk number
		send(sockets[secondary], &chunkNumber, sizeof(chunkNumber), 0); 

		// send data size
		send(sockets[secondary], &dataSize, sizeof(dataSize), 0); 

		// send actual data
		send(sockets[secondary], buffer, dataSize, 0);
	}

	cout << "Chunk " << chunkNumber << " stored at "<< activePorts[primary] << " and " << activePorts[secondary] << endl;
 	
	// Send metadata to master
	// ✅ Send BOTH to master
	send(masterSock, fileID.c_str(), fileID.size() + 1, 0);
	send(masterSock, &chunkNumber, sizeof(chunkNumber), 0);
	send(masterSock, &activePorts[primary], sizeof(int), 0);

	send(masterSock, fileID.c_str(), fileID.size() + 1, 0);
	send(masterSock, &chunkNumber, sizeof(chunkNumber), 0);
	send(masterSock, &activePorts[secondary], sizeof(int), 0);

        cout << "Sent Chunk " << chunkNumber
     << " to Servers " << activePorts[primary]
     << " and " << activePorts[secondary] << endl;

        chunkNumber++;
        serverIndex = (serverIndex + 1) % sockets.size();
    }


    // remaining bytes
    if (file.gcount() > 0) {
	int primary = serverIndex;
        int secondary = (serverIndex + 1) % sockets.size();
        int dataSize = file.gcount();

	// primary
	send(sockets[primary], fileID.c_str(), fileID.size() + 1, 0);
	send(sockets[primary], &chunkNumber, sizeof(chunkNumber), 0);
	send(sockets[primary], &dataSize, sizeof(dataSize), 0);
	send(sockets[primary], buffer, dataSize, 0);

	// secondary
	send(sockets[secondary], fileID.c_str(), fileID.size() + 1, 0);
	send(sockets[secondary], &chunkNumber, sizeof(chunkNumber), 0);
	send(sockets[secondary], &dataSize, sizeof(dataSize), 0);
	send(sockets[secondary], buffer, dataSize, 0);

	// metadata
	send(masterSock, fileID.c_str(), fileID.size() + 1, 0);
	send(masterSock, &chunkNumber, sizeof(chunkNumber), 0);
	send(masterSock, &activePorts[primary], sizeof(int), 0);

	send(masterSock, fileID.c_str(), fileID.size() + 1, 0);
	send(masterSock, &chunkNumber, sizeof(chunkNumber), 0);
	send(masterSock, &activePorts[secondary], sizeof(int), 0);

    }

    cout << "File distributed across servers\n";

    file.close();
    
    for (int sock : sockets) {
        close(sock);
    }
    close(masterSock);

    return 0;
}
