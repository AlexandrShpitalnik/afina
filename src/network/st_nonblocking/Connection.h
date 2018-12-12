#ifndef AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H

#include <cstring>
#include <deque>
#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>
#include "protocol/Parser.h"
#include <sys/epoll.h>

namespace Afina {
namespace Network {
namespace STnonblock {

class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> ps) : _socket(s), _pStorage(ps), offs(0) {
        std::memset(&_event, 0, sizeof(struct epoll_event));
    }

    inline bool isAlive() const { return _is_running; }

    void Start(std::shared_ptr<spdlog::logger> logger, std::deque<Connection*>::iterator self_it);

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class ServerImpl;

    void SetWrite();

    std::shared_ptr<spdlog::logger> _logger;
    int _socket;
    struct epoll_event _event;
    bool _is_running;
    int readed_bytes;
    int client_buff_ofs = 0;
    char client_buffer[4096];
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::unique_ptr<Execute::Command> command_to_execute;
    std::string argument_for_command;
    std::deque<std::string> global_result;
    size_t offs;
    std::deque<Connection*>::iterator _self_it ;
    std::shared_ptr<Afina::Storage> _pStorage;

};

} // namespace STnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
