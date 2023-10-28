/* Passed the exam on 2023-10-26 with this version 
If you want to use a constant instead of hard-coding the value 120,000 everywhere, 
you will need to change the initialization of 'clientBuf' by removing the '= {}' 
After that, add the 'clientBufReset' function call to set it to 0.
*/

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

// Added libraries
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h> // for the isSocketReadable function

// VARIABLES
// Because it's an exam, go easy on yourself and use global variables.

// Variables copied from the main function
int sockfd; // Server socket
struct sockaddr_in servaddr; // Used for the bind and accept functions

int max_fd; // For faster loop instead of using FD_SETSIZE

// ID variables
int ids[FD_SETSIZE]; // Store client IDs by client FD. FD_SETSIZE is the max usable FD for the select function
int idCount = 0; // To assign a unique incremental ID to clients starting at 0

// For the serverLoop select function
fd_set current_fds, read_fds;

// For the isStillReadable function
fd_set temp_fds;
struct timeval timeout;

// Custom error handling
void errorNExit(char* message){
    write(2, message, strlen(message));
    exit(1);
}

// Custom memset to avoid -Werror problem
void clientBufReset(char clientBuf[120000]){
    for (int i = 0; i < 120000; i++){
        clientBuf[i] = '\0';
    }
}

void broadcastToOthers(int clientFd, char* message){
    for (int fd = 0; fd < max_fd + 1; fd++){
        if (fd != clientFd) // To avoid sending to the author of the message
            send(fd, message, strlen(message), MSG_NOSIGNAL); // In the Linux version, see the comment below

        /*
        Because there is no way to be sure a client is still connected while using send at this point
        and because we are not allowed to use signal handling or sockopt, there is no way that we can
        completely avoid an error trying to send on a disconnected socket, resulting in the program stopping with
        a SIGPIPE signal (test 9 issue).
        To avoid the interruption of the program, you need to use a flag in the send function as the fourth argument to ignore this SIGPIPE signal.
        On Mac, use SO_NOSIGPIPE.
        On Linux, use MSG_NOSIGNAL.
        */
    }
}

// For broadcasting the connection/disconnection message to other clients
void broadcastConnexion(int clientFd, int connect1_Disconnect2){
    char connBuf[100] = {}; // Initialized the array with '\0'
    if (connect1_Disconnect2 == 1)
        sprintf(connBuf, "server: client %d just arrived\n", ids[clientFd]); // Connecting
    else
        sprintf(connBuf, "server: client %d just left\n", ids[clientFd]); // Disconnecting
    broadcastToOthers(clientFd, connBuf);
}

// Function returns true if the FD can be read
bool isSocketReadable(int fd){
    FD_ZERO(&temp_fds);
    FD_SET(fd, &temp_fds);
    select(fd + 1, &temp_fds, NULL, NULL, &timeout); // Non-blocking select (timeout set to zero in the main function)
    if (FD_ISSET(fd, &temp_fds))
        return true;
    return false;
}

void sendingMessage(int clientFd, int recvLen, char clientBuf[120000]){
    char introBuf[100] = {};
    sprintf(introBuf, "client %d: ", ids[clientFd]);
    broadcastToOthers(clientFd, introBuf);
    
    /* I'm using a one-by-one char broadcast method to catch in real-time the \n to broadcast the introBuf.
    I'm not sure if this is tested (I've seen a lot of code without it), but it's asked by the subject. */
    char sendBuf[2] = {}; // I'm using a 2-char array to always have a \0 at the end.

    // Because the loop is based on recvLen, if the client disconnects when reading from the socket, the loop will end because recvLen will be 0.
    for (int i = 0; i < recvLen; i++){
        sendBuf[0] = clientBuf[i]; // Only changing the first char
        broadcastToOthers(clientFd, sendBuf);

        // If there is a need to broadcast the intro on a new line
        if (i + 1 < recvLen && sendBuf[0] == '\n'){
            // If this is not the last char of clientBuf and the char is a \n
            broadcastToOthers(clientFd, introBuf);
        }

        if (i + 1 == recvLen && recvLen == 120000){
            // If clientBuf is full (it's optional: it's to avoid using isSocketReadable for nothing)
            if (isSocketReadable(clientFd)){
                clientBufReset(clientBuf); // If it's forgotten, the remaining text will be written again (no \0)
                recvLen = recv(clientFd, clientBuf, 120000, 0); // Copy-paste from checkSocket
                i = -1; // This restarts the next loop to 0
            }
        }
    }

    /* Probably optional and not tested (I've never seen that anywhere).
    If a user sends a message not finishing with a \n, you should add one.
    This cannot happen naturally (e.g., using nc in a terminal), but it can happen with a tester. */
    if (sendBuf[0] != '\n'){
        broadcastToOthers(clientFd, "\n"); // You're sending a string, not just a char
    }
}

