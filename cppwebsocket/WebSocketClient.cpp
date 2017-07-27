//
//  WebSocketClient.cpp
//  cppwebsocket
//
//  Created by dawenhing on 15/03/2017.
//  Copyright © 2017 dawenhing. All rights reserved.
//

#include "WebSocketClient.hpp"
#include <iostream>

namespace cppws {
    
    WebSocketClient::WebSocketClient(const std::vector<std::string> &strUrls, bool useMask) {
        useMask_ = useMask;
        readyState_ = INIT;
        // 反向插入，获取的时候也是从后往前
        serviceUrls_.assign(strUrls.rbegin(), strUrls.rend());
    }
    
    WebSocketClient::~WebSocketClient() {
        closeInmediatly();
    }
    
    void WebSocketClient::useMask(bool mask) {
        useMask_ = mask;
    }
    
    void WebSocketClient::open() {
        if (readyState_ != INIT) {
            closeInmediatly();
        }
        readyState_ = INIT;
        serviceThread_ = std::thread([this]{
            runPollInThread();
        });
    }
    
    std::string WebSocketClient::nextServiceAddress() {
        if (serviceUrls_.size() > 0) {
            std::string address = serviceUrls_.back();
            serviceUrls_.pop_back();
            serviceUrls_.insert(serviceUrls_.begin(), address);
            return address;
        }
        return std::string();
    }
    
    void WebSocketClient::close() {
        sendClose();
        if (serviceThread_.joinable()) {
            serviceThread_.join();
        }
    }
    
    void WebSocketClient::closeInmediatly() {
        std::lock_guard<std::recursive_mutex> lock(sendMutex_);
        // clean the pending messsage, make close quickly
        sendBuff_.clear();
        close();
    }
    
    void WebSocketClient::runPollInThread() {
        const static int timeout = 100;
        const static int rbuffsize = 1500;
        const static int maxPendingSendSize = 1024;
        
        socket_t sockfd = OpenWebSocketURL(nextServiceAddress(), "");
        if (sockfd != INVALID_SOCKET) {
            if (onOpen) {
                onOpen();
            }
            
            std::string fullMessage;
            while (readyState_ != CLOSED) {
                // select 
                if (timeout != 0) {
                    fd_set rfds;
                    fd_set wfds;
                    timeval tv = { timeout/1000, (timeout%1000) * 1000 };
                    FD_ZERO(&rfds);
                    FD_ZERO(&wfds);
                    FD_SET(sockfd, &rfds);
                    if (sendBuff_.size()) { FD_SET(sockfd, &wfds); }
                    select(sockfd + 1, &rfds, &wfds, 0, timeout > 0 ? &tv : 0);
                }
                
                // receive data
                while (true) {
                    size_t N = recvBuff_.size();
                    ssize_t ret;
                    recvBuff_.resize(N + rbuffsize);
                    ret = recv(sockfd, (char*)&recvBuff_[0] + N, rbuffsize, 0);
                    if (ret < 0 && (socketerrno == SOCKET_EWOULDBLOCK || socketerrno == SOCKET_EAGAIN_EINPROGRESS)) {
                        recvBuff_.resize(N);
                        break;
                    }
                    else if (ret <= 0) {
                        recvBuff_.resize(N);
                        closesocket(sockfd);
                        readyState_ = CLOSED;
                        std::cerr << (ret < 0 ? "Connection error!" : "Connection closed!") << std::endl;
                        break;
                    }
                    else {
                        recvBuff_.resize(N + ret);
                        
                        // dispatch received message
                        while(extractReceivedMessage(fullMessage)) {
                            if (onMessage) {
                                onMessage(fullMessage);
                            }
                            fullMessage.clear();
                            std::string().swap(fullMessage);  // free memory
                        }                        
                    }
                    // check if pending send buffer is too large
                    std::lock_guard<std::recursive_mutex> lock(sendMutex_);
                    if (sendBuff_.size() > maxPendingSendSize) {
                        break;
                    }
                }
                                
                // send all pending messages
                std::lock_guard<std::recursive_mutex> lock(sendMutex_);
                while (sendBuff_.size() && readyState_ != CLOSED) {
                    ssize_t ret = ::send(sockfd, (char*)&sendBuff_[0], sendBuff_.size(), 0);
                    if (ret < 0 && (socketerrno == SOCKET_EWOULDBLOCK || socketerrno == SOCKET_EAGAIN_EINPROGRESS)) {
                        break;
                    }
                    else if (ret <= 0) {
                        closesocket(sockfd);
                        readyState_ = CLOSED;
                        std::cerr << (ret < 0 ? "Connection error!" : "Connection closed!") << std::endl;
                        break;
                    }
                    else {
                        sendBuff_.erase(sendBuff_.begin(), sendBuff_.begin() + ret);
                    }
                }
                
                // handle closing case
                if (sendBuff_.size() == 0 && readyState_ == CLOSING) {
                    closesocket(sockfd);
                    readyState_ = CLOSED;
                }
            }
            closesocket(sockfd);
        }        
        readyState_ = CLOSED;
        if (onClosed) {
            onClosed();
        }
    }
    
