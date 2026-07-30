#include <iostream>
#include <map>
#include <vector>
#include <cstring>
#include <stdint.h>
#include "socket_helper.h"

using namespace std;

// Shared metadata structure: fileID -> (chunkNumber -> vector of ports)
map<string, map<int, vector<int>>> metadata;
Mutex metadataMutex;

// Structure to receive upload metadata
struct UploadPacket {
    char fileID[256];
    int chunkNumber;
    int port;
};

// Thread handler for each connected client
THREAD_ROUTINE handleClient(THREAD_ARG arg) {
    SOCKET client_sock = (SOCKET)(uintptr_t)arg;
    char cmd = 0;

    int bytes = recv(client_sock, &cmd, 1, 0);
    if (bytes <= 0) {
        CLOSE_SOCKET(client_sock);
        return 0;
    }

    if (cmd == 'U') {
        // Upload metadata
        while (true) {
            UploadPacket packet;
            int r = recv(client_sock, (char*)&packet, sizeof(UploadPacket), 0);
            if (r <= 0) break;

            LockGuard lock(metadataMutex);
            string fId(packet.fileID);
            
            // Avoid duplicate ports for the same chunk
            bool exists = false;
            for (int p : metadata[fId][packet.chunkNumber]) {
                if (p == packet.port) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                metadata[fId][packet.chunkNumber].push_back(packet.port);
            }

            cout << "[Master] Registered Chunk " << packet.chunkNumber 
                 << " of File " << fId << " at Port " << packet.port << endl;
        }
    } 
    else if (cmd == 'Q') {
        // Query metadata
        char fileID[256];
        memset(fileID, 0, sizeof(fileID));
        int r = recv(client_sock, fileID, sizeof(fileID), 0);
        if (r > 0) {
            string fId(fileID);
            int numChunks = 0;
            
            LockGuard lock(metadataMutex);
            auto it = metadata.find(fId);
            if (it != metadata.end()) {
                if (!it->second.empty()) {
                    numChunks = it->second.rbegin()->first + 1;
                }
                
                // Send number of chunks
                send(client_sock, (char*)&numChunks, sizeof(numChunks), 0);
                
                // For each chunk, send available ports
                for (int c = 0; c < numChunks; ++c) {
                    vector<int> ports = it->second[c];
                    int numReplicas = ports.size();
                    send(client_sock, (char*)&numReplicas, sizeof(numReplicas), 0);
                    if (numReplicas > 0) {
                        send(client_sock, (char*)ports.data(), numReplicas * sizeof(int), 0);
                    }
                }
                cout << "[Master] Query served for File ID: " << fId 
                     << " (Total Chunks: " << numChunks << ")" << endl;
            } else {
                // File not found
                send(client_sock, (char*)&numChunks, sizeof(numChunks), 0);
                cout << "[Master] Query for non-existent File ID: " << fId << endl;
            }
        }
    }

    CLOSE_SOCKET(client_sock);
    return 0;
}

int main() {
    if (!init_network()) {
        cerr << "Failed to initialize Winsock\n";
        return 1;
    }

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!IS_VALIDSOCKET(server_fd)) {
        cerr << "Failed to create socket\n";
        cleanup_network();
        return 1;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(9000);

    // Reuse address
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        cerr << "Bind failed on port 9000\n";
        CLOSE_SOCKET(server_fd);
        cleanup_network();
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        cerr << "Listen failed\n";
        CLOSE_SOCKET(server_fd);
        cleanup_network();
        return 1;
    }

    cout << "Master Server running on port 9000...\n";

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        SOCKET new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        
        if (IS_VALIDSOCKET(new_socket)) {
            start_thread(handleClient, (THREAD_ARG)(uintptr_t)new_socket);
        }
    }

    CLOSE_SOCKET(server_fd);
    cleanup_network();
    return 0;
}
