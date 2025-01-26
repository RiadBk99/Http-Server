#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <stdio.h>
// Don't forget to include "Ws2_32.lib" in the library list.
#include <winsock2.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <dirent.h>

#define MAX_SOCKETS 60
#define EMPTY  0
#define LISTEN 1
#define RECEIVE 2
#define IDLE  3
#define SEND 4

#define SEND_TIME 1
#define SEND_SECONDS 2

#define SEND_FILE 200
#define SEND_NOT_SUPPORTED 400
#define SEND_FILE_NOT_FOUND 404
#define SEND_DIRECTORY_NOT_FOUND 500

#define TIME_PORT  27015
#define INITIAL_BUFFER_SIZE 1024


struct SocketState {
    SOCKET id;            // Socket handle
    int recv;             // Receiving?
    int send;             // Sending?
    int sendSubType;      // Sending sub-type
 //   int msgType;          // plain text or html
    char *buffer;
    int len;
};

int addSocket(SOCKET id, int what);
void removeSocket(int index);
void acceptConnection(int index);
void receiveMessage(int index);
void sendMessage(int index);
char *readFile(char *filename);
void expandBuffer(int index);
int handleHttpGet(int index);
int handleHttpDelete(int index);
char *fetchFile(char* name, int index);
void wrapSendBuffInHTML(char **sendbuff);
boolean checkForAnError(int bytesResult, char* ErrorAt, SOCKET socket_1, SOCKET socket_2);
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

		// initialize Recv socket set
        fd_set waitRecv;
        FD_ZERO(&waitRecv);
        // fill set with available listen and receive sockets
        for (int i = 0; i < MAX_SOCKETS; i++) {
            if ((sockets[i].recv == LISTEN) || (sockets[i].recv == RECEIVE))
                FD_SET(sockets[i].id, &waitRecv);
        }

        // initialize Send socket set
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
            // allocate initial memory
            sockets[i].buffer = (char *)malloc(INITIAL_BUFFER_SIZE);
            if (sockets[i].buffer == NULL) {
                perror("Failed to allocate memory for buffer");
                exit(EXIT_FAILURE);
            }
            sockets[i].len = 0;
            socketsCount++;
            return 1;
        }
    }
    return 0;
}

