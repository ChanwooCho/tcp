#include <iostream>
#include <cstdio>
#include <string>
#include <sstream>
#include <vector>

// Function to execute a command and return the output
std::string execCommand(const char* cmd) {
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) throw std::runtime_error("popen() failed!");
    try {
        while (fgets(buffer, sizeof buffer, pipe) != NULL) {
            result += buffer;
        }
    } catch (...) {
        pclose(pipe);
        throw;
    }
    pclose(pipe);
    return result;
}

// Function to parse RSSI from termux-wifi-scaninfo output
int getRSSI() {
    std::string output = execCommand("termux-wifi-scaninfo");
    std::istringstream ss(output);
    std::string line;
    int rssi = -1; // Default invalid RSSI value

    while (std::getline(ss, line)) {
        // Check if the line contains "level" (RSSI information)
        size_t pos = line.find("\"level\":");
        if (pos != std::string::npos) {
            // Extract the RSSI value after "level":
            std::string rssiStr = line.substr(pos + 8);
            rssi = std::stoi(rssiStr);
            break;
        }
    }

    return rssi;
}

int main() {
    try {
        int rssi = getRSSI();
        if (rssi != -1) {
            std::cout << "RSSI: " << rssi << " dBm" << std::endl;
        } else {
            std::cout << "Unable to retrieve RSSI" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
