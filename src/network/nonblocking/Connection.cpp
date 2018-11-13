#include "Connection.h"
#include <spdlog/logger.h>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>

#include "protocol/Parser.h"


#include <iostream>
#include <sys/socket.h>

namespace Afina {
namespace Network {
namespace NonBlocking {

// See Connection.h
void Connection::Start() {
    std::cout << "Start" << std::endl;
    readed_bytes = -1;
    _event.events = EPOLLIN|EPOLLRDHUP;
    _event.data.fd = _socket;
    _event.data.ptr = this;
    _is_running = true;
    }

// See Connection.h
void Connection::OnError() {
    std::cout << "OnError" << std::endl;
    shutdown(_socket, SHUT_RDWR);
    _is_running = false;
    }

// See Connection.h
void Connection::OnClose() {
    std::cout << "OnClose" << std::endl;
    shutdown(_socket, SHUT_RDWR);
    _is_running = false;



    }

// See Connection.h
void Connection::DoRead() {
    std::cout << "DoRead" << std::endl;
    std::string result;
    while (isAlive() && (readed_bytes = read(_socket, client_buffer, sizeof(client_buffer))) > 0) {
        //logger->debug("Got {} bytes from socket", readed_bytes);
        while (readed_bytes > 0 && isAlive()) {
            //_logger->debug("Process {} bytes", readed_bytes);
            // There is no command yet
            if (!command_to_execute) {
                std::size_t parsed = 0;
                if (parser.Parse(client_buffer, readed_bytes, parsed)) {
                    //_logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                    command_to_execute = parser.Build(arg_remains);
                    if (arg_remains > 0) {
                        arg_remains += 2;
                    }
                }
                if (parsed == 0) {
                    break;
                } else {
                    std::memmove(client_buffer, client_buffer + parsed, readed_bytes - parsed);
                    readed_bytes -= parsed;
                }
            }

            // There is command, but we still wait for argument to arrive...
            if (command_to_execute && arg_remains > 0) {
                //_logger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
                // There is some parsed command, and now we are reading argument
                std::size_t to_read = std::min(arg_remains, std::size_t(readed_bytes));
                argument_for_command.append(client_buffer, to_read);

                std::memmove(client_buffer, client_buffer + to_read, readed_bytes - to_read);
                arg_remains -= to_read;
                readed_bytes -= to_read;
            }

            // Thre is command & argument - RUN!
            if (command_to_execute && arg_remains == 0) {
                //_logger->debug("Start command execution");
                if(argument_for_command.back() == '\n'){
                    argument_for_command.pop_back();
                    argument_for_command.pop_back();
                }
                command_to_execute->Execute(*_pStorage, argument_for_command, result);
                result += "\r\n";
                global_result += result;
                SetWrite();
                command_to_execute.reset();
                argument_for_command.resize(0);
                parser.Reset();


            }
        }
    }}


// See Connection.h
void Connection::DoWrite() {
    std::cout << "DoWrite" << std::endl;
    send(_socket, global_result.data(), global_result.size(), 0);
    global_result.clear();
    _event.events = EPOLLIN|EPOLLRDHUP;
    }

void Connection::SetWrite() {
    _event.events |= EPOLLOUT;
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