// terminate socket
void removeSocket(int index) {
    sockets[index].recv = EMPTY;
    sockets[index].send = EMPTY;
    socketsCount--;
}
// accept new client connection
void acceptConnection(int index) {
    SOCKET id = sockets[index].id;
    struct sockaddr_in from;    // Address of sending partner
    int fromLen = sizeof(from);

    SOCKET msgSocket = accept(id, (struct sockaddr*)&from, &fromLen);
    if (INVALID_SOCKET == msgSocket) {
        printf("Files Server: Error at accept(): %d\n", WSAGetLastError());
        return;
    }
    printf("Files Server: Client %s:%d is connected.\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));

	// Set the socket to be in non-blocking mode.
    unsigned long flag = 1;
    if (ioctlsocket(msgSocket, FIONBIO, &flag) != 0) {
        printf("Files Server: Error at ioctlsocket(): %d\n", WSAGetLastError());
    }

    if (!addSocket(msgSocket, RECEIVE)) {
        printf("\t\tToo many connections, dropped!\n");
        closesocket(id);
    }
}

void receiveMessage(int index) {

    SOCKET msgSocket = sockets[index].id;
    int bytesRecv;
    int firstRun = 0; // 0 for true
    int len = sockets[index].len;

    // make sure there is enough space in the buffer
    int remainingSize = INITIAL_BUFFER_SIZE - len;
    while (1) {
        if (remainingSize <= 0) {
            // expand the buffer if it's full
            expandBuffer(index);
            remainingSize = INITIAL_BUFFER_SIZE;
                }
        // save socket data into buffer
        bytesRecv = recv(msgSocket, &sockets[index].buffer[sockets[index].len], remainingSize, 0);
        // connection error, terminate the socket.
        if (bytesRecv == SOCKET_ERROR && firstRun == 0) {
            printf("Time Server: Error at recv(): %d\n", WSAGetLastError());
            closesocket(msgSocket);
            removeSocket(index);
            return;
                }
        if (bytesRecv == 0 && firstRun == 0) {
            printf("Client disconnected.\n");
            closesocket(msgSocket);
            removeSocket(index);
            return;
                }

        if(bytesRecv>0){

            // update flag
            firstRun = 1;

            // update socket buffer length
            sockets[index].len += bytesRecv;

            // partial receive means end of data
            if (bytesRecv <= remainingSize) {
                sockets[index].buffer[sockets[index].len] = '\0';
                printf("Files Server: Received: %d bytes of \"%s\" message.\n", bytesRecv, &sockets[index].buffer[len]);
                break;
                }
            // update remaining size
            remainingSize = remainingSize - bytesRecv;
            }
        }


    // parse received message
    // check if its a GET or DELETE request
    if (strncmp(sockets[index].buffer, "GET", 3) == 0){
            int parse = handleHttpGet(index);

            // set type of message to be sent
            if(parse == SEND_FILE)
                    sockets[index].sendSubType = SEND_FILE;
            else if(parse == SEND_FILE_NOT_FOUND)
                    sockets[index].sendSubType = SEND_FILE_NOT_FOUND;
            else if(parse == SEND_DIRECTORY_NOT_FOUND)
                    sockets[index].sendSubType = SEND_DIRECTORY_NOT_FOUND;
            else if (parse == SEND_NOT_SUPPORTED)
                    sockets[index].sendSubType = SEND_NOT_SUPPORTED;

            // set socket to be a Send Socket
            sockets[index].recv = IDLE;
            sockets[index].send = SEND;
    }
    else if(strncmp(sockets[index].buffer, "DELETE", 6) == 0)
            printf("reached delete");
            //  handleHttpDelete(index);
            // NOT FINISHED!!!!!!!!!!!!!!!!!!!!
            // DO ALMOST SAME AS THE GET

    else{
        // set socket to be  a Send Socket
        sockets[index].recv = IDLE;
        sockets[index].send = SEND;
        // set type of message to be sent
        sockets[index].sendSubType = SEND_NOT_SUPPORTED;
        }

}

int handleHttpGet(int index){

    char* requestLine = strtok(sockets[index].buffer, "\r\n"); // extract the request line

    // deal with unsupported requests
    if (requestLine == NULL)
        return SEND_NOT_SUPPORTED;

    // parse the GET request
    char* method = strtok(requestLine, " ");      // extract the HTTP method
    char* path = strtok(NULL, " ");               // extract the path

    // deal with unsupported requests
    if (method == NULL || path == NULL)
        return SEND_NOT_SUPPORTED;

    // remove leading slash from path
    if (path[0] == '/')
        path++;
    printf("\nFetching file name : %s\n", path);

    char *result = fetchFile(path, index);
    if (strcmp(result, "Directory Not Found") == 0) {
        return SEND_DIRECTORY_NOT_FOUND;

        } else if (strcmp(result, "File Not Found") == 0) {
            return SEND_FILE_NOT_FOUND;

            } else if (strcmp(result,"Send File") == 0)
                return SEND_FILE;

}

char *fetchFile(char *name, int index){

    DIR *folder;
    struct dirent *entry;
    folder = opendir("Files");
    int files = 0;

    if(folder == NULL)
    {
        return "Directory Not Found";
    }

    while((entry=readdir(folder)) )
    {
        files++;
        char *fileName = entry->d_name;
        if(strcmp(name, entry->d_name)==0){

            // build the file path
            size_t filePathSize = strlen("Files/") + strlen(fileName) + 1;
            char *filePath = (char *)malloc(filePathSize);
            snprintf(filePath, filePathSize, "Files/%s", fileName);

            //read file content
            free(sockets[index].buffer);
            sockets[index].buffer = readFile(filePath);
            sockets[index].len = strlen(sockets[index].buffer);

            // free memory & close folder
            free(filePath);
            filePath = NULL;
            closedir(folder);

            return "Send File";
        }
    }

    closedir(folder); //cClose the directory
    return "File Not Found";

}


void sendMessage(int index) {
    int bytesSent;
    SOCKET msgSocket = sockets[index].id;
    const char *htmlStart = "<html><body>";
    const char *htmlEnd = "</body></html>";

    expandBuffer(index);

    // look for relevant socket type and send message
    if (sockets[index].sendSubType == SEND_FILE) {
        printf("SENDING FILE\n");
        int contentLength = strlen(htmlStart) + sockets[index].len + strlen(htmlEnd);
        snprintf(sockets[index].buffer, sizeof(sockets[index].buffer),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/html\r\n"
                 "Content-Length: %d\r\n"
                 "Connection: keep-alive\r\n\r\n%s%s%s",
                 contentLength, htmlStart, sockets[index].buffer ,htmlEnd);
                 bytesSent = send(msgSocket, sockets[index].buffer, strlen(sockets[index].buffer), 0);

    }else if (sockets[index].sendSubType == SEND_NOT_SUPPORTED) {
        printf("SENDING NOT SUPPORTED\n");
            int contentLength = strlen("Invalid HTTP Request");
            snprintf(sockets[index].buffer, sizeof(sockets[index].buffer),
                     "HTTP/1.1 400 Bad Request\r\n"
                     "Content-Type: text/html\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: keep-alive\r\n\r\n"
                     "%sInvalid HTTP Request%s",
                     contentLength,htmlStart,htmlEnd);
                    bytesSent = send(msgSocket, sockets[index].buffer, strlen(sockets[index].buffer), 0);
    } else if (sockets[index].sendSubType == SEND_FILE_NOT_FOUND) {
        printf("SENDING FILE NOT FOUND\n");
        int contentLength = strlen("File not found");
        snprintf(sockets[index].buffer, sizeof(sockets[index].buffer),
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/html\r\n"
                "Content-Length: %d\r\n"
                "Connection: keep-alive\r\n\r\n"
                "%sFile not found%s",
                contentLength,htmlStart,htmlEnd);
                bytesSent = send(msgSocket, sockets[index].buffer, strlen(sockets[index].buffer), 0);
    }else if (sockets[index].sendSubType == SEND_DIRECTORY_NOT_FOUND) {
        printf("SENDING INTERNAL ERROR\n");
        int contentLength = strlen("Error reading file");
        snprintf(sockets[index].buffer, sizeof(sockets[index].buffer),
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Type: text/html\r\n"
                "Content-Length: %d\r\n"
                "Connection: keep-alive\r\n\r\n"
                "%sError reading file%s",
                contentLength,htmlStart,htmlEnd);
                bytesSent = send(msgSocket, sockets[index].buffer, strlen(sockets[index].buffer), 0);
    }
    if (SOCKET_ERROR == bytesSent) {
        printf("Files Server: Error at send(): %d\n", WSAGetLastError());
        return;
    }

    printf("Files Server: Sent: %d\\%d bytes of \"%s\" message.\n", bytesSent, strlen(sockets[index].buffer), sockets[index].buffer);

    // update socket mode to Recv
    sockets[index].send = IDLE;
    sockets[index].recv = RECEIVE;
}

// read file contents
char *readFile(char *filename) {
     FILE *f = fopen(filename, "rt");
     assert(f);
     fseek(f, 0, SEEK_END);
     long length = ftell(f);
     fseek(f, 0, SEEK_SET);
     char *buffer = (char *) malloc(length + 1);
     buffer[length] = '\0';
     fread(buffer, 1, length, f);
     fclose(f);
     return buffer;
}


// expand socket buffer
void expandBuffer(int index) {

    int newSize = INITIAL_BUFFER_SIZE + sockets[index].len; // get new required size
    char *newBuffer = (char *)realloc(sockets[index].buffer, newSize);  // allocate memory
    // check for errors
    if (newBuffer == NULL) {
        perror("Failed to expand buffer");
        free(sockets[index].buffer);
        sockets[index].buffer = NULL;
        exit(EXIT_FAILURE);
    }
    // set new pointer
    sockets[index].buffer = newBuffer;
    printf("Buffer expanded to %d bytes.\n", newSize);
    printf("%s", newBuffer);
}


