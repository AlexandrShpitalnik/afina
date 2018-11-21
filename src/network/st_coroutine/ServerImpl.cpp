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


    _work_thread = std::thread([&](){engine.start(OnRun, engine, _server_socket, _event_fd);});
            //&ServerImpl::Ugly, this);
    //engine.start(OnRun, engine, _server_socket);

}

void ServerImpl::Ugly(){
    engine.start(OnRun, engine, _server_socket, _event_fd);
}

// See Server.h
void ServerImpl::Stop() {
    _logger->warn("Stop network service");
    engine.stop();

    // Wakeup threads that are sleep on epoll_wait
    if (eventfd_write(_event_fd, 1)) {
        throw std::runtime_error("Failed to wakeup workers");
    }

    _work_thread.join();
}

// See Server.h
void ServerImpl::Join() {
    engine.stop();

    // Wakeup threads that are sleep on epoll_wait
    if (eventfd_write(_event_fd, 1)) {
        throw std::runtime_error("Failed to wakeup workers");
    }

    _work_thread.join();

}

// See ServerImpl.h
void ServerImpl::OnRun(Afina::Network::Coroutine::Engine &engine, int &_server_socket, int &_event_fd) {
    //engine._logger->info("Start engine");
    void *coroutine =  engine.getCoroutine();


    int epoll = epoll_create1(0);
    if (epoll == -1) {
        throw std::runtime_error("Failed to create epoll file descriptor: " + std::string(strerror(errno)));
    }

    struct epoll_event event2;
    event2.events = EPOLLIN;
    event2.data.fd = _event_fd;
    if (epoll_ctl(epoll, EPOLL_CTL_ADD, _event_fd, &event2)) {
        throw std::runtime_error("Failed to add file descriptor to epoll");
    }

    void* ac =  engine.run(ServerImpl::Accept, engine, _server_socket, epoll, coroutine);

    engine.setCoroutineInfo(ac, Add(ac, epoll, _server_socket, true));


    while (engine.isRunnging()) {
        std::array<struct epoll_event, 64> mod_list;
        int nmod = epoll_wait(epoll, &mod_list[0], mod_list.size(), -1);
        for (int i = 0; i < nmod; i++) {
            struct epoll_event &current_event = mod_list[i];
            if(current_event.data.fd == _event_fd){
                //engine._logger->debug("Break acceptor due to stop signal");
                continue;
            }
            if ((current_event.events & EPOLLERR) || (current_event.events & EPOLLHUP) ||
                (current_event.events & EPOLLRDHUP)){


                auto info = static_cast<epoll_event*>(engine.getCoroutineInfo(current_event.data.ptr));
                if (epoll_ctl(epoll, EPOLL_CTL_DEL, current_event.data.fd, info)) {
                    engine._logger->error("Failed to delete connection from epoll");
                }
                close(current_event.data.fd);
                engine.deleteCoroutine(current_event.data.ptr);

            } else {
                engine.sched(current_event.data.ptr);
            }
        }
    }
    std::cout << 'w';
}


void ServerImpl::Accept(Afina::Network::Coroutine::Engine &engine, int &socket, int &epoll, void *&prev) { // todo - logger(in engine)
    while (engine.isRunnging()) {
        struct sockaddr in_addr;
        socklen_t in_len;
        void* new_connection = nullptr;
        // No need to make these sockets non blocking since accept4() takes care of it.
        in_len = sizeof in_addr;
        int infd = accept4(socket, &in_addr, &in_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (infd == -1) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                //void* info = engine.getCoroutineInfo(nullptr);
                //void* cr =  engine.getCoroutine();
                //Add(cr, epoll, socket, true);
                engine.sched(prev);
                continue; // We have processed all incoming connections.
            } else {
                //engine._logger->error("Failed to accept socket");
                continue;
            }
        }
        // Print host and service info.
        char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
        int retval = getnameinfo(&in_addr, in_len, hbuf, sizeof hbuf, sbuf, sizeof sbuf,
                                 NI_NUMERICHOST | NI_NUMERICSERV);
        if (retval == 0) {
            //engine._logger->info("Accepted connection on descriptor {} (host={}, port={})\n", infd, hbuf, sbuf);
        }
        new_connection = engine.run(Worker, engine, infd, epoll, prev);
        engine.setCoroutineInfo(new_connection, Add(new_connection, epoll, infd, true));
    }
    std::cout << 'a';
    shutdown(socket, SHUT_RDWR);


}



