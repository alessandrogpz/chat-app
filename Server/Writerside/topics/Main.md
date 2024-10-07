# Main
---

## **Overall Architecture**

This program is a **multi-client chat server** that:
1. **Listens for incoming client connections** using a TCP/IP socket.
2. **Handles each client in a separate thread** to allow multiple clients to communicate simultaneously.
3. **Broadcasts messages** from one client to all connected clients.
4. **Handles client disconnections** gracefully by notifying other clients when someone leaves the chat.

The key architectural elements are:
- **Sockets**: Used to establish communication between the server and clients.
- **Threads**: Each client is handled in a separate thread to enable simultaneous communication.
- **Mutex** (`std::mutex`): Used to ensure that shared resources like the list of clients (`clients`) and the map of client names (`clientNames`) are accessed safely from multiple threads.
- **Message Broadcasting**: Messages from one client are sent to all other connected clients via a broadcasting mechanism.

---

### **Code Breakdown**

### 1. **Including Necessary Headers and Libraries**

```
#include <iostream>     // For input/output operations (e.g., std::cout, std::cerr)
#include <thread>       // For creating and managing threads
#include <string>       // For working with std::string
#include <vector>       // To store connected client sockets
#include <map>          // To map client sockets to client names
#include <mutex>        // To control access to shared resources (like clients and clientNames)
#include <winsock2.h>   // Main header for Windows socket programming
#include <ws2tcpip.h>   // Additional socket functionality (e.g., address conversion)

#pragma comment(lib, "ws2_32.lib")  // Linking the Winsock library needed for network operations
```

- **<iostream>**: Provides basic input and output functionality such as `std::cout` and `std::cerr` to print messages to the console.
- **<thread>**: We use the **thread** library to handle multiple clients by creating a new thread for each client connection.
- **<string>**: Provides the **std::string** class for string manipulation.
- **<vector>**: The **std::vector** class is used to store a dynamic list of active client sockets.
- **<map>**: We use **std::map** to associate each client socket (a unique identifier for the connection) with a client’s name (as a string).
- **<mutex>**: **std::mutex** is used to prevent race conditions when multiple threads access shared data (e.g., adding/removing clients).
- **<winsock2.h>** and **<ws2tcpip.h>**: These headers provide the necessary functions and data structures for **Winsock**, the Windows API for network programming. Winsock is required to create and manage sockets, enabling network communication between the server and clients.
- **#pragma comment(lib, "ws2_32.lib")**: This tells the compiler to link with the **ws2_32.lib** library, which is required to use Winsock functions.

---

### 2. **Global Variables**

```
std::vector<SOCKET> clients;  // Stores active client sockets
std::map<SOCKET, std::string> clientNames;  // Map to store client names associated with each socket
std::mutex clients_mutex;  // Mutex to ensure thread-safe access to shared data
```

- **clients**: A vector that holds the **SOCKET** descriptors (a type used in Windows for network connections) of all connected clients. This allows the server to keep track of which clients are currently connected.
- **clientNames**: A map that associates each **SOCKET** with the corresponding client's name. This lets the server know who sent a message, which is necessary for broadcasting messages with the correct sender information.
- **clients_mutex**: A mutex that protects access to the `clients` and `clientNames` data structures. Since multiple threads (one for each client) may modify these shared resources concurrently, the mutex prevents race conditions by ensuring only one thread accesses or modifies the data at a time.

---

### 3. **The `broadcastMessage` Function**

```
void broadcastMessage(const std::string& message, SOCKET sender) {
    std::lock_guard<std::mutex> guard(clients_mutex);  // Automatically locks and unlocks the mutex

    for (SOCKET client : clients) {  // Loop through all connected clients
        if (client != sender) {  // Don't send the message back to the sender
            int result = send(client, message.c_str(), message.size() + 1, 0);  // Send message to each client
            if (result == SOCKET_ERROR) {
                std::cerr << "Failed to send message to a client. Error: " << WSAGetLastError() << std::endl;
            }
        }
    }
}
```

#### Function Explanation:

- **Purpose**: This function is responsible for broadcasting a message from one client to all other connected clients.
- **Parameters**:
    - `message`: The message to be broadcasted (e.g., `"Alice: Hello"`).
    - `sender`: The socket of the client who sent the message (we don’t want to send the message back to this client).

#### Detailed Breakdown:
- **std::lock_guard<std::mutex> guard(clients_mutex)**: This locks the `clients_mutex` during the function execution to ensure thread-safe access to the `clients` vector. The mutex is automatically released when the function exits or an exception is thrown.
- **for (SOCKET client : clients)**: Iterates through all connected clients.
- **if (client != sender)**: We check if the client is the same as the sender to avoid echoing the message back to the sender.
- **send()**: Sends the message to each client over their respective socket.
- **Error Handling**: If the message fails to send (due to a socket error, such as a disconnected client), we log the error using `WSAGetLastError()`.

---

### 4. **The `handleClient` Function**

