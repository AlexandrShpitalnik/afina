#ifndef AFINA_NETWORK_MT_BLOCKING_SERVER_H
#define AFINA_NETWORK_MT_BLOCKING_SERVER_H

#include <atomic>
#include <thread>
#include <algorithm>

#include <afina/network/Server.h>
#include <netdb.h>
#include <stack>
#include <mutex>
#include <condition_variable>

namespace spdlog {
class logger;
}

namespace Afina {
namespace Network {
namespace MTblocking {

/**
 * # Network resource manager implementation
 * Server that is spawning a separate thread for each connection
 */
class ServerImpl : public Server {
public:
    ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl);
    ~ServerImpl();

    // See Server.h
    void Start(uint16_t port, uint32_t, uint32_t) override;

    // See Server.h
    void Stop() override;

    // See Server.h
    void Join() override;

protected:
    /**
     * Method is running in the connection acceptor thread
     */
    void OnRun();
    void Worker(std::vector<std::thread*>::iterator self_it, int client_socket);

private:
    // Logger instance
    std::shared_ptr<spdlog::logger> _logger;

    // Atomic flag to notify threads when it is time to stop. Note that
    // flag must be atomic in order to safely publisj changes cross thread
    // bounds
    std::atomic<bool> running;
    std::vector<std::thread*> _workers;
    std::stack<std::vector<std::thread*>::iterator> _completed;
    std::mutex _stack_mutex;
    std::mutex _condition_mutex;

    // Server socket to accept connections on
    int _server_socket;
    int num_workers = 0;
    bool _is_completed = false;
    // Thread to run network on
    std::thread _thread;
    std::condition_variable _cond_var;

    void StartWorker(int client_socket);
    void FreeStack();
};

} // namespace MTblocking
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_BLOCKING_SERVER_H
