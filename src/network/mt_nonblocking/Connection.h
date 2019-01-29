#ifndef AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H

#include <cstring>
#include <deque>
#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>
#include "protocol/Parser.h"
#include <sys/epoll.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> ps,
               std::mutex* connects_mutex,  std::deque<Connection*> *connects) : _socket(s), connects_mutex(connects_mutex),
                                                                                _connects(connects), _pStorage(ps){
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
    }

    inline bool isAlive() const { return _is_running.load(); }

    void Start(std::shared_ptr<spdlog::logger> logger, std::deque<Connection*>::iterator self_it);

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class Worker;
    friend class ServerImpl;


    std::shared_ptr<spdlog::logger> _logger;
    int _socket;
    struct epoll_event _event;

    std::atomic<bool> _is_running;

    int readed_bytes;
    int client_buff_ofs = 0;
    char client_buffer[4096];
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::unique_ptr<Execute::Command> command_to_execute;
    std::string argument_for_command;
    std::deque<std::string> global_result;
    size_t offs;

    std::deque<Connection*> *_connects;
    std::deque<Connection*>::iterator _self_it;
    std::shared_ptr<Afina::Storage> _pStorage;
    std::mutex* connects_mutex;

};

} // namespace MTnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
