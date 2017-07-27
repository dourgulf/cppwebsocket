#include "SocketUtils.hpp"

socket_t hostnameConnect(const std::string& hostname, int port) {
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *p;
    int ret;
    socket_t sockfd = INVALID_SOCKET;
    char sport[16];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(sport, 16, "%d", port);
    if ((ret = getaddrinfo(hostname.c_str(), sport, &hints, &result)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return 1;
    }
    for(p = result; p != NULL; p = p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == INVALID_SOCKET) { continue; }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) != SOCKET_ERROR) {
            break;
        }
        closesocket(sockfd);
        sockfd = INVALID_SOCKET;
    }
    freeaddrinfo(result);
    return sockfd;
}

socket_t OpenWebSocketURL(const std::string& url, const std::string& origin)
{
    char host[128];
    int port;
    char path[128];
    if (url.size() >= 128) {
        fprintf(stderr, "ERROR: url size limit exceeded: %s\n", url.c_str());
        return INVALID_SOCKET;
    }
    if (origin.size() >= 200) {
        fprintf(stderr, "ERROR: origin size limit exceeded: %s\n", origin.c_str());
        return INVALID_SOCKET;
    }
    if (false) { }
    else if (sscanf(url.c_str(), "ws://%[^:/]:%d/%s", host, &port, path) == 3) {
    }
    else if (sscanf(url.c_str(), "ws://%[^:/]/%s", host, path) == 2) {
        port = 80;
    }
    else if (sscanf(url.c_str(), "ws://%[^:/]:%d", host, &port) == 2) {
        path[0] = '\0';
    }
    else if (sscanf(url.c_str(), "ws://%[^:/]", host) == 1) {
        port = 80;
        path[0] = '\0';
    }
    else {
        fprintf(stderr, "ERROR: Could not parse WebSocket url: %s\n", url.c_str());
        return INVALID_SOCKET;
    }
    socket_t sockfd = hostnameConnect(host, port);
    if (sockfd == INVALID_SOCKET) {
        fprintf(stderr, "Unable to connect to %s:%d\n", host, port);
        return INVALID_SOCKET;
    }
    {
        char line[256];
        snprintf(line, sizeof(line)-1, "GET /%s HTTP/1.1\r\n", path); 
        ::send(sockfd, line, strlen(line), 0);
        
        if (port == 80) {
            snprintf(line, sizeof(line)-1, "Host: %s\r\n", host); 
            ::send(sockfd, line, strlen(line), 0);
        }
        else {
            snprintf(line, sizeof(line)-1, "Host: %s:%d\r\n", host, port); 
            ::send(sockfd, line, strlen(line), 0);
        }
        snprintf(line, sizeof(line)-1, "Upgrade: websocket\r\n"); 
        ::send(sockfd, line, strlen(line), 0);
        
        snprintf(line, sizeof(line)-1, "Connection: Upgrade\r\n"); 
        ::send(sockfd, line, strlen(line), 0);
        
        if (!origin.empty()) {
            snprintf(line, sizeof(line)-1, "Origin: %s\r\n", origin.c_str()); 
            ::send(sockfd, line, strlen(line), 0);
        }
        snprintf(line, sizeof(line)-1, "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"); 
        ::send(sockfd, line, strlen(line), 0);
        
        snprintf(line, sizeof(line)-1, "Sec-WebSocket-Version: 13\r\n"); 
        ::send(sockfd, line, strlen(line), 0);
        
        snprintf(line, sizeof(line)-1, "\r\n"); 
        ::send(sockfd, line, strlen(line), 0);
        
        int i;
        for (i = 0; i < 2 || (i < 255 && line[i-2] != '\r' && line[i-1] != '\n'); ++i) { 
            if (recv(sockfd, line+i, 1, 0) == 0) {
                closesocket(sockfd);
                return INVALID_SOCKET; 
            } 
        }
        line[i] = 0;
        
        if (i == 255) { 
            fprintf(stderr, "ERROR: Got invalid status line connecting to: %s\n", url.c_str());
            closesocket(sockfd);
            return INVALID_SOCKET; 
        }
        int status;
        if (sscanf(line, "HTTP/1.1 %d", &status) != 1 || status != 101) {
            fprintf(stderr, "ERROR: Got bad status connecting to %s: %s", url.c_str(), line); 
            closesocket(sockfd);
            return INVALID_SOCKET; 
        }
        // TODO: verify response headers,
        while (true) {
            for (i = 0; i < 2 || (i < 255 && line[i-2] != '\r' && line[i-1] != '\n'); ++i) { 
                if (recv(sockfd, line+i, 1, 0) == 0) {
                    closesocket(sockfd);
                    return INVALID_SOCKET; 
                } 
            }
            if (line[0] == '\r' && line[1] == '\n') { 
                break; 
            }
        }
    }
    
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*) &flag, sizeof(flag)); // Disable Nagle's algorithm
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(sockfd, FIONBIO, &on);
#else
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
#endif
    fprintf(stderr, "Connected to: %s\n", url.c_str());
    return sockfd;
}