```
void handleClient(SOCKET clientSocket) {
    char buf[4096];  // Buffer to store messages
    std::string clientName;  // Variable to store the client's name

    try {
        // Receive the client's name
        ZeroMemory(buf, 4096);  // Zero out the buffer to clear old data
        int bytesReceived = recv(clientSocket, buf, 4096, 0);  // Receive data from the client
        if (bytesReceived > 0) {
            clientName = std::string(buf, 0, bytesReceived);  // Convert the received bytes to a string (client name)

            // Store the client name in the map
            {
                std::lock_guard<std::mutex> guard(clients_mutex);  // Lock the mutex before modifying shared data
                clientNames[clientSocket] = clientName;  // Associate the socket with the client's name
            }

            std::cout << "Client '" << clientName << "' connected." << std::endl;

            // Broadcast to other clients that a new user has joined (OUTSIDE mutex lock)
            std::string joinMessage = clientName + " has joined the chat.";
            broadcastMessage(joinMessage, clientSocket);  // Call this outside the mutex lock
        } else {
            std::cerr << "Error receiving client name. Closing connection." << std::endl;
            closesocket(clientSocket);  // Close the connection if the name couldn't be received
            return;  // Exit the function since we can't proceed without the client's name
        }

        // Communication loop
        while (true) {
            ZeroMemory(buf, 4096);  // Clear the buffer before receiving the next message
            int bytesReceived = recv(clientSocket, buf, 4096, 0);  // Receive the next message
            if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) {
                // Handle client disconnection or error
                {
                    std::lock_guard<std::mutex> guard(clients_mutex);  // Lock mutex before modifying shared data
                    auto it = clientNames.find(clientSocket);  // Find the client's name in the map
                    if (it != clientNames.end()) {
                        clientName = it->second;  // Get the client's name

                        std::cout << "Client '" << clientName << "' disconnected." << std::endl;

                        // Remove the client from the list and map
                        clients.erase(std::remove(clients.begin(), clients.end(), clientSocket), clients.end());
                        clientNames.erase(it);  // Remove the client's entry from the map
                    } else {
                        std::cerr << "Client socket not found in map, possibly already removed." << std::endl;
                    }
                }

                // Broadcast that the client has left the chat
                std::string leaveMessage = clientName + " has left the chat.";
                broadcastMessage(leaveMessage, clientSocket);  // Call this outside the mutex lock

                closesocket(clientSocket);  // Close the socket
                break;  // Exit the communication loop
            }

            // Get the client's name and construct the message
            std::string message = clientName + ": " + std::string(buf, 0, bytesReceived);  // Format the message
            std::cout << "Received

: " << message << std::endl;

            // Broadcast the message to other clients
            broadcastMessage(message, clientSocket);  // Call this outside the mutex lock
        }

    } catch (const std::exception& ex) {
        std::cerr << "Exception occurred in client handler: " << ex.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception occurred in client handler." << std::endl;
    }

    closesocket(clientSocket);  // Ensure the socket is always closed, even in case of an exception
}
```

#### Detailed Breakdown:
- **`buf[4096]`**: A buffer to store the incoming messages. The size of 4096 bytes is arbitrary and can be adjusted based on expected message size.
- **`std::string clientName`**: A string to store the client's name. We receive the name first so we can identify the client in future messages.

#### Client Name Reception:
- **recv()**: This function reads data sent from the client. The first message from each client is expected to be their name.
- **ZeroMemory(buf, 4096)**: Clears the buffer to ensure no leftover data from previous messages.
- **clientName = std::string(buf, 0, bytesReceived)**: Converts the received bytes into a string (representing the client's name).

#### Communication Loop:
- **recv()**: Inside the loop, the server waits to receive messages from the client. The loop continues until the client sends no more data (disconnection) or an error occurs.
- **Broadcasting Messages**: Once a message is received, it is broadcast to all other clients using the `broadcastMessage()` function.

#### Disconnection Handling:
- **if (bytesReceived == SOCKET_ERROR || bytesReceived == 0)**: This checks if the client has disconnected (`bytesReceived == 0`) or if there’s a network error (`SOCKET_ERROR`). When this happens, the client is removed from the `clients` vector and `clientNames` map.
- **broadcastMessage(leaveMessage, clientSocket)**: Notifies other clients that someone has left the chat.

---

### 5. **The `main()` Function**

```
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
```

#### Functionality:
- **WSAStartup()**: Initializes Winsock, which is required to use the Windows socket API. We request version 2.2 of the API with `MAKEWORD(2, 2)`.
- **socket()**: Creates a new socket for the server using `AF_INET` (IPv4), `SOCK_STREAM` (TCP), and `0` for the protocol.
- **bind()**: Binds the server socket to an IP address (`INADDR_ANY` to bind to all available interfaces) and port (54000).
- **listen()**: Puts the server socket into listening mode, allowing it to accept incoming connections.
- **accept()**: Waits for a client to connect. Once a connection is made, a new socket is created for communication with the client.
- **Thread Management**: For each client that connects, a new thread is created to handle the client’s communication. The thread is detached, meaning it runs independently of the main thread.

---

### **How It All Fits Together**

1. **Server Setup**: The server initializes Winsock, creates a listening socket, and binds it to a specific port (54000). It then enters a loop to accept incoming client connections.
2. **Multi-Client Handling**: When a client connects, a new thread is created to handle communication with that client. The main thread continues to listen for other clients.
3. **Communication**: Each client thread receives messages from its respective client and broadcasts those messages to all other connected clients.
4. **Thread Safety**: Shared resources (`clients`, `clientNames`) are protected using a mutex to prevent race conditions.
5. **Client Disconnection**: When a client disconnects, the server removes them from the list of active clients and notifies the other clients of the disconnection.

---

### **Summary**

This server architecture is a scalable and efficient way to handle a multi-client chat system:
- **Sockets** provide the communication mechanism between the server and the clients.
- **Threads** enable the server to handle multiple clients concurrently.
- **Mutexes** ensure that shared data (like the list of clients and client names) are accessed in a thread-safe manner.
- **Broadcasting** allows all clients to receive messages from each other, making the chat experience seamless.

By following this architecture, you can build chat systems that handle real-time communication for multiple users efficiently and securely!