#ifndef AFINA_NETWORK_COROUTINE_SERVER_H
#define AFINA_NETWORK_COROUTINE_SERVER_H

#include "Engine.h"
#include <afina/network/Server.h>
#include <thread>



namespace spdlog {
    class logger;
}

namespace Afina {
namespace Network {
namespace Coroutine {


class ServerImpl : public Server {
public:
    ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl);
    ~ServerImpl();

    // See Server.h
    void Start(uint16_t port, uint32_t acceptors, uint32_t workers) override;

    // See Server.h
    void Stop() override;

    // See Server.h
    void Join() override;

    std::thread _work_thread;

    void Ugly();

protected:


    static void OnRun(Afina::Network::Coroutine::Engine &engine, int &_server_socket, int &_event_fd);
    static void Reload(void *&info, int epoll, int fd, bool on_read);
    static void* Add(void *&coroutine, int epoll, int fd, bool on_read);

    static void Accept(Afina::Network::Coroutine::Engine &engine, int &socket, int &epoll, void *&prev);
    static void Worker(Afina::Network::Coroutine::Engine &engine, int &_socket, int &epoll, void *&prev);

private:

    // logger to use
    std::shared_ptr<spdlog::logger> _logger;


    Engine engine;

    // Port to listen for new connections, permits access only from
    // inside of accept_thread
    // Read-only
    uint16_t listen_port;

    int _server_socket;

    bool is_running;

    // Curstom event "device" used to wakeup workers
    int _event_fd;



};

} //namespace Afina 
} //namespace Network
} //namespace Coroutine
#endif // AFINA_NETWORK_COROUTINE_SERVER_H
