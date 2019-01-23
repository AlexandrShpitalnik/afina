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
#include <thread>


#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>

#include "protocol/Parser.h"

namespace Afina {
namespace Network {
namespace Coroutine {


// See Serverh
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl) : Server(ps, pl),
                                                                                                   engine(ps, pl) {}

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


    _event_fd = eventfd(0, EFD_NONBLOCK);
    if (_event_fd == -1) {
        throw std::runtime_error("Failed to create epoll file descriptor: " + std::string(strerror(errno)));
    }


    //_work_thread = std::thread([&](){engine.start(OnRun, engine, _server_socket, _event_fd);});
    _work_thread = std::thread([this] { this->engine.start([this] { this->Idle(); }, [this] { this->OnRun(); }); });


    }


// See Server.h
void ServerImpl::Stop() {
    _logger->warn("Stop network service");
    engine.stop();

    // Wakeup threads that are sleep on epoll_wait
    if (eventfd_write(_event_fd, 1)) {
        throw std::runtime_error("Failed to wakeup workers");
    }


}

// See Server.h
void ServerImpl::Join() {

    _work_thread.join();

}


void ServerImpl::Idle() {
    while (is_running) {

        if (!engine.isRunning()){
            is_running = false;
            engine.unblockAll();
            engine.yield();
        }

        std::array<struct epoll_event, 64> mod_list;
        int nmod = epoll_wait(epoll, &mod_list[0], mod_list.size(), -1);
        for (int i = 0; i < nmod; i++) {
            struct epoll_event &current_event = mod_list[i];

            if (current_event.data.fd == _event_fd) {
                //engine._logger->debug("Break acceptor due to stop signal");
                engine.stop();
                continue;
            } else if (current_event.data.fd == _server_socket) {
                Accept();
                continue;
            }

            void* coroutine = current_event.data.ptr;
            engine.setCoroutineEvent(coroutine, current_event.events);
            engine.unblockCoroutine(coroutine);
        }
        engine.yield();
    }
}


// See ServerImpl.h
void ServerImpl::OnRun() {


    epoll = epoll_create1(0);
    if (epoll == -1) {
        throw std::runtime_error("Failed to create epoll file descriptor: " + std::string(strerror(errno)));
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = _server_socket;
    if (epoll_ctl(epoll, EPOLL_CTL_ADD, _server_socket, &event)) {
        throw std::runtime_error("Failed to add file descriptor to epoll");
    }


    struct epoll_event event2;
    event2.events = EPOLLIN;
    event2.data.fd = _event_fd;
    if (epoll_ctl(epoll, EPOLL_CTL_ADD, _event_fd, &event2)) {
        throw std::runtime_error("Failed to add file descriptor to epoll");
    }
}




void ServerImpl::Accept() {
    for (;;) {
        struct sockaddr in_addr;
        socklen_t in_len;

        // No need to make these sockets non blocking since accept4() takes care of it.
        in_len = sizeof in_addr;
        int infd = accept4(_server_socket, &in_addr, &in_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (infd == -1) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                break; // We have processed all incoming connections.
            } else {
                //_logger->error("Failed to accept socket");
                break;
            }
        }

        // Print host and service info.
        char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
        int retval =
            getnameinfo(&in_addr, in_len, hbuf, sizeof hbuf, sbuf, sizeof sbuf, NI_NUMERICHOST | NI_NUMERICSERV);
        if (retval == 0) {
           // _logger->info("Accepted connection on descriptor {} (host={}, port={})\n", infd, hbuf, sbuf);
        }

         void* new_connection = engine.run([this, infd] { this->Worker(infd); } );
    }
}


int ServerImpl::BlockingRead(const int fd, void *buf, unsigned count, epoll_event *event){
    int exit_code = -1;

    while (is_running) {
        int readed_bytes = read(fd, buf, count);
        int mask = EPOLLRDHUP|EPOLLHUP|EPOLLERR|EPOLLIN;

        if (readed_bytes <= 0) {
            if (event->events != mask){
                event->events = mask;
                if (epoll_ctl(epoll, EPOLL_CTL_MOD, fd, event)) {
                    throw std::runtime_error("Failed to mod file descriptor to epoll");
                }
            }

            engine.blockCoroutine();
            engine.yield();

            int cur_events = engine.getCoroutineEvent();
            if (cur_events & EPOLLRDHUP) {
                exit_code = 0;
                break;
            }
            if (cur_events & (EPOLLERR | EPOLLHUP)) {
                break;
            }
        } else {
            return readed_bytes;
        }
    }

    return exit_code;
}



int ServerImpl::BlockingWrite(const int fd, const void *buf, unsigned count, epoll_event *event){
    int written = 0;
    int mask = EPOLLRDHUP|EPOLLHUP|EPOLLERR|EPOLLOUT;

    while (is_running) {
        written += write(fd, (char *)buf + written, count - written);

        if (written < count) {
            if (event->events != mask){
                if (epoll_ctl(epoll, EPOLL_CTL_MOD, fd, event)) {
                    throw std::runtime_error("Failed to add file descriptor to epoll");
                }
            }
            engine.blockCoroutine();
            engine.yield();
            int cur_events = engine.getCoroutineEvent();
            if (cur_events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
                break;
            }
        } else {
            return written;
        }
    }

    return -1;
}



void ServerImpl::Worker(const int client_socket) {

    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    int readed_bytes = -1;
    char client_buffer[4096];


    try{
        struct epoll_event *event = new struct epoll_event;
        event->data.ptr = engine.getCoroutine();
        event->events = EPOLLRDHUP|EPOLLHUP|EPOLLERR|EPOLLIN;
        if (epoll_ctl(epoll, EPOLL_CTL_ADD, client_socket, event)) {
            throw std::runtime_error("Failed to add file descriptor to epoll");
        }

        while (is_running && (readed_bytes = BlockingRead(client_socket, client_buffer, sizeof(client_buffer), event)) > 0) {
            //_logger->debug("Got {} bytes from socket", readed_bytes);
            while (readed_bytes > 0 && is_running) {
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
                   // _logger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
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

                    std::string result;
                    command_to_execute->Execute(*pStorage, argument_for_command, result);

                    result += "\r\n";
                    // Send response
                    if (BlockingWrite(client_socket, result.data(), result.size(), event) <= 0) {
                        throw std::runtime_error("Failed to send response");
                    }

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            } // while (readed_bytes)
        }

        if (readed_bytes == 0) {
            //_logger->debug("Connection closed");
        } else {
            throw std::runtime_error(std::string(strerror(errno)));
        }


        if (epoll_ctl(epoll, EPOLL_CTL_DEL, client_socket, event)) {
            //engine._logger->error("Failed to delete connection from epoll");
        }
        delete event;

    } catch (std::runtime_error &ex) {
        //_logger->error("Failed to process connection on descriptor {}: {}", client_socket, ex.what());
    }

    close(client_socket);

}






} // namespace Coroutine
} // namespace Network
} // namespace Afina
