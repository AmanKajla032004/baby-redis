#include <iostream>
#include <unordered_map>
#include <optional>
#include <string>
#include <sstream>
#include <fstream>
#include <cassert>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <ctime>
#include <thread>
#include <mutex>

using namespace std;




//linked list structure to be used in LRU.
struct Node{
    string key;
    string value;
    Node* next;
    Node* prev;
    optional <time_t> expiry;
    Node (string key, string value, Node* prev = nullptr, Node* next = nullptr,
    optional <time_t> expiry = nullopt) : key(key), value(value), prev(prev), next(next), expiry(expiry) {}
};

//LRU class: to handle the Data with linked list and Map.
class LRUCache{
    public:
    LRUCache(int capacity){
        assert(capacity > 0 && "Capacity must be positive");
        Capacity = capacity;
        dummy = new Node("*DummyNode*", "*DummyNode*");
        dummy -> next = dummy;
        dummy -> prev = dummy;
    }
    optional <string> get(string key){
        auto place = map.find(key);
        if (place == map.end()) return nullopt;
        time_t now = ::time(nullptr);
        if (place->second->expiry.has_value() && now >= place->second->expiry.value()) {
            del(key);
            return nullopt;
        }
        touch(place -> second);
        return place -> second -> value;
    }
    optional<string> set(string key, string value, optional <time_t> expiry = nullopt){
        auto place = map.find(key);
        bool evicted = false;
        string lastNodeKey;
        if (place != map.end()){
            place -> second -> value = value;
            touch(place -> second);
        }
        else{
            if (Capacity > map.size()){
                addNew(key, value);
            }
            else{
                lastNodeKey = dummy -> prev -> key;
                int delStatus = del(dummy -> prev -> key);
                if (delStatus == 1){
                    addNew(key, value);
                    evicted = true;
                }
            }
        }
        place = map.find(key);
        place -> second -> expiry = expiry;
        if (evicted) return lastNodeKey;
        return nullopt;
    }
    int del(string key){
        auto foundPlace = map.find(key);
        if (foundPlace != map.end()){
            Node* tmp = foundPlace -> second;
            unLink(tmp);
            map.erase(tmp -> key);
            delete tmp;
            tmp = nullptr;
            return 1;
        }
        return 0;
    }
    void sweepExpired(){
        time_t now = ::time(nullptr);
        Node* curr = dummy -> next;
        while (curr != dummy){
            Node* next = curr -> next;
            if (curr -> expiry.has_value() && now >= curr -> expiry.value()){
                del(curr -> key);
            }
            curr = next;
        }
    }

    private:
    void addNew(string key, string value){
        Node* tmp = new Node(key, value);
        map.insert({key, tmp});
        insertFront(tmp);
    }
    void unLink(Node* node){
        node -> next -> prev = node -> prev;
        node -> prev -> next = node -> next;
        node -> next = nullptr;
        node -> prev = nullptr;
    }
    void insertFront(Node* newNode){
        newNode -> next = dummy -> next;
        dummy -> next -> prev = newNode;
        dummy -> next = newNode;
        newNode -> prev = dummy;
    }
    void touch(Node* node){
        unLink(node);
        insertFront(node);
    }
    

    int Capacity;
    Node* dummy;
    unordered_map <string, Node*> map;
};

//code to do the Actions needed. also re run the logfile so that data doesn't get lost.
void writeChange(LRUCache &cache, ofstream *logFile, string s, int clientfd){
    string command, key, value, expiry, duration;
    istringstream expression(s);
    expression >> command >> key >> value >> expiry >> duration;
    if (command == "SET" && value == ""){
        if (logFile != nullptr){
            string response = "Incorrect number of input. Try again\n";
            write(clientfd, response.c_str(), response.size());
        }
    }
    else if (command == "GET") {
        auto val = cache.get(key);
        if (logFile != nullptr){
            string response;
            if (val != nullopt) response = val.value() + "\n";
            else response = "NIL\n";
            write(clientfd, response.c_str(), response.size());
        }
        
    }
    else if (command == "SET") {
        optional<time_t> time = nullopt;
        if (expiry == "EX") {
            if (duration.empty() || duration.find_first_not_of("0123456789") != string::npos) {
                if (logFile != nullptr) {
                    string response = "Invalid Expiry Duration\n";
                    write(clientfd, response.c_str(), response.size());
                }
                return;
            }
            int durationInt = stoi(duration);
            if (durationInt <= 0) {
                if (logFile != nullptr) {
                    string response = "Invalid Expiry Duration\n";
                    write(clientfd, response.c_str(), response.size());
                }
                return;
            }
            time_t now = ::time(nullptr);
            time = now + durationInt;
        }
        optional<string> evictedKey = cache.set(key, value, time);
        if (logFile != nullptr) {
            *logFile << s << endl;
            if (evictedKey != nullopt) 
            *logFile << "DEL " << evictedKey.value() << endl;
            string response = "OK\n";
            write(clientfd, response.c_str(), response.size());
        }
    }
    else if (command == "DEL"){
        int count = cache.del(key);
        if (logFile != nullptr) {
            *logFile << s << endl;
            string response = to_string(count) + "\n";
            write(clientfd, response.c_str(), response.size());
        }
    }
    else {
        if (logFile != nullptr){
            string response = "Unknown Command. Current Command Skipped.\n";
            write(clientfd, response.c_str(), response.size());
        }
    }
}

