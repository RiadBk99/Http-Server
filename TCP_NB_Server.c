#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <stdio.h>
// Don't forget to include "Ws2_32.lib" in the library list.
#include <winsock2.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <dirent.h>
#include <time.h>
#include <ws2tcpip.h>

#define MAX_SOCKETS 60
#define EMPTY  0
#define LISTEN 1
#define RECEIVE 2
#define IDLE  3
#define SEND 4

#define SEND_TIME 1
#define SEND_SECONDS 2

#define SEND_FILE 200
#define SEND_REMOVED 201
#define SEND_NOT_SUPPORTED 400
#define SEND_FILE_NOT_FOUND 404
#define SEND_INTERNAL_ERROR 500

#define JSON 1
#define ANYTHING 2

#define TIME_PORT  27015
#define INITIAL_BUFFER_SIZE 1024


struct SocketState {
    SOCKET id;            // Socket handle
    int recv;             // Receiving?
    int send;             // Sending?
    int sendSubType;      // Sending sub-type
    char *buffer;
    int len;
};

int addSocket(SOCKET id, int what);
void removeSocket(int index);
void acceptConnection(int index);
void receiveMessage(int index);
void sendMessage(int index);
char *readFile(char *filename);
int expandBuffer(int index);
int handleHttpGet(int index);
int handleHttpDelete(int index);
int askMainServer(char* name, int index);
int saveToFile(char *name, char *content);
char *fetchFile(char* name, int index);
char *deleteFile(char* name, int index);
void generateDateHeader(char* dateHeader, size_t bufferSize);
void wrapSendBuffInHTML(char **sendbuff);
boolean checkForAnError(int bytesResult, char* ErrorAt, SOCKET socket_1, SOCKET socket_2);
struct sockaddr_in serverService;
struct SocketState sockets[MAX_SOCKETS] = { 0 };
LARGE_INTEGER lastRecvTracker[MAX_SOCKETS] = {0};
LARGE_INTEGER frequency;
int socketsCount = 0;