/* Keep this part from the main function to help you remember how to use accept
len = sizeof(cli);
connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
*/

// All actions needed to accept a client
void acceptClient(){
    socklen_t addrlen = sizeof(servaddr); // Needed for accept (man accept)
    int clientFd = accept(sockfd, (struct sockaddr*)&servaddr, &addrlen); // Use the example from the main function
    // Check if accept worked (no need for more verification like if clientFd is higher than FD_SETSIZE)
    if (clientFd > 0){
        if (clientFd > max_fd){
            // Condition to be sure to include the new FD in the for loops
            max_fd = clientFd;
        }
        ids[clientFd] = idCount++; // Change the index at clientFd for the idCount value and increment it
        FD_SET(clientFd, &current_fds); // Add clientFd to current_fds
        broadcastConnexion(clientFd, 1);
    }
}

// Three actions to disconnect a client
void disconnectClient(int clientFd){
    broadcastConnexion(clientFd, 2); // Option 2 to disconnect
    FD_CLR(clientFd, &current_fds);
    close(clientFd);
}

// Verification made on the return value of the recv function to know if it's a message or a disconnection
void checkSocket(int clientFd){
    char clientBuf[120000] = {}; // For client messages
    int recvLen = recv(clientFd, clientBuf, 120000, 0);
    if (recvLen > 0)
        sendingMessage(clientFd, recvLen, clientBuf);
    else
        disconnectClient(clientFd);
}

void serverLoop(){
    while (1){
        read_fds = current_fds;
        /*
        - Because select can never return -1 in that case, not sure this condition is needed, but I passed with it.
        - Don't forget the + 1.
        */
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0){
            continue;
        }

        // Check for all FDs up to max_fd (inclusive).
        for (int fd = 0; fd < max_fd + 1; fd++){
            // If the FD IS NOT set in read_fds, continue to the next FD.
            if (!(FD_ISSET(fd, &read_fds))){
                continue;
            }

            if (fd == sockfd){
                acceptClient();
            } else {
                checkSocket(fd);
            }
        }
    }
}

int main(int argc, char** argv) {
    // Argument condition
    if (argc != 2)
        errorNExit("Wrong number of arguments\n"); // Copy-paste from the subject, don't forget the \n.

    // Socket creation and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
        errorNExit("Fatal error\n");

    bzero(&servaddr, sizeof(servaddr));
    // Assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(2130706433); // 127.0.0.1
    servaddr.sin_port = htons(atoi(argv[1])); // No need to parse the port.

    // Binding the newly created socket to the given IP and verification
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
        errorNExit("Fatal error\n");

    if (listen(sockfd, 9999999) != 0){
        // listen will truncate to the SOMAXCONN value (the highest backlog possible)
        errorNExit("Fatal error\n");
    }

    // Regrouping global variables initialization
    FD_ZERO(&current_fds);
    FD_SET(sockfd, &current_fds);
    max_fd = sockfd;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    serverLoop();

    return (0); // Added but never reached
}
