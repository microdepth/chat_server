#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <fstream>
using namespace std;

// stream buffer for redirecting output
class TeeBuf : public streambuf {
public:
    TeeBuf(streambuf* sb1, streambuf* sb2) : buf1(sb1), buf2(sb2) {
        if (!buf1 || !buf2) {
            throw std::invalid_argument("streambuf pointers cant be null");
        }
    };

    int overflow(int c) override {
        if (c == EOF) {
            return EOF;
        } else {

            const char ch = static_cast<char>(c);
            if (buf1->sputc(ch) == EOF || buf2->sputc(ch) == EOF) {
                return EOF;
            }
            return c;
        }
    }

    int sync() override {
        if (buf1->pubsync() == -1 || buf2->pubsync() == -1) {
            return -1;
        }
        return 0;
    }

private:
    streambuf* buf1;
    streambuf* buf2;
};
struct Client {
    SOCKET socket;
    int id;
};

vector<Client*> clients;
int client_count = 0;
mutex m;
const string encryption_key = "monkey";
bool is_running = true;

void display_clients() {
    for (Client* client : clients) {
        cout << client->id << endl;
    }
}
string xor_text_encryption(string& data, const string& key) {
    for (size_t i = 0; i < data.size(); i++) {
        data[i] ^= key[i % key.size()];
    }
    return data;
}
void log(const string& level, const string& msg) {
    auto current_time = time(nullptr);
    auto local_time = *localtime(&current_time);
    cout << put_time(&local_time, "[%Y-%m-%d %H:%M:%S] ") << "[" << level << "] " << msg << endl;
}
DWORD WINAPI ClientHandler(LPVOID lp_param) {
    auto* client = (Client*)lp_param;

    char recv_buffer[512] = {0};
    int bytes_recieved;

    fd_set readfds;
    timeval tv{};
    int retval;

    while (true) {
        FD_ZERO(&readfds);
        FD_SET(client->socket, &readfds);

        tv.tv_sec = 1;
        tv.tv_usec = 0;

        retval = select(client->socket + 1, &readfds, nullptr, nullptr, &tv);
        if (retval < 0) {
            string message = "select failed, error: " + to_string(WSAGetLastError());
            log("ERROR", message);
            break;
        } else if (retval > 0) {
            bytes_recieved = recv(client->socket, recv_buffer, sizeof(recv_buffer) - 1, 0);
            if (bytes_recieved > 0) {
                recv_buffer[bytes_recieved] = '\0';
                string recieved_message(recv_buffer);
                log("INFO", "{client " + to_string(client->id) + "} received: " + recieved_message);
                memset(recv_buffer, 0, sizeof(recv_buffer));
                continue;
            } else if (bytes_recieved == 0) {
                m.lock();
                clients.erase(
                        remove_if(
                                clients.begin(),
                                clients.end(),
                                [&client](const Client *c) { return c->id == client->id; }
                        ),
                        clients.end()
                );
                m.unlock();
                string message = string("{client ") + to_string(client->id) + "} disconnected!";
                log("INFO", message);
                break;
            } else if (bytes_recieved < 0) {
                string message = "{client " + to_string(client->id) + "} recv failed, error: " + to_string(WSAGetLastError());
                log("ERROR", message);
                break;
            }
        } else if (retval == 0) {

        }
    }

    closesocket(client->socket);
    delete client;
    client = nullptr;
    m.lock();
    client_count--;
    m.unlock();
    return 0;
}

int main() {
    WSADATA wsa_data{};

    int init_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);

    if (init_result != 0) {
        string message = "WSAStartup failed, error: " + to_string(WSAGetLastError());
        log("ERROR", message);
        return 1;
    }

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        string message = "socket failed, error: " +  to_string(WSAGetLastError());
        log("ERROR", message);
        WSACleanup();
        return 1;
    }

    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(8080);

    if (bind(server_socket, (sockaddr*)&server_address, sizeof(server_address)) < 0) {
        string message = "bind failed, error: " + to_string(WSAGetLastError());
        log("ERROR", message);
        WSACleanup();
        return 1;
    }
    if (listen(server_socket, 3) < 0) {
        string message = "listen failed, error: " + to_string(WSAGetLastError());
        log("ERROR", message);
        WSACleanup();
        return 1;
    }

    sockaddr_in client_address{};
    int addrlen = sizeof(client_address);

    fd_set readfds;
    timeval tv{};
    int retval;

    int max_fd = server_socket;

    for (Client* c : clients) {
        if (c->socket > max_fd) {
            max_fd = c->socket;
        }
    }

    max_fd += 1;

    while (is_running) {

        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);

        tv.tv_sec = 5;
        tv.tv_usec = 0;

        retval = select(max_fd, &readfds, nullptr, nullptr, &tv);
        if (retval < 0) {
            string message = "select failed, error: " + to_string(WSAGetLastError());
            log("ERROR", message);
        } else if (retval > 0) {
            Client* new_client = new Client;
            clients.push_back(new_client);

            if (FD_ISSET(server_socket, &readfds)) {
                new_client->socket = accept(server_socket, (sockaddr*)&client_address, &addrlen);

                if (new_client->socket == INVALID_SOCKET) {
                    string message = "{client" + to_string(new_client->id) + "} accept failed, error: " + to_string(WSAGetLastError());
                    log("ERROR", message);
                    delete new_client;
                    clients.pop_back();
                    continue;
                } else {
                    new_client->id = ++client_count;
                    string message = "{client " + to_string(new_client->id) + "} connected!";
                    log("INFO", message);
                    message = "you are client number {" + to_string(new_client->id) + "}";
                    if (send(new_client->socket, message.c_str(), message.length(), 0) == SOCKET_ERROR) {
                        message = "{client" + to_string(new_client->id) + "} send failed, error: " + to_string(WSAGetLastError());
                        log("ERROR", message);
                    }
                }

                if (!QueueUserWorkItem(ClientHandler, (LPVOID)new_client, WT_EXECUTEDEFAULT)) {
                    string message = "{client " + to_string(new_client->id) + "} queueuserworkitem failed, error: " + to_string(GetLastError());
                    log("ERROR", message);

                    closesocket(new_client->socket);
                    delete new_client;
                    clients.pop_back();
                }
            }

        } else {
            continue;
        }
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}
