// server/Main.cpp
#include "Server.hpp"
#include <iostream>

int main() {
    try {
        Server server(8080);
        server.Start();
    } catch (const std::exception& e) {
        std::cerr << "Критическая ошибка сервера: " << e.what() << std::endl;
    }
    return 0;
}
