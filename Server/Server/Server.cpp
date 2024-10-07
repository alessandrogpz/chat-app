#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

std::vector<SOCKET> clients;
std::map<SOCKET, std::string> clientNames;  // Map to store client names
std::mutex clients_mutex;

void broadcastMessage(const std::string& message, SOCKET sender) {
    std::lock_guard<std::mutex> guard(clients_mutex);  // Lock the mutex only for this section

    for (SOCKET client : clients) {
        if (client != sender) {
            int result = send(client, message.c_str(), message.size() + 1, 0);
            if (result == SOCKET_ERROR) {
                std::cerr << "Failed to send message to a client. Error: " << WSAGetLastError() << std::endl;
            }
        }
    }
}

void handleClient(SOCKET clientSocket) {
    char buf[4096];
    std::string clientName;

    try {
        // Receive the client's name
        ZeroMemory(buf, 4096);
        int bytesReceived = recv(clientSocket, buf, 4096, 0);
        if (bytesReceived > 0) {
            clientName = std::string(buf, 0, bytesReceived);

            // Store the client name in the map
            {
                std::lock_guard<std::mutex> guard(clients_mutex);
                clientNames[clientSocket] = clientName;
            }

            std::cout << "Client '" << clientName << "' connected." << std::endl;

            // Broadcast to other clients that a new user has joined (OUTSIDE mutex lock)
            std::string joinMessage = clientName + " has joined the chat.";
            broadcastMessage(joinMessage, clientSocket);  // Call this outside the mutex lock
        } else {
            std::cerr << "Error receiving client name. Closing connection." << std::endl;
            closesocket(clientSocket);
            return;
        }

        // Communication loop
        while (true) {
            ZeroMemory(buf, 4096);
            int bytesReceived = recv(clientSocket, buf, 4096, 0);
            if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) {
                // Handle client disconnection (client closed connection or error occurred)
                {
                    std::lock_guard<std::mutex> guard(clients_mutex);
                    auto it = clientNames.find(clientSocket);
                    if (it != clientNames.end()) {
                        clientName = it->second;

                        std::cout << "Client '" << clientName << "' disconnected." << std::endl;

                        // Remove the client from the list and map
                        clients.erase(std::remove(clients.begin(), clients.end(), clientSocket), clients.end());
                        clientNames.erase(it);
                    } else {
                        std::cerr << "Client socket not found in map, possibly already removed." << std::endl;
                    }
                }

                // Broadcast that the client has left the chat (OUTSIDE mutex lock)
                std::string leaveMessage = clientName + " has left the chat.";
                broadcastMessage(leaveMessage, clientSocket);  // Call this outside the mutex lock

                closesocket(clientSocket);
                break;
            }

            // Get the client's name and construct the message
            std::string message = clientName + ": " + std::string(buf, 0, bytesReceived);
            std::cout << "Received: " << message << std::endl;

            // Broadcast the message to other clients (OUTSIDE mutex lock)
            broadcastMessage(message, clientSocket);  // Call this outside the mutex lock
        }

    } catch (const std::exception& ex) {
        std::cerr << "Exception occurred in client handler: " << ex.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception occurred in client handler." << std::endl;
    }

    closesocket(clientSocket);  // Ensure the socket is always closed
}

int main() {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock." << std::endl;
        return 1;
    }

    // Create a listening socket
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed. Error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Bind the socket to an IP and port
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;  // Listen on any IP address
    serverAddr.sin_port = htons(54000);       // Port number

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed. Error: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // Listen for incoming connections
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed. Error: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server is listening on port 54000..." << std::endl;

    // Accept multiple clients
    while (true) {
        sockaddr_in clientAddr;
        int clientSize = sizeof(clientAddr);

        SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientSize);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed. Error: " << WSAGetLastError() << std::endl;
            continue;
        }

        // Lock the clients vector and add the new client
        {
            std::lock_guard<std::mutex> guard(clients_mutex);
            clients.push_back(clientSocket);
        }

        // Create a new thread for each connected client
        std::thread clientThread(handleClient, clientSocket);
        clientThread.detach();  // Detach so that the main thread doesn't have to wait for it to finish
    }

    // Cleanup
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
