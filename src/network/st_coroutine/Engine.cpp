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


void* Engine::getCoroutine() {
    return cur_routine;
}

int Engine::getCoroutineEvent() {
    return cur_routine->event;
}

void Engine::setCoroutineEvent(void* coroutine, int event){
    auto ctx = static_cast<context*>(coroutine);
    ctx->event = event;
}

void Engine::blockCoroutine() {
    context *ctx = cur_routine;
    if (!ctx -> is_blocked){

        ctx->is_blocked = true;

        if (ctx->prev != nullptr) {
            ctx->prev->next = ctx->next;
        }

        if (ctx->next != nullptr) {
            ctx->next->prev = ctx->prev;
        }

        if (alive == cur_routine) {
            alive = alive->next;
        }

        ctx->prev = nullptr;
        ctx->next = blocked;
        blocked = ctx;
        if (ctx->next != nullptr) {
            ctx->next->prev = ctx;
        }
    }
}



void Engine::unblockCoroutine(void *coroutine) {
    auto ctx = static_cast<context*>(coroutine);

    ctx->is_blocked = false;

    if (ctx->prev != nullptr) {
        ctx->prev->next = ctx->next;
    }

    if (ctx->next != nullptr) {
        ctx->next->prev = ctx->prev;
    }

    if (blocked == cur_routine) {
        blocked = blocked->next;
    }

    ctx->prev = nullptr;
    ctx->next = alive;
    alive = ctx;
    if (ctx->next != nullptr) {
        ctx->next->prev = ctx;
    }
}


void Engine::unblockAll(){
    if (alive) {
        context *tmp = alive;
        while (tmp->next) {
            tmp = tmp->next;
        }
        tmp->next = blocked;
        if (blocked) {
            tmp->next->prev = tmp;
        }
    } else {
        alive = blocked;
    }

    blocked = nullptr;
}



void Engine::stop(){
    is_running = false;
}


bool Engine::isRunning() {
    return is_running;
}




void Engine::yield() {
    if(this->cur_routine){
        if (setjmp(this->cur_routine->Environment) > 0) {
            return;
        }
        Store(*this->cur_routine);
    }

    if (alive != nullptr) {
        if (alive != cur_routine) {
            Restore(*alive);
        } else if (alive->next) {
            Restore(*alive->next);
        }
    }

    if(this->cur_routine){
        Restore(*idle_ctx);
    }

}


void Engine::sched(void *routine_) {
    if (routine_){
        if(this->cur_routine){
            if (setjmp(this->cur_routine->Environment) > 0) {
                return;
            }
            Store(*this->cur_routine);
        }
        auto arg = static_cast<context*>(routine_);
        Restore(*arg);
    }
}


} // namespace Coroutine
} // namespace Network
} // namespace Afina
