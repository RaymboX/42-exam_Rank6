#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

int sockfd;
struct sockaddr_in servaddr;
int max_fd;
int ids[FD_SETSIZE];
int idCount = 0;
fd_set current_fds, read_fds, temp_fds;
struct timeval timeout;

void errorNExit(char* message){
	write(2, message, strlen(message));
	exit(1);
}

void clientBufReset(char clientBuf[120000]){
    for (int i = 0; i < 120000; i++){
        clientBuf[i] = '\0';
    }
}

void broadcastToOthers(int clientFd, char* message){
	for (int fd = 0; fd < max_fd + 1; fd++){
		if (fd != clientFd)
			send(fd, message, strlen(message), MSG_NOSIGNAL); //Linux version Mac: SO_NOSIGPIPE
	}
}

void broadcastConnexion(int clientFd, int connect1_Disconnect2){
	char connBuf[100] = {};
	if (connect1_Disconnect2 == 1)
		sprintf(connBuf, "server: client %d just arrived\n", ids[clientFd]);
	else
		sprintf(connBuf, "server: client %d just left\n", ids[clientFd]);
	broadcastToOthers(clientFd, connBuf);
}

bool isSocketReadable(int fd){
	FD_ZERO(&temp_fds);
	FD_SET(fd, &temp_fds);
	select(fd + 1, &temp_fds, NULL, NULL, &timeout);
	if (FD_ISSET(fd, &temp_fds))
		return true;
	return false;
}

void sendingMessage(int clientFd, int recvLen, char clientBuf[120000]){
	char introBuf[100] = {};
	sprintf(introBuf, "client %d: ", ids[clientFd]);
	broadcastToOthers(clientFd, introBuf);
	char sendBuf[2] = {};
	for (int i = 0; i < recvLen; i++){
		sendBuf[0] = clientBuf[i];
		broadcastToOthers(clientFd, sendBuf);
		if (i + 1 < recvLen 
				&& sendBuf[0] == '\n')
			broadcastToOthers(clientFd, introBuf);
		if (i + 1 == recvLen
				&& recvLen == 120000
				&& isSocketReadable(clientFd)){
			clientBufReset(clientBuf);
			recvLen = recv(clientFd, clientBuf, 120000, 0);
			i = -1;
		}
	}
	if (sendBuf[0] != '\n')
		broadcastToOthers(clientFd, "\n");
}

void acceptClient(){
	socklen_t addrlen = sizeof(servaddr);
	int clientFd = accept(sockfd, (struct sockaddr*)&servaddr, &addrlen);
	if (clientFd > 0){
		if (clientFd > max_fd)
			max_fd = clientFd;
		ids[clientFd] = idCount++;
		FD_SET(clientFd, &current_fds);
		broadcastConnexion(clientFd, 1);
	}
}

void disconnectClient(int clientFd){
	broadcastConnexion(clientFd, 2);
	FD_CLR(clientFd, &current_fds);
	close(clientFd);
}

void checkSocket(int clientFd){
	char clientBuf[120000] = {};
	int recvLen = recv(clientFd, clientBuf, 120000, 0);
	if (recvLen > 0)
		sendingMessage(clientFd, recvLen, clientBuf);
	else
		disconnectClient(clientFd);
}

void serverLoop(){
	while (1){
		read_fds = current_fds;
		if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) 
			continue;
		for (int fd = 0; fd < max_fd + 1; fd++){
			if (!(FD_ISSET(fd, &read_fds)))
				continue;
			if (fd == sockfd)
				acceptClient();
			else
				checkSocket(fd);
		}
	}
}

int main(int argc, char** argv) {
	if (argc != 2)
		errorNExit("Wrong number of arguments\n");
	
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1)
		errorNExit("Fatal error\n");

	bzero(&servaddr, sizeof(servaddr)); 
	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(argv[1]));
  
	// Binding newly created socket to given IP and verification 
	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
		errorNExit("Fatal error\n");
	
	if (listen(sockfd, SOMAXCONN) != 0)
		errorNExit("Fatal error\n");

    FD_ZERO(&current_fds);
	FD_SET(sockfd, &current_fds);
	max_fd = sockfd;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	serverLoop();

	return (0);
}