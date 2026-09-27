#include <iostream>
#include <fstream>
#include <cstring>
#include <stdint.h>
#include "socket_helper.h"

using namespace std;

#define CHUNK_SIZE 512

struct ClientArg {
    SOCKET sock;
    int server_port;
};



THREAD_ROUTINE handleClient(THREAD_ARG arg) {
    ClientArg* c_arg = (ClientArg*)arg;
    SOCKET client_sock = c_arg->sock;
    int port = c_arg->server_port;
    delete c_arg;

    char cmd = 0;
    if (recv(client_sock, &cmd, 1, 0) <= 0) {
        CLOSE_SOCKET(client_sock);
        return 0;
    }

    if (cmd == 'U') {
        // Upload chunk
        char fileID[256];
        memset(fileID, 0, sizeof(fileID));
        int chunkNumber = 0;
        int dataSize = 0;

        if (!recv_all(client_sock, fileID, sizeof(fileID)) ||
            !recv_all(client_sock, (char*)&chunkNumber, sizeof(chunkNumber)) ||
            !recv_all(client_sock, (char*)&dataSize, sizeof(dataSize))) {
            CLOSE_SOCKET(client_sock);
            return 0;
        }

        char* buffer = new char[dataSize];
        if (!recv_all(client_sock, buffer, dataSize)) {
            delete[] buffer;
            CLOSE_SOCKET(client_sock);
            return 0;
        }

        // Save chunk using unique file name containing server port to avoid collision on localhost
        string chunkFile = "server_" + to_string(port) + "_" + string(fileID) + "_chunk_" + to_string(chunkNumber) + ".txt";
        ofstream chunkOut(chunkFile, ios::binary);
        if (chunkOut) {
            chunkOut.write(buffer, dataSize);
            chunkOut.close();
            
            // Send success confirmation
            char status = 'S';
            send(client_sock, &status, 1, 0);
            cout << "[Port " << port << "] Received and saved Chunk " 
                 << chunkNumber << " for File " << fileID << " (" << dataSize << " bytes)" << endl;
        } else {
            char status = 'F';
            send(client_sock, &status, 1, 0);
            cout << "[Port " << port << "] Failed to save Chunk " << chunkNumber << endl;
        }
        delete[] buffer;
    } 
    else if (cmd == 'D') {
        // Download chunk
        char fileID[256];
        memset(fileID, 0, sizeof(fileID));
        int chunkNumber = 0;

        if (!recv_all(client_sock, fileID, sizeof(fileID)) ||
            !recv_all(client_sock, (char*)&chunkNumber, sizeof(chunkNumber))) {
            CLOSE_SOCKET(client_sock);
            return 0;
        }

        string chunkFile = "server_" + to_string(port) + "_" + string(fileID) + "_chunk_" + to_string(chunkNumber) + ".txt";
        ifstream chunkIn(chunkFile, ios::binary | ios::ate);
        if (!chunkIn) {
            int dataSize = -1;
            send(client_sock, (char*)&dataSize, sizeof(dataSize), 0);
            cout << "[Port " << port << "] Chunk " << chunkNumber << " not found for File " << fileID << endl;
        } else {
            int dataSize = chunkIn.tellg();
            chunkIn.seekg(0, ios::beg);

            char* buffer = new char[dataSize];
            chunkIn.read(buffer, dataSize);
            chunkIn.close();

            // Send chunk size
            send(client_sock, (char*)&dataSize, sizeof(dataSize), 0);
            // Send chunk data
            send(client_sock, buffer, dataSize, 0);

            cout << "[Port " << port << "] Sent Chunk " << chunkNumber 
                 << " for File " << fileID << " (" << dataSize << " bytes)" << endl;
            delete[] buffer;
        }
    }

    CLOSE_SOCKET(client_sock);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cout << "Usage: ./server <port>\n";
        return 1;
    }
    int port = atoi(argv[1]);

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
    address.sin_port = htons(port);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        cerr << "Bind failed on port " << port << "\n";
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

    cout << "Server listening on port " << port << "...\n";

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        SOCKET new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        
        if (IS_VALIDSOCKET(new_socket)) {
            ClientArg* arg = new ClientArg();
            arg->sock = new_socket;
            arg->server_port = port;
            start_thread(handleClient, (THREAD_ARG)arg);
        }
    }

    CLOSE_SOCKET(server_fd);
    cleanup_network();
    return 0;
}
