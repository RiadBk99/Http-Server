TCP NON BLOCKING SERVER.

Sockets programming, written in C language.
Supports receiving get and delete requests from self.
Supports send get http request to an online server called "httpbin.org", saving copy of body received in a .txt file.

**IMPORTANT**
Must include Ws2_32.lib and winsock32.lib

**REVIEW**
The server currently operates as a blocking server, accepting all incoming connections at the same time BUT dealing with each request once at a time.
To fix this i'm going to add flags to the socket state struct in order to identify send socket states as client or server state.

**EXTRAS**
Make the server send actual files, test for a pdf file, considering to turn the pdf file into bits in the system.
Combine the files server with the Handle Files proccess(MyBusiness repository).
Make it online, approachable via password or specific ip address or online self authentication.
