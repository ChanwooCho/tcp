#include <iostream>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#include <json/json.h>  // If you use a JSON parser like jsoncpp

std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

int main() {
    // Call the termux-wifi-connectioninfo command
    std::string wifiInfoJson = exec("termux-wifi-connectioninfo");

    // Print the JSON output
    std::cout << "Wi-Fi Info JSON: " << wifiInfoJson << std::endl;

    // Parse the JSON to extract the RSSI value
    Json::Value jsonData;
    Json::CharReaderBuilder builder;
    std::string errs;
    
    std::istringstream s(wifiInfoJson);
    if (Json::parseFromStream(builder, s, &jsonData, &errs)) {
        int rssi = jsonData["rssi"].asInt();
        std::cout << "RSSI: " << rssi << " dBm" << std::endl;
    } else {
        std::cerr << "Failed to parse JSON: " << errs << std::endl;
    }

    return 0;
}
