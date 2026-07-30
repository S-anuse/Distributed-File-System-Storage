#include "socket_helper.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <ctime>
#include <cstring>
#include <stdint.h>

using namespace std;

#define CHUNK_SIZE 512

struct UploadPacket {
    char fileID[256];
    int chunkNumber;
    int port;
};

// Function to establish a socket connection to a specific port
SOCKET connectToPort(int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (!IS_VALIDSOCKET(sock)) return INVALID_SOCKET;

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Set connection timeout (optional, but good for fault tolerance)
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

// Function to upload a chunk to a storage server
bool uploadChunk(int port, const string& fileID, int chunkNumber, const char* data, int dataSize) {
    SOCKET sock = connectToPort(port);
    if (!IS_VALIDSOCKET(sock)) {
        cout << "[Client] Connection failed to storage server on port " << port << endl;
        return false;
    }

    // 1. Send Command
    char cmd = 'U';
    send(sock, &cmd, 1, 0);

    // 2. Send File ID (padded to 256 bytes)
    char fileIDBuffer[256];
    memset(fileIDBuffer, 0, sizeof(fileIDBuffer));
    strncpy(fileIDBuffer, fileID.c_str(), sizeof(fileIDBuffer) - 1);
    send(sock, fileIDBuffer, sizeof(fileIDBuffer), 0);

    // 3. Send Chunk Number
    send(sock, (char*)&chunkNumber, sizeof(chunkNumber), 0);

    // 4. Send Data Size
    send(sock, (char*)&dataSize, sizeof(dataSize), 0);

    // 5. Send Data Payload
    send(sock, data, dataSize, 0);

    // 6. Receive status response
    char status = 0;
    int r = recv(sock, &status, 1, 0);
    CLOSE_SOCKET(sock);

    return (r > 0 && status == 'S');
}

// Send registration metadata packet to the Master Server
void registerMetadata(SOCKET masterSock, const string& fileID, int chunkNumber, int port) {
    UploadPacket packet;
    memset(packet.fileID, 0, sizeof(packet.fileID));
    strncpy(packet.fileID, fileID.c_str(), sizeof(packet.fileID) - 1);
    packet.chunkNumber = chunkNumber;
    packet.port = port;

    send(masterSock, (char*)&packet, sizeof(UploadPacket), 0);
}

int main() {
    if (!init_network()) {
        cerr << "Failed to initialize network\n";
        return 1;
    }

    vector<int> ports = {8080, 8081, 8082};

    // Connect to Master Server
    SOCKET masterSock = connectToPort(9000);
    if (!IS_VALIDSOCKET(masterSock)) {
        cerr << "Connection to Master Server (port 9000) failed. Please start the Master Server first.\n";
        cleanup_network();
        return 1;
    }
    cout << "Connected to Master Server.\n";

    // Send 'U' command to Master Server to initiate metadata upload session
    char masterCmd = 'U';
    send(masterSock, &masterCmd, 1, 0);

    ifstream file("test.txt", ios::binary);
    if (!file) {
        cerr << "Error opening 'test.txt'. Make sure the file exists in the current directory.\n";
        CLOSE_SOCKET(masterSock);
        cleanup_network();
        return 1;
    }

    string filename = "test.txt";
    time_t now = time(0);
    string fileID = filename + "_" + to_string(now);

    // Save filename to fileID mapping in file_index.txt
    ofstream index("file_index.txt", ios::app);
    index << filename << " " << fileID << endl;
    index.close();

    cout << "Uploading File: " << filename << " as Unique ID: " << fileID << endl;

    char buffer[CHUNK_SIZE];
    int chunkNumber = 0;
    int serverIndex = 0;

    while (!file.eof()) {
        file.read(buffer, CHUNK_SIZE);
        int dataSize = file.gcount();
        if (dataSize <= 0) break;

        int primaryPort = ports[serverIndex];
        int secondaryPort = ports[(serverIndex + 1) % ports.size()];

        cout << "Processing Chunk " << chunkNumber << " (Size: " << dataSize << " bytes)" << endl;

        // Try Uploading to Primary Server
        bool primarySuccess = uploadChunk(primaryPort, fileID, chunkNumber, buffer, dataSize);
        if (primarySuccess) {
            cout << "  Chunk " << chunkNumber << " uploaded successfully to Primary: Port " << primaryPort << endl;
            registerMetadata(masterSock, fileID, chunkNumber, primaryPort);
        } else {
            cout << "  [Warning] Failed to upload Chunk " << chunkNumber << " to Primary: Port " << primaryPort << endl;
        }

        // Try Uploading to Secondary Server (Replica)
        bool secondarySuccess = uploadChunk(secondaryPort, fileID, chunkNumber, buffer, dataSize);
        if (secondarySuccess) {
            cout << "  Chunk " << chunkNumber << " uploaded successfully to Replica: Port " << secondaryPort << endl;
            registerMetadata(masterSock, fileID, chunkNumber, secondaryPort);
        } else {
            cout << "  [Warning] Failed to upload Chunk " << chunkNumber << " to Replica: Port " << secondaryPort << endl;
        }

        if (!primarySuccess && !secondarySuccess) {
            cout << "  [Error] Chunk " << chunkNumber << " failed on ALL replica servers! Reliable retrieval for this chunk will not be possible.\n";
        }

        chunkNumber++;
        serverIndex = (serverIndex + 1) % ports.size();
    }

    file.close();
    CLOSE_SOCKET(masterSock);
    cleanup_network();

    cout << "Upload process complete. Metadata updated on Master Server.\n";
    return 0;
}
