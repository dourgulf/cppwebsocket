//
//  main.cpp
//  cppwebsocket
//
//  Created by dawenhing on 27/07/2017.
//  Copyright © 2017 dawenhing. All rights reserved.
//

#include <iostream>
#include <string>
#include "WebSocketClient.hpp"

std::string getlineWithReturn() {
    std::string str;
    char ch;
    static const size_t maxCommandLength = 64;
    while ((ch = std::cin.get()) != '\n' && str.length() < maxCommandLength) {
        str.push_back(ch);
    }
    
    return str;
}


int main(int argc, const char * argv[]) {
    auto ws = std::unique_ptr<cppws::WebSocketClient>(new cppws::WebSocketClient({"ws://127.0.0.1:12345/chat"}));
    ws->onOpen = [] {
        std::cout << ":open" << std::endl;
    };
    ws->onClosed = [] {
        std::cout << ":closed" << std::endl;
    };
    ws->onMessage = [&ws](const std::string& message) {
        std::cout << ":" << message << std::endl;
    };
    ws->open();
    std::thread finish([&ws]{
        while(true) {
            std::string str = getlineWithReturn();
            if (str != "stop") {
                ws->sendMessage(str);
            }
        }
    });
    finish.join();
    
    return 0;
}
