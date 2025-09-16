#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <mutex>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

#define TOPIC_LEN 64
#define MSG_LEN   1024
#define MAX_CLIENTS 50

// Message Types
#define TYPE_SUBSCRIBE  10
#define TYPE_PUBLISH    11
#define TYPE_MSG        12
#define TYPE_ACK        2
#define TYPE_TERM       3

struct Message {
    int type;
    char topic[TOPIC_LEN];
    int payload_len;
    char content[MSG_LEN];
};

struct ClientInfo {
    SOCKET sock;
    string topic;
    bool subscribed = false;
};

vector<ClientInfo> clients;
mutex clients_mtx;

void handle_client(SOCKET clientSock) {
    Message msg;
    while (true) {
        int n = recv(clientSock, (char*)&msg, sizeof(msg), 0);
        if (n <= 0) {
            cout << "Client disconnected\n";
            closesocket(clientSock);
            return;
        }

        if (msg.type == TYPE_SUBSCRIBE) {
            lock_guard<mutex> lock(clients_mtx);
            for (auto &c : clients) {
                if (c.sock == clientSock) {
                    c.subscribed = true;
                    c.topic = msg.topic;
                }
            }
            cout << "Client subscribed to " << msg.topic << endl;
            Message ack{};
            ack.type = TYPE_ACK;
            strncpy(ack.topic, msg.topic, TOPIC_LEN);
            send(clientSock, (char*)&ack, sizeof(ack), 0);
        }
        else if (msg.type == TYPE_PUBLISH) {
            cout << "Publish on topic " << msg.topic
                 << " : " << msg.content << endl;
            lock_guard<mutex> lock(clients_mtx);
            for (auto &c : clients) {
                if (c.subscribed && c.topic == msg.topic) {
                    send(c.sock, (char*)&msg, sizeof(msg), 0);
                }
            }
            Message ack{};
            ack.type = TYPE_ACK;
            strncpy(ack.topic, msg.topic, TOPIC_LEN);
            send(clientSock, (char*)&ack, sizeof(ack), 0);
        }
        else if (msg.type == TYPE_TERM) {
            cout << "Client terminated\n";
            closesocket(clientSock);
            return;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <port>\n";
        return 1;
    }
    int PORT = stoi(argv[1]);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET serverSock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSock, 5);

    cout << "Server listening on port " << PORT << endl;

    while (true) {
        sockaddr_in clientAddr{};
        int len = sizeof(clientAddr);
        SOCKET clientSock = accept(serverSock, (sockaddr*)&clientAddr, &len);

        {
            lock_guard<mutex> lock(clients_mtx);
            clients.push_back(ClientInfo{clientSock, "", false});
        }

        thread t(handle_client, clientSock);
        t.detach();
    }

    closesocket(serverSock);
    WSACleanup();
    return 0;
}
