#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <chrono>
#include <vector>
#include <algorithm>
#include <sstream>

using namespace std;

int main(){
    //setting up the client side.
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(8080);
    int status = connect(socketfd, (sockaddr*)&addr, sizeof(addr));
    if (status != 0){
        perror("Connection Failed!");
        return 1;
    }
    int N = 1000;
    vector<double> latencies;
    char buffer[1024];

    auto startTotal = chrono::high_resolution_clock::now();

    for (int i = 0; i < N; i++) {
        string command = "SET benchkey" + to_string(i) + " val\n";

        auto t1 = chrono::high_resolution_clock::now();
        write(socketfd, command.c_str(), command.size());
        read(socketfd, buffer, sizeof(buffer));
        auto t2 = chrono::high_resolution_clock::now();

        double ms = chrono::duration<double, milli>(t2 - t1).count();
        latencies.push_back(ms);
    }

    auto endTotal = chrono::high_resolution_clock::now();
    double totalSeconds = chrono::duration<double>(endTotal - startTotal).count();

    double sum = 0, minLat = latencies[0], maxLat = latencies[0];
    for (double l : latencies) {
        sum += l;
        minLat = min(minLat, l);
        maxLat = max(maxLat, l);
    }

    cout << "Ops: " << N << "\n";
    cout << "Total time: " << totalSeconds << " sec\n";
    cout << "Throughput: " << (N / totalSeconds) << " ops/sec\n";
    cout << "Avg latency: " << (sum / N) << " ms\n";
    cout << "Min latency: " << minLat << " ms\n";
    cout << "Max latency: " << maxLat << " ms\n";

    close(socketfd);
    return 0;
}