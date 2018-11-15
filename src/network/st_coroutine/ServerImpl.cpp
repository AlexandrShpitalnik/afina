#include "Engine.h"
#include "ServerImpl.h"
#include "Utils.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <spdlog/logger.h>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>

#include "protocol/Parser.h"

namespace Afina {
namespace Network {
namespace Coroutine {


// See Serverh
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl) : Server(ps, pl) {}

// See Server.h
ServerImpl::~ServerImpl() {}

// See Server.h
void ServerImpl::Start(uint16_t port, uint32_t n_acceptors, uint32_t n_workers) {
    _logger = pLogging->select("network");
    _logger->info("Start network service");

    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    // Create server socket
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_port = htons(port);       // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address

    _server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_server_socket == -1) {
        throw std::runtime_error("Failed to open socket: " + std::string(strerror(errno)));
    }

    int opts = 1;
    if (setsockopt(_server_socket, SOL_SOCKET, (SO_KEEPALIVE), &opts, sizeof(opts)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket setsockopt() failed: " + std::string(strerror(errno)));
    }

    if (bind(_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket bind() failed: " + std::string(strerror(errno)));
    }

    make_socket_non_blocking(_server_socket);
    if (listen(_server_socket, 5) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket listen() failed: " + std::string(strerror(errno)));
    }

    is_running = true;

    engine.start(ServerImpl::OnRun, this);

}

// See Server.h
void ServerImpl::Stop() {
    _logger->warn("Stop network service");
    engine.Stop();
}

// See Server.h
void ServerImpl::Join() {
        engine.Stop();

}

// See ServerImpl.h
void ServerImpl::OnRun() {
    _logger->info("Start acceptor");
    //accept_cor =  engine.run(this->Accept, )

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = _server_socket;
    event.data.ptr = engine.cur_routine;

    if (epoll_ctl(engine.epoll, EPOLL_CTL_ADD, _server_socket, &event)) {
        throw std::runtime_error("Failed to add file descriptor to epoll");
    }

    for(;;){
        engine.yield();
        for (;;) {
            struct sockaddr in_addr;
            socklen_t in_len;
            void* new_connection = nullptr;

            // No need to make these sockets non blocking since accept4() takes care of it.
            in_len = sizeof in_addr;
            int infd = accept4(_server_socket, &in_addr, &in_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (infd == -1) {
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                    break; // We have processed all incoming connections.
                } else {
                    _logger->error("Failed to accept socket");
                    break;
                }
            }

            // Print host and service info.
            char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
            int retval = getnameinfo(&in_addr, in_len, hbuf, sizeof hbuf, sbuf, sizeof sbuf,
                                     NI_NUMERICHOST | NI_NUMERICSERV);
            if (retval == 0) {
                _logger->info("Accepted connection on descriptor {} (host={}, port={})\n", infd, hbuf, sbuf);
            }

            new_connection = engine.run(ServerImpl::OnRun, this, infd);

        }
    }
}


void ServerImpl::Reload(Engine::context *ctx, int fd, bool on_read) { //implementation in engine
    if (ctx->event == nullptr) {
        ctx->event = new epoll_event;
        ctx->event->data.fd = fd;
        ctx->event->data.ptr = engine.cur_routine;
    }
    ctx->event->events = EPOLLRDHUP | EPOLLONESHOT;
    if (on_read) {
        ctx->event->events |= EPOLLIN;
    } else {
        ctx->event->events |= EPOLLOUT;
    }
    if (epoll_ctl(engine.epoll, EPOLL_CTL_ADD, fd, ctx->event)) {
        throw std::runtime_error("Failed to add file descriptor to epoll");
    }
}






void ServerImpl::OnNewConnection(int _socket){

    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    int readed_bytes = -1;
    char client_buffer[4096];

    while (is_running && (readed_bytes = read(_socket, client_buffer, sizeof(client_buffer))) > 0) {
        _logger->debug("Got {} bytes from socket", readed_bytes);
        while (readed_bytes > 0 && is_running) {
            _logger->debug("Process {} bytes", readed_bytes);
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
                std::string result;
                command_to_execute->Execute(*pStorage, argument_for_command, result);
                result += "\r\n";

                Reload(engine.cur_routine, _socket, false);

                engine.yield();
                command_to_execute.reset();
                argument_for_command.resize(0);
                parser.Reset();
            }
        }
        Reload(engine.cur_routine, _socket, true);
        engine.yield();

    }
}





} // namespace Coroutine
} // namespace Network
} // namespace Afina
