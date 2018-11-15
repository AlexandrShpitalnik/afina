#include "Engine.h"

#include <setjmp.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <csetjmp>
#include <string.h>

namespace Afina {
namespace Network {
namespace Coroutine {


void Engine::Store(context &ctx) {
    char StackEndsHere;
    ctx.Low = &StackEndsHere;
    ctx.Hight = this->StackBottom;
    unsigned int size = ctx.Hight -ctx.Low;
    std::get<1>(ctx.Stack) = size;
    if(std::get<0>(ctx.Stack)){
        delete[](std::get<0>(ctx.Stack));
    }
    std::get<0>(ctx.Stack) = new char[size];
    memcpy(std::get<0>(ctx.Stack), ctx.Low, size);

}

void Engine::Restore(context &ctx) {
    char StackEndsHere;
    if (&StackEndsHere > ctx.Low){
        Restore(ctx);
    }
    cur_routine = &ctx == idle_ctx ? nullptr : &ctx;
    memcpy(ctx.Low, std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
    std::longjmp(ctx.Environment, 1);
}


void Engine::Block(context &ctx){
    if (ctx.prev != nullptr) {
        ctx.prev->next = ctx.next;
    }

    if (ctx.next != nullptr) {
        ctx.next->prev = ctx.prev;
    }

    if (alive == cur_routine) {
        alive = alive->next;
    }

    ctx.next = blocked;
    blocked = &ctx;
    if (ctx.next != nullptr) {
        ctx.next->prev = &ctx;
    }

}

void Engine::Unblock() {
    std::array<struct epoll_event, 64> mod_list;
    int nmod = epoll_wait(epoll, &mod_list[0], mod_list.size(), -1);
    for (int i = 0; i < nmod; i++) {
        struct epoll_event &current_event = mod_list[i];
        context *ctx = static_cast<context*>(current_event.data.ptr);
        if (ctx->prev != nullptr) {
            ctx->prev->next = ctx->next;
        }

        if (ctx->next != nullptr) {
            ctx->next->prev = ctx->prev;
        }

        if (blocked == cur_routine) {
            blocked = blocked->next;
        }
        if ((current_event.events & EPOLLIN) || (current_event.events & EPOLLOUT)) {
            ctx->next = alive;
            alive = ctx;
            if (ctx->next != nullptr) {
                ctx->next->prev = ctx;
            }
        } else {
            ctx->prev = ctx->next = nullptr;
            delete std::get<0>(ctx->Stack);
            delete ctx->event;
            delete ctx;
        }
    }
}


void Engine::Stop(){
    is_running = false;
}


void Engine::yield() {
    if(this->cur_routine){
        if (setjmp(this->cur_routine->Environment) > 0) {
            return;
        }
        Block(*this->cur_routine);
        Store(*this->cur_routine);
    }
    if (alive != nullptr) {
        Restore(*alive);
    }
}


void Engine::sched(void *routine_) {
    if (routine_){
        if(this->cur_routine){
            if (setjmp(this->cur_routine->Environment) > 0) {
                return;
            }
            Block(*this->cur_routine);
            Store(*this->cur_routine);
        }
        auto arg = static_cast<context*>(routine_);
        Restore(*arg);
    }
}


} // namespace Coroutine
} // namespace Network
} // namespace Afina