    bool WebSocketClient::extractReceivedMessage(std::string &fullMessage) {
        while (true) {
            WebSocketHeader ws;
            if (recvBuff_.size() < 2) { 
                return false; /* Need at least 2 */ 
            }
            const uint8_t * data = (uint8_t *) &recvBuff_[0]; // peek, but don't consume
            ws.fin = (data[0] & 0x80) == 0x80;
            ws.opcode = (WebSocketHeader::OpcodeType) (data[0] & 0x0f);
            ws.mask = (data[1] & 0x80) == 0x80;
            ws.N0 = (data[1] & 0x7f);
            ws.headerSize = 2 + (ws.N0 == 126? 2 : 0) + (ws.N0 == 127? 8 : 0) + (ws.mask? 4 : 0);
            if (recvBuff_.size() < ws.headerSize) { 
                return false; /* Need: ws.headerSize - recvBuff_.size() */ 
            }
            int i = 0;
            if (ws.N0 < 126) {
                ws.N = ws.N0;
                i = 2;
            }
            else if (ws.N0 == 126) {
                ws.N = 0;
                ws.N |= ((uint64_t) data[2]) << 8;
                ws.N |= ((uint64_t) data[3]) << 0;
                i = 4;
            }
            else if (ws.N0 == 127) {
                ws.N = 0;
                ws.N |= ((uint64_t) data[2]) << 56;
                ws.N |= ((uint64_t) data[3]) << 48;
                ws.N |= ((uint64_t) data[4]) << 40;
                ws.N |= ((uint64_t) data[5]) << 32;
                ws.N |= ((uint64_t) data[6]) << 24;
                ws.N |= ((uint64_t) data[7]) << 16;
                ws.N |= ((uint64_t) data[8]) << 8;
                ws.N |= ((uint64_t) data[9]) << 0;
                i = 10;
            }

            if (recvBuff_.size() < ws.headerSize+ws.N) { 
                return false; /* Need: ws.headerSize+ws.N - recvBuff_.size() */ 
            }
            
            if (ws.mask) {
                ws.maskingKey[0] = ((uint8_t) data[i+0]) << 0;
                ws.maskingKey[1] = ((uint8_t) data[i+1]) << 0;
                ws.maskingKey[2] = ((uint8_t) data[i+2]) << 0;
                ws.maskingKey[3] = ((uint8_t) data[i+3]) << 0;
            }
            else {
                ws.maskingKey[0] = 0;
                ws.maskingKey[1] = 0;
                ws.maskingKey[2] = 0;
                ws.maskingKey[3] = 0;
            }
            
            // We got a whole message, now do something with it:
            if (
                ws.opcode == WebSocketHeader::TEXT_FRAME 
                || ws.opcode == WebSocketHeader::BINARY_FRAME
                || ws.opcode == WebSocketHeader::CONTINUATION
                ) {
                
                if (ws.mask) { 
                    for (size_t i = 0; i != ws.N; ++i) { 
                        recvBuff_[i+ws.headerSize] ^= ws.maskingKey[i&0x3]; 
                    } 
                }
                
                fullMessage.insert(fullMessage.end(), recvBuff_.begin()+ws.headerSize, recvBuff_.begin()+ws.headerSize+(size_t)ws.N);// just feed
                if (ws.fin) {
                    recvBuff_.erase(recvBuff_.begin(), recvBuff_.begin() + ws.headerSize+(size_t)ws.N);                    
                    return true;
                }
            }
            else if (ws.opcode == WebSocketHeader::PING) {
                if (ws.mask) { 
                    for (size_t i = 0; i != ws.N; ++i) {
                        recvBuff_[i+ws.headerSize] ^= ws.maskingKey[i&0x3]; 
                    } 
                }
                sendData(WebSocketHeader::PONG, (size_t)ws.N, [this, &ws]{
                    sendBuff_.insert(sendBuff_.end(), recvBuff_.begin()+ws.headerSize, recvBuff_.begin()+ws.headerSize+(size_t)ws.N);
                });
            }
            else if (ws.opcode == WebSocketHeader::PONG) { 
            }
            else if (ws.opcode == WebSocketHeader::CLOSE) { 
                sendClose(); 
            }
            else { 
                std::cerr << "ERROR: Got unexpected WebSocket message." << std::endl; 
                sendClose(); 
            }
            
            recvBuff_.erase(recvBuff_.begin(), recvBuff_.begin() + ws.headerSize+(size_t)ws.N);
            return false;
        }
    }
    
