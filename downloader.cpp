#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <stdint.h>
#include "socket_helper.h"

using namespace std;

// Helper function to read exact number of bytes
bool recv_all(SOCKET s, char* buf, int len) {
    int total = 0;
    while (total < len) {
        int r = recv(s, buf + total, len - total, 0);
        if (r <= 0) return false;
        total += r;
    }
    return true;
}

SOCKET connectToPort(int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (!IS_VALIDSOCKET(sock)) return INVALID_SOCKET;

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Set connection timeout (short for fast replica fallback)
    #ifdef _WIN32
        DWORD timeout = 2000; // 2 seconds
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    #else
        struct timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    #endif

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        CLOSE_SOCKET(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

struct ChunkMetadata {
    vector<int> replicaPorts;
};

int main() {
    if (!init_network()) {
        cerr << "Failed to initialize network\n";
        return 1;
    }

    ifstream index("file_index.txt");
    if (!index) {
        cerr << "No file_index.txt found. Please run the client to upload files first.\n";
        cleanup_network();
        return 1;
    }

    string filename, fileID;
    vector<pair<string, string>> files;

    cout << "Available Files for Retrieval:\n";
    while (index >> filename >> fileID) {
        cout << files.size() << ". " << filename << " (ID: " << fileID << ")\n";
        files.push_back({filename, fileID});
    }
    index.close();

    if (files.empty()) {
        cout << "No files registered in index.\n";
        cleanup_network();
        return 0;
    }

    int choice;
    cout << "Enter file number: ";
    cin >> choice;
    if (choice < 0 || choice >= (int)files.size()) {
        cout << "Invalid choice\n";
        cleanup_network();
        return 1;
    }

    filename = files[choice].first;
    fileID = files[choice].second;

    // 1. Query Master Server for Metadata
    SOCKET masterSock = connectToPort(9000);
    if (!IS_VALIDSOCKET(masterSock)) {
        cerr << "Connection to Master Server (port 9000) failed. Cannot retrieve metadata.\n";
        cleanup_network();
        return 1;
    }

    // Send command 'Q' to Master
    char cmd = 'Q';
    send(masterSock, &cmd, 1, 0);

    // Send File ID (padded to 256 bytes)
    char fileIDBuffer[256];
    memset(fileIDBuffer, 0, sizeof(fileIDBuffer));
    strncpy(fileIDBuffer, fileID.c_str(), sizeof(fileIDBuffer) - 1);
    send(masterSock, fileIDBuffer, sizeof(fileIDBuffer), 0);

    // Receive number of chunks
    int numChunks = 0;
    if (!recv_all(masterSock, (char*)&numChunks, sizeof(numChunks)) || numChunks <= 0) {
        cerr << "Failed to retrieve valid metadata from Master Server (File may not exist or has 0 chunks).\n";
        CLOSE_SOCKET(masterSock);
        cleanup_network();
        return 1;
    }

    cout << "Metadata retrieved. File has " << numChunks << " chunks.\n";
    vector<ChunkMetadata> chunks(numChunks);

    for (int c = 0; c < numChunks; ++c) {
        int numReplicas = 0;
        if (!recv_all(masterSock, (char*)&numReplicas, sizeof(numReplicas))) {
            cerr << "Metadata read error for Chunk " << c << "\n";
            CLOSE_SOCKET(masterSock);
            cleanup_network();
            return 1;
        }

        if (numReplicas > 0) {
            vector<int> ports(numReplicas);
            if (!recv_all(masterSock, (char*)ports.data(), numReplicas * sizeof(int))) {
                cerr << "Metadata read error for Chunk " << c << " ports\n";
                CLOSE_SOCKET(masterSock);
                cleanup_network();
                return 1;
            }
            chunks[c].replicaPorts = ports;
        }
    }
    CLOSE_SOCKET(masterSock);

    // 2. Download each chunk with replica fallback (Fault Tolerance)
    string outputFilename = fileID + "_output.txt";
    ofstream output(outputFilename, ios::binary);
    if (!output) {
        cerr << "Failed to create output file " << outputFilename << endl;
        cleanup_network();
        return 1;
    }

    cout << "\nStarting reconstruction of " << filename << " as " << outputFilename << "...\n";

    for (int c = 0; c < numChunks; ++c) {
        vector<int>& ports = chunks[c].replicaPorts;
        bool chunkRetrieved = false;

        if (ports.empty()) {
            cerr << "[Error] No active storage locations known for Chunk " << c << ". File reconstruction failed.\n";
            output.close();
            remove(outputFilename.c_str());
            cleanup_network();
            return 1;
        }

        // Try replicas sequentially
        for (size_t r = 0; r < ports.size(); ++r) {
            int targetPort = ports[r];
            cout << "  Retrieving Chunk " << c << " (Replica " << r + 1 << "/" << ports.size() << ") from Port " << targetPort << "..." << endl;

            SOCKET serverSock = connectToPort(targetPort);
            if (!IS_VALIDSOCKET(serverSock)) {
                cout << "    [Warning] Failed to connect to server on port " << targetPort << ". Trying next replica...\n";
                continue;
            }

            // Send Download Command
            char sCmd = 'D';
            send(serverSock, &sCmd, 1, 0);

            // Send File ID (padded to 256 bytes)
            send(serverSock, fileIDBuffer, sizeof(fileIDBuffer), 0);

            // Send Chunk Number
            send(serverSock, (char*)&c, sizeof(c), 0);

            // Read response dataSize
            int dataSize = -1;
            if (!recv_all(serverSock, (char*)&dataSize, sizeof(dataSize))) {
                cout << "    [Warning] Failed to receive header from port " << targetPort << ". Trying next replica...\n";
                CLOSE_SOCKET(serverSock);
                continue;
            }

            if (dataSize <= 0) {
                cout << "    [Warning] Server on port " << targetPort << " reports chunk not found. Trying next replica...\n";
                CLOSE_SOCKET(serverSock);
                continue;
            }

            // Read raw bytes payload
            char* chunkBuffer = new char[dataSize];
            if (!recv_all(serverSock, chunkBuffer, dataSize)) {
                cout << "    [Warning] Failed to read chunk payload from port " << targetPort << ". Trying next replica...\n";
                delete[] chunkBuffer;
                CLOSE_SOCKET(serverSock);
                continue;
            }

            // Write payload to output file
            output.write(chunkBuffer, dataSize);
            delete[] chunkBuffer;
            CLOSE_SOCKET(serverSock);

            cout << "    [Success] Successfully downloaded Chunk " << c << " (" << dataSize << " bytes) from port " << targetPort << endl;
            chunkRetrieved = true;
            break; // Success! Move to next chunk
        }

        if (!chunkRetrieved) {
            cerr << "[Error] Failed to retrieve Chunk " << c << " from ALL available replica servers. File reconstruction failed.\n";
            output.close();
            remove(outputFilename.c_str());
            cleanup_network();
            return 1;
        }
    }

    output.close();
    cleanup_network();

    cout << "\nFile reconstruction complete! Saved to " << outputFilename << endl;
    return 0;
}
