//
//  WebSocketClient.hpp
//  cppwebsocket
//
//  Created by dawenhing on 15/03/2017.
//  Copyright © 2017 dawenhing. All rights reserved.
//

#ifndef WebSocketClient_hpp
#define WebSocketClient_hpp

#include "SocketUtils.hpp"

#include <string>
#include <thread>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace cppws {
    enum ReadyStateValues: int {
        INIT,
        CLOSING, 
        CLOSED, 
    };

    // http://tools.ietf.org/html/rfc6455#section-5.2  Base Framing Protocol
    //
    //  0                   1                   2                   3
    //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // +-+-+-+-+-------+-+-------------+-------------------------------+
    // |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
    // |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
    // |N|V|V|V|       |S|             |   (if payload len==126/127)   |
    // | |1|2|3|       |K|             |                               |
    // +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
    // |     Extended payload length continued, if payload len == 127  |
    // + - - - - - - - - - - - - - - - +-------------------------------+
    // |                               |Masking-key, if MASK set to 1  |
    // +-------------------------------+-------------------------------+
    // | Masking-key (continued)       |          Payload Data         |
    // +-------------------------------- - - - - - - - - - - - - - - - +
    // :                     Payload Data continued ...                :
    // + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
    // |                     Payload Data continued ...                |
    // +---------------------------------------------------------------+
    struct WebSocketHeader {
        unsigned headerSize;
        bool fin;
        bool mask;
        enum OpcodeType {
            CONTINUATION = 0x0,
            TEXT_FRAME = 0x1,
            BINARY_FRAME = 0x2,
            CLOSE = 8,
            PING = 9,
            PONG = 0xa,
        } opcode;
        int N0;
        uint64_t N;
        uint8_t maskingKey[4];
    };
    
    class WebSocketClient {
    public:
        WebSocketClient(const std::vector<std::string> &strUrls, bool useMask=true);
        ~WebSocketClient();
        
        void useMask(bool mask);
        void open();
        void close();
        void closeInmediatly();
        void sendMessage(const std::string &message);
        void sendBinary(const std::string &message);
        void sendBinary(const std::vector<uint8_t> &message);
        void sendPing();
        void sendClose();
        
    public:
        // call back interface
        std::function<void ()> onOpen;
        std::function<void (const std::string &msg)> onMessage;
        std::function<void ()> onClosed;
        
    private:
        std::string nextServiceAddress();
        
        void runPollInThread();
        
    private:
        void sendData(WebSocketHeader::OpcodeType type, uint64_t message_size, std::function<void()> appendPlayload);
        bool extractReceivedMessage(std::string &receivedMessage);
        
    private:
        std::vector<std::string> serviceUrls_;
        
        std::thread serviceThread_;
                
        ReadyStateValues readyState_;
        bool useMask_;
        
        std::vector<uint8_t> recvBuff_;
        std::vector<uint8_t> sendBuff_;
        std::recursive_mutex sendMutex_;
    };    
}

#endif /* JCWsClient_hpp */