void ServerImpl::Reload(void *&info, int epoll, int fd, bool on_read) {
    auto event = static_cast<epoll_event*>(info);
    event->events = EPOLLRDHUP|EPOLLHUP|EPOLLERR;
    if (on_read) {
        event->events |= EPOLLIN;
    } else {
        event->events |= EPOLLOUT;
    }
    if (epoll_ctl(epoll, EPOLL_CTL_MOD, fd, event)) {
        throw std::runtime_error("Failed to add file descriptor to epoll");
    }
    //return event;
}


void* ServerImpl::Add(void *&coroutine, int epoll, int fd, bool on_read) {
    auto event = new epoll_event;
    event->data.fd = fd;
    event->data.ptr = coroutine;
    event->events = EPOLLRDHUP|EPOLLHUP|EPOLLERR;
    if (on_read) {
        event->events |= EPOLLIN;
    } else {
        event->events |= EPOLLOUT;
    }
    if (epoll_ctl(epoll, EPOLL_CTL_ADD, fd, event)) {
        throw std::runtime_error("Failed to add file descriptor to epoll");
    }
    //return event;
}





void ServerImpl::Worker(Afina::Network::Coroutine::Engine &engine, int &_socket, int &epoll, void *&prev) {

    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    int readed_bytes = -1;
    char client_buffer[4096];

    while (engine.isRunnging() && (readed_bytes = read(_socket, client_buffer, sizeof(client_buffer))) > 0) {
        //engine._logger->debug("Got {} bytes from socket", readed_bytes);
        while (readed_bytes > 0 && engine.isRunnging()) {
            //engine._logger->debug("Process {} bytes", readed_bytes);
            // There is no command yet
            if (!command_to_execute) {
                std::size_t parsed = 0;
                if (parser.Parse(client_buffer, readed_bytes, parsed)) {
                    //engine._logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
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
                //engine._logger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
                // There is some parsed command, and now we are reading argument
                std::size_t to_read = std::min(arg_remains, std::size_t(readed_bytes));
                argument_for_command.append(client_buffer, to_read);

                std::memmove(client_buffer, client_buffer + to_read, readed_bytes - to_read);
                arg_remains -= to_read;
                readed_bytes -= to_read;
            }

            // Thre is command & argument - RUN!
            if (command_to_execute && arg_remains == 0) {
                //engine._logger->debug("Start command execution");
                if(argument_for_command.back() == '\n'){
                    argument_for_command.pop_back();
                    argument_for_command.pop_back();
                }
                std::string result;
                command_to_execute->Execute(*(engine.Storage), argument_for_command, result);
                result += "\r\n";


                void * &info = engine.getCoroutineInfo(nullptr);
                //void *coroutine = engine.getCoroutine();
                Reload(info, epoll, _socket, false);
                //Reload(info, epoll, _socket, false);



                // Send response

                int offs = 0;

                do {

                    engine.sched(prev);

                    int new_offs = send(_socket, result.data() + offs, result.size() - offs, 0);
                    if (new_offs <= 0) {
                        throw std::runtime_error("Failed to send response");
                    }

                    offs += new_offs;

                } while (offs != result.size());
                command_to_execute.reset();
                argument_for_command.resize(0);
                parser.Reset();
            }
        }
        if(engine.isRunnging()) {


            void *&info = engine.getCoroutineInfo(nullptr);
            //void *coroutine = engine.getCoroutine();
            Reload(info, epoll, _socket, true);

            //Reload(info, epoll, _socket, true);
            engine.sched(prev);
        } else {
            std::cout << 'w';
            shutdown(_socket, SHUT_RDWR);
        }

    }
}





} // namespace Coroutine
} // namespace Network
} // namespace Afina
