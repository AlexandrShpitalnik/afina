#ifndef AFINA_NETWORK_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_NONBLOCKING_CONNECTION_H

#include <cstring>
#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>
#include "protocol/Parser.h"
#include <sys/epoll.h>

namespace Afina {
namespace Network {
namespace NonBlocking {

class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> ps) : _socket(s), _pStorage(ps) { std::memset(&_event, 0, sizeof(struct epoll_event)); }

    inline bool isAlive() const { return _is_running; }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class Worker;
    friend class ServerImpl;

    void SetWrite();
    int _socket;
    bool _is_running;
    struct epoll_event _event;
    int readed_bytes;
    char client_buffer[4096];
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::unique_ptr<Execute::Command> command_to_execute;
    std::string argument_for_command;
    std::string global_result;
    std::shared_ptr<Afina::Storage> _pStorage;


};

} // namespace NonBlocking
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_NONBLOCKING_CONNECTION_H
