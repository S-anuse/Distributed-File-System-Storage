#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <map>
#include <vector>
#include <cstring>

using namespace std;

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    map<string, map<int, vector<int>>> metadata;
    // fileID → (chunkNumber → servers)

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(9000);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 3);

    cout << "Master Server running on port 9000...\n";

    new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);

    int chunkNumber, port;
    char fileID[200];

    while (true) {
        memset(fileID, 0, sizeof(fileID));
	int bytes = read(new_socket, fileID, sizeof(fileID));
	if (bytes <= 0) break;

	read(new_socket, &chunkNumber, sizeof(chunkNumber));
	read(new_socket, &port, sizeof(port));

        metadata[string(fileID)][chunkNumber].push_back(port);

        cout << "File: " << fileID 
	<< " | Chunk " << chunkNumber 
        << " stored at port " << port << endl;
    }

    cout << "\nFinal Metadata:\n";

    for (auto &fileEntry : metadata) {
    	cout << "File: " << fileEntry.first << endl;

    	for (auto &chunkEntry : fileEntry.second) {
        	cout << "  Chunk " << chunkEntry.first << " → ";
        	for (int p : chunkEntry.second) {
            		cout << p << " ";
        	}
        	cout << endl;
    	}
    }

    close(new_socket);
    close(server_fd);

    return 0;
}
