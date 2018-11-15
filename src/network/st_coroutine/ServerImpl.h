#ifndef AFINA_NETWORK_COROUTINE_SERVER_H
#define AFINA_NETWORK_COROUTINE_SERVER_H

#include "Engine.h"
#include <afina/network/Server.h>


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

protected:
    void OnRun();
    void OnNewConnection(int _socket);

private:
    // logger to use
    std::shared_ptr<spdlog::logger> _logger;

    void Reload(Engine::context* ctx, int fd, bool on_read);

    Engine engine;

    // Port to listen for new connections, permits access only from
    // inside of accept_thread
    // Read-only
    uint16_t listen_port;

    int _server_socket;

    bool is_running;



};

} //namespace Afina 
} //namespace Network
} //namespace Coroutine
#endif // AFINA_NETWORK_COROUTINE_SERVER_H
