#include <iostream>
#include <fstream>
#include <vector>

using namespace std;

int main() {
    
    ifstream index("file_index.txt");
    string filename, fileID;

    cout << "Available Files:\n";

    vector<pair<string, string>> files;

    while (index >> filename >> fileID) {
    	cout << files.size() << ". " << filename << endl;
    	files.push_back({filename, fileID});
    }

    int choice;
    cout << "Enter file number: ";
    cin >> choice;
    if (choice < 0 || choice >= files.size()) {
    	cout << "Invalid choice\n";
    	return 1;
    }
    fileID = files[choice].second;
    ofstream output(fileID + "_output.txt", ios::binary);

    int chunkNumber = 0;

    while (true) {
        string chunkFileName = fileID + "_chunk_" + to_string(chunkNumber) + ".txt";
        ifstream chunkFile(chunkFileName, ios::binary);

        // Stop if file not found
        if (!chunkFile) break;

        char buffer[1024];

        while (chunkFile.read(buffer, sizeof(buffer))) {
            output.write(buffer, sizeof(buffer));
        }

        // Write remaining bytes
        output.write(buffer, chunkFile.gcount());

        cout << "Merged " << chunkFileName << endl;

        chunkFile.close();
        chunkNumber++;
    }

    output.close();

    cout << "\nFile reconstructed as " << fileID + "_output.txt" << endl;

    return 0;
}