    void WebSocketClient::sendMessage(const std::string &message) {
        sendData(WebSocketHeader::TEXT_FRAME, message.size(), [this, &message]{
            sendBuff_.insert(sendBuff_.end(), message.begin(), message.end());
        });
    }
    
    void WebSocketClient::sendBinary(const std::string &message) {
        sendData(WebSocketHeader::BINARY_FRAME, message.size(), [this, &message]{
            sendBuff_.insert(sendBuff_.end(), message.begin(), message.end());
        });
    }
    
    void WebSocketClient::sendBinary(const std::vector<uint8_t> &message) {
        sendData(WebSocketHeader::BINARY_FRAME, message.size(), [this, &message]{
            sendBuff_.insert(sendBuff_.end(), message.begin(), message.end());
        });
    }
    
    void WebSocketClient::sendPing() {
        std::string empty;
        sendData(WebSocketHeader::PING, empty.size(), nullptr);
    }
    
    void WebSocketClient::sendClose() {
        if(readyState_ == CLOSING || readyState_ == CLOSED) { 
            return; 
        }
        readyState_ = CLOSING;
        
        std::string close;
        sendData(WebSocketHeader::CLOSE, close.size(), nullptr);
    }
    
    void WebSocketClient::sendData(WebSocketHeader::OpcodeType type, uint64_t messageSize, std::function<void()> appendPlayload) {
        // TODO:
        // Masking key should (must) be derived from a high quality random
        // number generator, to mitigate attacks on non-WebSocket friendly
        // middleware:
        const uint8_t maskingKey[4] = { 0x12, 0x34, 0x56, 0x78 };        
        std::vector<uint8_t> header;
        header.assign(2 + (messageSize >= 126 ? 2 : 0) + (messageSize >= 65536 ? 6 : 0) + (useMask_ ? 4 : 0), 0);
        header[0] = 0x80 | type;
        if (messageSize < 126) {
            header[1] = (messageSize & 0xff) | (useMask_ ? 0x80 : 0);
            if (useMask_) {
                header[2] = maskingKey[0];
                header[3] = maskingKey[1];
                header[4] = maskingKey[2];
                header[5] = maskingKey[3];
            }
        }
        else if (messageSize < 65536) {
            header[1] = 126 | (useMask_ ? 0x80 : 0);
            header[2] = (messageSize >> 8) & 0xff;
            header[3] = (messageSize >> 0) & 0xff;
            if (useMask_) {
                header[4] = maskingKey[0];
                header[5] = maskingKey[1];
                header[6] = maskingKey[2];
                header[7] = maskingKey[3];
            }
        }
        else { // TODO: run coverage testing here
            header[1] = 127 | (useMask_ ? 0x80 : 0);
            header[2] = (messageSize >> 56) & 0xff;
            header[3] = (messageSize >> 48) & 0xff;
            header[4] = (messageSize >> 40) & 0xff;
            header[5] = (messageSize >> 32) & 0xff;
            header[6] = (messageSize >> 24) & 0xff;
            header[7] = (messageSize >> 16) & 0xff;
            header[8] = (messageSize >>  8) & 0xff;
            header[9] = (messageSize >>  0) & 0xff;
            if (useMask_) {
                header[10] = maskingKey[0];
                header[11] = maskingKey[1];
                header[12] = maskingKey[2];
                header[13] = maskingKey[3];
            }
        }
        
        std::lock_guard<std::recursive_mutex> lock(sendMutex_);
        // N.B. - txbuf will keep growing until it can be transmitted over the socket:
        sendBuff_.insert(sendBuff_.end(), header.begin(), header.end());
        
        if (appendPlayload) {
            appendPlayload();
        }
        if (useMask_) {
            for (size_t i = 0; i != messageSize; ++i) { *(sendBuff_.end() - messageSize + i) ^= maskingKey[i&0x3]; }
        }
        // TODO: maybe define a overflow control;
    }    
}