//main code.
int main(){
    //initiating.
    LRUCache cache(3);
    ofstream logFile("babyredis.log", ios::app);
    ifstream logIn("babyredis.log");

    //setting up the server! and log ready.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);
    //binding, listneing and getting started from the user.
    int bindStatus = bind(sockfd, (sockaddr*)&addr, sizeof(addr));;
    if (bindStatus != 0){
        perror("Bind Failed!");
        return 1;
    }
    int backlog = 1;
    int listenStatus = listen(sockfd, backlog);;
    if (listenStatus != 0){
        perror("Listen Failed!");
        return 1;
    }
    string logLine;
    //write the past situation how they happened.
    while (getline(logIn, logLine)) writeChange(cache, nullptr, logLine, -1);

    
    mutex cacheMutex;
    //Handles multiple user.
    while (true){
        string inputBuffer;
        cache.sweepExpired();
        int clientfd = accept(sockfd, nullptr, nullptr);
        if (clientfd < 0) {
            perror("Accept Failed!");
            continue;
        }
        thread  clientThread(handleClient, clientfd, &logFile, ref(cacheMutex));
        clientThread.detach(); //concurrency
        //handles only the current working user (thread).
        while (true){
            char temp[1024];
            ssize_t bytesRead = read(clientfd, temp, sizeof(temp));
            if (bytesRead <= 0) break;  // 0 = clean disconnect, -1 = error; either way stop

            inputBuffer.append(temp, bytesRead);
            bool userQUIT = false;
            size_t pos;
            while ((pos = inputBuffer.find('\n')) != string::npos) {
                string command = inputBuffer.substr(0, pos);
                inputBuffer.erase(0, pos + 1);
                if (command == "QUIT") {
                    userQUIT = true;
                    string response = "Quiting the Session!\n";
                    write(clientfd, response.c_str(), response.size());
                    break;
                }
                writeChange(cache, &logFile, command, clientfd);
            }
            if (userQUIT) break;
        }
        close(clientfd);
    }
    
    return 0;
}


/* Layer 1 Dead Code (future layers earlier framework).
class Store{
    public:
    void set (const string &key, const string &value){
        data[key] = value;
    }
    optional <string> get (const string &key) const {
        auto temp = data.find(key);
        if (temp == data.end()) return nullopt;
        return temp -> second;
    }
    int del (const string &key){
        return data.erase(key);
    }

    private:
    unordered_map <string, string> data;
};


//Layer 1-3 input format until we exit in main.
    while (true) {
        string line;
        getline(cin, line);
        if (line == "QUIT") break;
        writeChange(cache, &logFile, line);
    }
    
//code to do the Actions needed. also re run the logfile so that data doesn't get lost.
from layer 1-3
void writeChange(LRUCache &cache, ofstream *logFile, string s){
    string command, key, value;
    istringstream expression(s);
    expression >> command >> key >> value;
    if (command == "SET" && value == ""){
        cout << "Incorrect number of input. Try again" << endl;
    }
    else if (command == "GET") {
        auto val = cache.get(key);
        if (val != nullopt) cout << val.value() << endl;
        else cout << "nil" << endl;
    }
    else if (command == "SET") {
        cache.set(key, value);
        if (logFile != nullptr) {
            *logFile << s << endl;
            cout << "OK" << endl;
        }
    }
    else if (command == "DEL"){
        int count = cache.del(key);
        if (logFile != nullptr) {
            *logFile << s << endl;
            cout << count << endl;
        }
    }
    else {
        cout << "Unknown Command. Current Command Skipped." << endl;
    }
}

//Main code till the end of layer 4

//main code.
int main(){
    //initiating.
    LRUCache cache(3);
    ofstream logFile("babyredis.log", ios::app);
    ifstream logIn("babyredis.log");

    //setting up the server! and log ready.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);
    //binding, listneing and getting started from the user.
    int bindStatus = bind(sockfd, (sockaddr*)&addr, sizeof(addr));;
    if (bindStatus != 0){
        perror("Bind Failed!");
        return 1;
    }
    int backlog = 1;
    int listenStatus = listen(sockfd, backlog);;
    if (listenStatus != 0){
        perror("Listen Failed!");
        return 1;
    }
    string logLine;
    //write the past situation how they happened.
    while (getline(logIn, logLine)) writeChange(cache, nullptr, logLine, -1);
    int clientfd = accept(sockfd, nullptr, nullptr);
    if (clientfd < 0) {
        perror("Accept Failed!");
        return 1;
    }

    
    string inputBuffer;
    while (true) {
        char temp[1024];
        ssize_t bytesRead = read(clientfd, temp, sizeof(temp));
        if (bytesRead <= 0) break;  // 0 = clean disconnect, -1 = error; either way stop

        inputBuffer.append(temp, bytesRead);

        size_t pos;
        while ((pos = inputBuffer.find('\n')) != string::npos) {
            string command = inputBuffer.substr(0, pos);
            inputBuffer.erase(0, pos + 1);
            if (command == "QUIT") return 0;
            writeChange(cache, &logFile, command, clientfd);
        }
    }
    
    return 0;
}


*/