int main() {
    // Initialize Winsock (Windows Sockets).

	// Create a WSADATA object called wsaData.
	// The WSADATA structure contains information about the Windows
	// Sockets implementation.
    WSADATA wsaData;

    if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData)) {
        printf("Files Server: Error at WSAStartup()\n");
        return -1;
    }
    if (!QueryPerformanceFrequency(&frequency)) {
    printf("High-resolution performance counter not supported.\n");
    exit(1);
    }
    struct timeval timeout;
    timeout.tv_sec = 40;  // Wait up to 5 seconds
    timeout.tv_usec = 0; // No microseconds

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (INVALID_SOCKET == listenSocket) {
        printf("Files Server: Error at socket(): %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    serverService.sin_family = AF_INET;
    serverService.sin_addr.s_addr = INADDR_ANY;
    serverService.sin_port = htons(TIME_PORT);

    if (SOCKET_ERROR == bind(listenSocket, (SOCKADDR*)&serverService, sizeof(serverService))) {
        printf("Files Server: Error at bind(): %d\n", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return -1;
    }

    if (SOCKET_ERROR == listen(listenSocket, 5)) {
        printf("Files Server: Error at listen(): %d\n", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return -1;
    }
    addSocket(listenSocket, LISTEN);
    printf("Files Server: Wait for clients' requests.\n");

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

        int nfd = select(0, &waitRecv, &waitSend, NULL, &timeout);
        if (nfd == SOCKET_ERROR) {
                printf("Files Server: Error at select(): %d\n", WSAGetLastError());
                WSACleanup();
                return -1;
            }
        if (nfd == 0) {
            // handle timeout: Close inactive RECEIVE sockets
                for (int i = 0; i < MAX_SOCKETS; i++) {
                    if (sockets[i].recv == RECEIVE) { // check if the socket is in RECEIVE state
                        printf("Closing inactive socket: %d\n", (int)sockets[i].id);
                        closesocket(sockets[i].id); // close the socket
                        removeSocket(i);           // remove the socket from the array
                        }
                }
            continue; // skip to next iteration of the loop
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

    printf("Files Server: Closing Connection.\n");
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
    printf("\nFiles Server: Client %s:%d is connected.\n\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));

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
    int len = sockets[index].len;

    // save socket data into buffer
    bytesRecv = recv(msgSocket, &sockets[index].buffer[sockets[index].len], INITIAL_BUFFER_SIZE, 0);

    // connection error, terminate the socket.
    if (bytesRecv == SOCKET_ERROR) {
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

    if(bytesRecv>0){
        // update socket buffer length
        sockets[index].len += bytesRecv;
        sockets[index].buffer[sockets[index].len] = '\0';
        QueryPerformanceCounter(&lastRecvTracker[index]);
        printf("Files Server: Received: %d bytes of %s \n****END OF RECEIVED REQUEST.****\n", bytesRecv, &sockets[index].buffer[len]);
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
            else if(parse == SEND_INTERNAL_ERROR)
                    sockets[index].sendSubType = SEND_INTERNAL_ERROR;
            else if (parse == SEND_NOT_SUPPORTED)
                    sockets[index].sendSubType = SEND_NOT_SUPPORTED;

            // set socket to be a Send Socket
            sockets[index].recv = IDLE;
            sockets[index].send = SEND;
    }
    else if(strncmp(sockets[index].buffer, "DELETE", 6) == 0){
            int parse = handleHttpDelete(index);

            // set type of message to be sent
            if(parse == SEND_REMOVED)
                    sockets[index].sendSubType = SEND_REMOVED;
            else if(parse == SEND_FILE_NOT_FOUND)
                    sockets[index].sendSubType = SEND_FILE_NOT_FOUND;
            else if(parse == SEND_INTERNAL_ERROR)
                    sockets[index].sendSubType = SEND_INTERNAL_ERROR;
            else if (parse == SEND_NOT_SUPPORTED)
                    sockets[index].sendSubType = SEND_NOT_SUPPORTED;

            // set socket to be a Send Socket
            sockets[index].recv = IDLE;
            sockets[index].send = SEND;
        }
    else{
        // set socket to be a Send Socket
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
    printf("\nFetching file named : %s\n", path);

    char *result = fetchFile(path, index);
    if (strcmp(result, "Internal Error") == 0) {
        return SEND_INTERNAL_ERROR;

        } else if (strcmp(result, "File Not Found") == 0) {
            return SEND_FILE_NOT_FOUND;

            } else if (strcmp(result,"Send File") == 0)
                return SEND_FILE;
                else return SEND_INTERNAL_ERROR;

}

int handleHttpDelete(int index){

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
    printf("Attempting to delete file named : %s\n", path);

    char *result = deleteFile(path, index);

    printf("%s\n", result);

    if (strcmp(result, "Directory Not Found") == 0) {
        return SEND_INTERNAL_ERROR;

        } else if (strcmp(result, "File Not Found") == 0) {
            return SEND_FILE_NOT_FOUND;

            } else if (strcmp(result,"File Removed") == 0)
                return SEND_REMOVED;
                else return SEND_INTERNAL_ERROR;

}


char *fetchFile(char *name, int index){

    DIR *folder;
    struct dirent *entry;
    folder = opendir("Files");
    int files = 0;

    if(folder == NULL)
    {
        return "Internal Error";
    }

    // make file name with .txt
    char tmpName[1028];
    if (strcmp(name, "json") == 0 || strcmp(name, "anything") == 0){
        snprintf(tmpName, sizeof(tmpName), "%s.txt", name);
    }

    while((entry=readdir(folder)) )
    {
        files++;
        char *fileName = entry->d_name;
        if(strcmp(name, entry->d_name)==0 || strcmp(tmpName, entry->d_name) == 0){

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
    closedir(folder); //close the directory

    // these are the available files at the main server
    if ((strcmp(name, "json") == 0) || (strcmp(name, "anything") == 0)){
        if(askMainServer(name, index)== SEND_INTERNAL_ERROR)
            return "Internal Error";
        return "Send File";
    }

    return "File Not Found";

}

char *deleteFile(char *name, int index){

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

            if (remove(filePath) == 0) {
                printf("File deleted successfully.\n");
            } else {
                perror("Error deleting file");
            }

            // free memory & close folder
            free(filePath);
            filePath = NULL;
            closedir(folder);

            return "File Removed";
        }
    }

    closedir(folder); //close the directory
    return "File Not Found";

}

int askMainServer(char *name, int index){

    LARGE_INTEGER start,end;
    double duration;
    SOCKET sockfd;
    struct addrinfo hints, *results;
    char *inputVal = "httpbin.org";
    int rv;

    printf("\nContacting Main Server...\n");

    // set up hints for getaddrinfo
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // use TCP

    // resolve domain name to IP
    if ((rv = getaddrinfo(inputVal, "80", &hints, &results)) != 0) {
        printf("getaddrinfo failed: %s\n", gai_strerrorA(rv));
        return SEND_INTERNAL_ERROR;
    }

    // create socket
    sockfd = socket(results->ai_family, results->ai_socktype, results->ai_protocol);
    if (sockfd == INVALID_SOCKET) {
        printf("Socket creation failed. Error Code: %d\n", WSAGetLastError());
        freeaddrinfo(results);
        return SEND_INTERNAL_ERROR;
    }

    // start timer
    QueryPerformanceCounter(&start);

    // connect to server
    if (connect(sockfd, results->ai_addr, (int)results->ai_addrlen) == SOCKET_ERROR) {
        printf("Connection failed. Error Code: %d\n", WSAGetLastError());
        closesocket(sockfd);
        freeaddrinfo(results);
        return SEND_INTERNAL_ERROR;
    }

    char httpRequest[256];

    snprintf(httpRequest, sizeof(httpRequest), "GET /%s HTTP/1.0\r\nHost: %s\r\n\r\n", name,inputVal);
    printf("\n****Sending request to main server : %s\n", httpRequest);
    if (send(sockfd, httpRequest, (int)strlen(httpRequest), 0) == SOCKET_ERROR) {
        printf("Send failed. Error Code: %d\n", WSAGetLastError());
        closesocket(sockfd);
        freeaddrinfo(results);
        return SEND_INTERNAL_ERROR; // internal failure
    }

    // receive response
    char buffer[INITIAL_BUFFER_SIZE];
    int bytesrecv;
    int headerEnd = 0;
    char *bodyStart = NULL; // pointer to the start of the body
    int totalBodyLength = 0; // tracks the total length of the body
    char *tmpBuffer = NULL; // temporary buffer to store the response body

    // loop to receive data
    while ((bytesrecv = recv(sockfd, buffer, INITIAL_BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytesrecv] = '\0'; // null-terminate the received chunk

        if (!headerEnd) {
            // check for the end of the headers (\r\n\r\n)
            bodyStart = strstr(buffer, "\r\n\r\n");
            if (bodyStart) {
                headerEnd = 1;
                bodyStart += 4; // move past the \r\n\r\n to the start of the body

                // allocate memory for the body buffer and copy the initial part of the body
                int bodyChunkLength = bytesrecv - (bodyStart - buffer);
                tmpBuffer = (char *)malloc(bodyChunkLength + 1);
                if (!tmpBuffer) {
                    perror("Failed to allocate memory for the response body");
                    closesocket(sockfd);
                    freeaddrinfo(results);
                    return SEND_INTERNAL_ERROR; // internal failure
                }
                strcpy(tmpBuffer, bodyStart);
                totalBodyLength = bodyChunkLength;
            }
        } else {
            // headers have already been processed; append the remaining body
            tmpBuffer = (char *)realloc(tmpBuffer, totalBodyLength + bytesrecv + 1);
            if (!tmpBuffer) {
                perror("Failed to reallocate memory for the response body");
                closesocket(sockfd);
                freeaddrinfo(results);
                return SEND_INTERNAL_ERROR ;
            }
            // append the new chunk to the body
            strncat(tmpBuffer, buffer, bytesrecv);
            totalBodyLength += bytesrecv;
        }
    }

    // null-terminate the entire body buffer
    if (tmpBuffer) {
        tmpBuffer[totalBodyLength] = '\0';
        printf("\n****BODY RECEIVED FROM MAIN SERVER : \n %s\n", tmpBuffer);
        // save copy of the file in local server database
        if(saveToFile(name, tmpBuffer) == SEND_INTERNAL_ERROR){
            free(sockets[index].buffer);
            closesocket(sockfd);
            freeaddrinfo(results);
            return SEND_INTERNAL_ERROR;
        }
        // store the body in sockets[index].buffer
        free(sockets[index].buffer);
        sockets[index].buffer = tmpBuffer;
        sockets[index].len = totalBodyLength;
    } else {
        printf("****NO BODY RECEIVED FROM MAIN SERVER****\n");
    }
    QueryPerformanceCounter(&end);
    duration = (double)(end.QuadPart - start.QuadPart) / frequency.QuadPart;
    printf("RTT to fetch data from main server(%.6f)\n\n", duration);

    // check for errors or connection closure
    if (bytesrecv < 0) {
        perror("Error receiving data\n");
        closesocket(sockfd);
        freeaddrinfo(results);
        return SEND_INTERNAL_ERROR;
    }

    closesocket(sockfd);
    freeaddrinfo(results);

    return SEND_FILE;
}

int saveToFile(char *name, char *content){

    // open the directory to ensure it exists
    DIR *folder = opendir("Files");
    if (folder == NULL) {
        perror("Error opening directory 'Files'");
        return SEND_INTERNAL_ERROR; // return error if directory doesn't exist
    }
    closedir(folder); // close the directory

    // build the full file path
    char filepath[1028];
    snprintf(filepath, sizeof(filepath), "Files/%s.txt", name);
    // open the file for writing
    FILE *file = fopen(filepath, "w");
    if (file == NULL) {
        perror("Error opening file");
        return SEND_INTERNAL_ERROR;
    }

    // write the content to the file
    fprintf(file, "%s\n", content);
    fclose(file); // close the file

    printf("\nAdded new file to the server: %s.txt\n", name);
    return SEND_FILE;
}


void sendMessage(int index) {

    int bytesSent;
    SOCKET msgSocket = sockets[index].id;
    const char *htmlStart = "<html><body>";
    const char *htmlEnd = "</body></html>";
    int newSize = expandBuffer(index);
    char *sendBuff = (char *) malloc(newSize);
    char dateHeader[100];
    generateDateHeader(dateHeader, sizeof(dateHeader));
    LARGE_INTEGER performanceCountEnd;
    double duration;


    // look for relevant socket type and send message
    if (sockets[index].sendSubType == SEND_FILE) {

        printf("Sending requested file contents...\n");
        int contentLength = strlen(htmlStart) + sockets[index].len + strlen(htmlEnd);

        // prepare message buffer
        snprintf(sendBuff, newSize,
                 "HTTP/1.1 200 OK\r\n"
                 "%s\n"
                 "Content-Type: text/html\r\n"
                 "Content-Length: %d\r\n"
                 "Connection: keep-alive\r\n\r\n"
                 "%s%s%s",
                 dateHeader,contentLength, htmlStart, sockets[index].buffer ,htmlEnd);

        // free previously allocated memory
        free(sockets[index].buffer);
        // get pointer to relevant memory
        sockets[index].buffer = sendBuff;
        // send message
        bytesSent = send(msgSocket, sockets[index].buffer, strlen(sockets[index].buffer), 0);

    }if (sockets[index].sendSubType == SEND_REMOVED) {

        const char *body = "File Removed";
        strcpy(sockets[index].buffer, body);

        printf("Sending response...\n");
        int contentLength = strlen(htmlStart) + sockets[index].len + strlen(htmlEnd);

        // prepare message buffer
        snprintf(sendBuff, newSize,
                 "HTTP/1.1 200 OK\r\n"
                 "%s\n"
                 "Content-Type: text/html\r\n"
                 "Content-Length: %d\r\n"
                 "Connection: keep-alive\r\n\r\n"
                 "%s%s%s",
                 dateHeader,contentLength, htmlStart, sockets[index].buffer ,htmlEnd);

        // free previously allocated memory
        free(sockets[index].buffer);
        // get pointer to relevant memory
        sockets[index].buffer = sendBuff;
        // send message
        bytesSent = send(msgSocket, sockets[index].buffer, strlen(sockets[index].buffer), 0);

    }else if (sockets[index].sendSubType == SEND_NOT_SUPPORTED) {

        const char *body = "Invalid HTTP Request";
        strcpy(sockets[index].buffer, body);

        printf("Sending response...\n");
        int contentLength = strlen(htmlStart) + sockets[index].len + strlen(htmlEnd);

        // prepare message buffer
        snprintf(sendBuff, newSize,
                 "HTTP/1.1 400 Bad Request\r\n"
                 "%s\n"
                 "Content-Type: text/html\r\n"
                 "Content-Length: %d\r\n"
                 "Connection: keep-alive\r\n\r\n"
                 "%s%s%s",
                 dateHeader,contentLength, htmlStart, sockets[index].buffer ,htmlEnd);

        // free previously allocated memory
        free(sockets[index].buffer);
        // get pointer to relevant memory
        sockets[index].buffer = sendBuff;
        // send message
        bytesSent = send(msgSocket, sockets[index].buffer, strlen(sockets[index].buffer), 0);

    }else if (sockets[index].sendSubType == SEND_FILE_NOT_FOUND) {

        const char *body = "File not found";
        strcpy(sockets[index].buffer, body);

        printf("Sending response...\n");
        int contentLength = strlen(htmlStart) + sockets[index].len + strlen(htmlEnd);

        // prepare message buffer
        snprintf(sendBuff, newSize,
                 "HTTP/1.1 404 Not Found\r\n"
                 "%s\n"
                 "Content-Type: text/html\r\n"
                 "Content-Length: %d\r\n"
                 "Connection: keep-alive\r\n\r\n"
                 "%s%s%s",
                 dateHeader,contentLength, htmlStart, sockets[index].buffer ,htmlEnd);

        // free previously allocated memory
        free(sockets[index].buffer);
        // get pointer to relevant memory
        sockets[index].buffer = sendBuff;
        // send message
        bytesSent = send(msgSocket, sockets[index].buffer, strlen(sockets[index].buffer), 0);

    }else if (sockets[index].sendSubType == SEND_INTERNAL_ERROR) {

        const char *body = "Error reading file";
        strcpy(sockets[index].buffer, body);

        printf("Sending response...\n");
        int contentLength = strlen(htmlStart) + sockets[index].len + strlen(htmlEnd);

        // prepare message buffer
        snprintf(sendBuff, newSize,
                 "HTTP/1.1 500 Internal Server Error\r\n"
                 "%s\n"
                 "Content-Type: text/html\r\n"
                 "Content-Length: %d\r\n"
                 "Connection: keep-alive\r\n\r\n"
                 "%s%s%s",
                 dateHeader,contentLength, htmlStart, sockets[index].buffer ,htmlEnd);

        // free previously allocated memory
        free(sockets[index].buffer);
        // get pointer to relevant memory
        sockets[index].buffer = sendBuff;
        // send message
        bytesSent = send(msgSocket, sockets[index].buffer, strlen(sockets[index].buffer), 0);
    }

    if (SOCKET_ERROR == bytesSent) {
        sockets[index].buffer = (char *)malloc(INITIAL_BUFFER_SIZE);
        sockets[index].len = 0;
        printf("Files Server: Error at send(): %d\n", WSAGetLastError());
        closesocket(sockets[index].id);
        removeSocket(sockets[index].id);
        return;
    }



    printf("Files Server: Sent: %d\\%d bytes of %s \n****END OF SENT MESSAGE.****\n", bytesSent, (int)strlen(sockets[index].buffer), sockets[index].buffer);
    QueryPerformanceCounter(&performanceCountEnd);
    duration = (double)(performanceCountEnd.QuadPart - lastRecvTracker[index].QuadPart) / frequency.QuadPart;

    printf("RTT to process client request : (%.6f)\n\n", duration);

    // reset counter
    QueryPerformanceCounter(&lastRecvTracker[index]);


    // update socket mode to Recv
    sockets[index].send = IDLE;
    sockets[index].recv = RECEIVE;
    // reset buffer data
    sockets[index].buffer = (char *)malloc(INITIAL_BUFFER_SIZE);
    sockets[index].len = 0;
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


void generateDateHeader(char *buffer, size_t bufferSize) {
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now); // Get time in GMT

    // Format the time as per RFC 7231
    strftime(buffer, bufferSize, "Date: %a, %d %b %Y %H:%M:%S GMT", gmt);

}

// expand socket buffer
int expandBuffer(int index) {

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
    return newSize;
}


