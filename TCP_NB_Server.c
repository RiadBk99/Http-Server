#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <stdio.h>
// Don't forget to include "Ws2_32.lib" in the library list.
#include <winsock2.h>
#include <string.h>
#include <time.h>

#define MAX_SOCKETS 60
#define LISTEN 1
#define RECEIVE 2
#define SEND 4
#define SEND_TIME 1
#define SEND_SECONDS 2
#define EMPTY  0
#define IDLE  3
#define TIME_PORT  27015


struct SocketState {
    SOCKET id;            // Socket handle
    int recv;             // Receiving?
    int send;             // Sending?
    int sendSubType;      // Sending sub-type
    char buffer[128];
    int len;
};

int addSocket(SOCKET id, int what);
void removeSocket(int index);
void acceptConnection(int index);
void receiveMessage(int index);
void sendMessage(int index);
struct sockaddr_in serverService;
struct SocketState sockets[MAX_SOCKETS] = { 0 };
int socketsCount = 0;

int main() {
    // Initialize Winsock (Windows Sockets).

	// Create a WSADATA object called wsaData.
	// The WSADATA structure contains information about the Windows
	// Sockets implementation.
    WSADATA wsaData;

    if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData)) {
        printf("Time Server: Error at WSAStartup()\n");
        return -1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (INVALID_SOCKET == listenSocket) {
        printf("Time Server: Error at socket(): %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    serverService.sin_family = AF_INET;
    serverService.sin_addr.s_addr = INADDR_ANY;
    serverService.sin_port = htons(TIME_PORT);

    if (SOCKET_ERROR == bind(listenSocket, (SOCKADDR*)&serverService, sizeof(serverService))) {
        printf("Time Server: Error at bind(): %d\n", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return -1;
    }

    if (SOCKET_ERROR == listen(listenSocket, 5)) {
        printf("Time Server: Error at listen(): %d\n", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return -1;
    }
    addSocket(listenSocket, LISTEN);
    printf("Time Server: Wait for clients' requests.\n");

    while (1) {
        // The select function determines the status of one or more sockets,
		// waiting if necessary, to perform asynchronous I/O. Use fd_sets for
		// sets of handles for reading, writing and exceptions. select gets "timeout" for waiting
		// and still performing other operations (Use NULL for blocking). Finally,
		// select returns the number of descriptors which are ready for use (use FD_ISSET
		// macro to check which descriptor in each set is ready to be used).

		// initialize recv socket set
        fd_set waitRecv;
        FD_ZERO(&waitRecv);
        // fill set with available listen and receive sockets
        for (int i = 0; i < MAX_SOCKETS; i++) {
            if ((sockets[i].recv == LISTEN) || (sockets[i].recv == RECEIVE))
                FD_SET(sockets[i].id, &waitRecv);
        }

        // initialize recv socket set
        fd_set waitSend;
        FD_ZERO(&waitSend);
        // fill set with available send sockets
        for (int i = 0; i < MAX_SOCKETS; i++) {
            if (sockets[i].send == SEND)
                FD_SET(sockets[i].id, &waitSend);
        }

        //
		// Wait for interesting event.
		// Note: First argument is ignored. The fourth is for exceptions.
		// And as written above the last is a timeout, hence we are blocked if nothing happens.
		//

		// get the number of ready sockets
        int nfd = select(0, &waitRecv, &waitSend, NULL, NULL);
        if (nfd == SOCKET_ERROR) {
            printf("Time Server: Error at select(): %d\n", WSAGetLastError());
            WSACleanup();
            return -1;
        }

        // look for all ready recv sockets
        // accept connection or data from each socket
        for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++) {
            if (FD_ISSET(sockets[i].id, &waitRecv)) {
                nfd--;
                switch (sockets[i].recv) {
                case LISTEN:
                    acceptConnection(i);
                    break;
                case RECEIVE:
                    receiveMessage(i);
                    break;
                }
            }
        }

        // look for all ready send sockets
        // send data
        for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++) {
            if (FD_ISSET(sockets[i].id, &waitSend)) {
                nfd--;
                if (sockets[i].send == SEND) {
                    sendMessage(i);
                }
            }
        }
    }

    printf("Time Server: Closing Connection.\n");
    closesocket(listenSocket);
    WSACleanup();
    return 0;
}

// add new socket to server sockets array
int addSocket(SOCKET id, int what) {
    // go through the sockets array
    for (int i = 0; i < MAX_SOCKETS; i++) {
        // check if recv field of a socket is empty
        if (sockets[i].recv == EMPTY) {
            // index is available for a new socket
            sockets[i].id = id;
            sockets[i].recv = what;
            sockets[i].send = IDLE;
            sockets[i].len = 0;
            socketsCount++;
            return 1;
        }
    }
    return 0;
}

void removeSocket(int index) {
    sockets[index].recv = EMPTY;
    sockets[index].send = EMPTY;
    socketsCount--;
}

void acceptConnection(int index) {
    SOCKET id = sockets[index].id;
    struct sockaddr_in from;    // Address of sending partner
    int fromLen = sizeof(from);

    SOCKET msgSocket = accept(id, (struct sockaddr*)&from, &fromLen);
    if (INVALID_SOCKET == msgSocket) {
        printf("Time Server: Error at accept(): %d\n", WSAGetLastError());
        return;
    }
    printf("Time Server: Client %s:%d is connected.\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));

	// Set the socket to be in non-blocking mode.
    unsigned long flag = 1;
    if (ioctlsocket(msgSocket, FIONBIO, &flag) != 0) {
        printf("Time Server: Error at ioctlsocket(): %d\n", WSAGetLastError());
    }

    if (!addSocket(msgSocket, RECEIVE)) {
        printf("\t\tToo many connections, dropped!\n");
        closesocket(id);
    }
}

void receiveMessage(int index) {
    SOCKET msgSocket = sockets[index].id;
    int len = sockets[index].len;
    int bytesRecv = recv(msgSocket, &sockets[index].buffer[len], sizeof(sockets[index].buffer) - len, 0);

    if (SOCKET_ERROR == bytesRecv) {
        printf("Time Server: Error at recv(): %d\n", WSAGetLastError());
        closesocket(msgSocket);
        removeSocket(index);
        return;
    }
    if (bytesRecv == 0) {
        closesocket(msgSocket);
        removeSocket(index);
        return;
    }

    sockets[index].buffer[len + bytesRecv] = '\0';
    printf("Time Server: Received: %d bytes of \"%s\" message.\n", bytesRecv, &sockets[index].buffer[len]);

    sockets[index].len += bytesRecv;

    // parse received message
    if (strncmp(sockets[index].buffer, "GET", 3) == 0)
        handleHttpGet();
    if (strncmp(sockets[index].buffer, "DELETE", 6) == 0)
        handleHttpDelete();
}

  //  if (sockets[index].len > 0) {
  //    printf("%s", sockets[index].buffer);
  //  }

    /*
    if (sockets[index].len > 0) {
        if (strncmp(sockets[index].buffer, "TimeString", 10) == 0) {
            sockets[index].send = SEND;
            sockets[index].sendSubType = SEND_TIME;
            memmove(sockets[index].buffer, &sockets[index].buffer[10], sockets[index].len - 10);
            sockets[index].len -= 10;
        } else if (strncmp(sockets[index].buffer, "SecondsSince1970", 16) == 0) {
            sockets[index].send = SEND;
            sockets[index].sendSubType = SEND_SECONDS;
            memmove(sockets[index].buffer, &sockets[index].buffer[16], sockets[index].len - 16);
            sockets[index].len -= 16;
        } else if (strncmp(sockets[index].buffer, "Exit", 4) == 0) {
            printf("closing connection with socket at index %d\n",index);
            closesocket(msgSocket);
            removeSocket(index);
        }
    }*/
//}

void sendMessage(int index) {
    int bytesSent;
    char sendBuff[255];

    SOCKET msgSocket = sockets[index].id;
    if (sockets[index].sendSubType == SEND_TIME) {
        time_t timer;
        time(&timer);
        strcpy(sendBuff, ctime(&timer));
        sendBuff[strlen(sendBuff) - 1] = '\0';
    } else if (sockets[index].sendSubType == SEND_SECONDS) {
        time_t timer;
        time(&timer);
        sprintf(sendBuff, "%ld", timer);
    }

    bytesSent = send(msgSocket, sendBuff, (int)strlen(sendBuff), 0);
    if (SOCKET_ERROR == bytesSent) {
        printf("Time Server: Error at send(): %d\n", WSAGetLastError());
        return;
    }

    printf("Time Server: Sent: %d\\%lu bytes of \"%s\" message.\n", bytesSent, strlen(sendBuff), sendBuff);

    sockets[index].send = IDLE;
}
