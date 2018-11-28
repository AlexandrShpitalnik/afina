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

void* Engine::getCoroutineInfo(void* coroutine) {
    if (coroutine){
        auto ctx = static_cast<context*>(coroutine);
        return ctx->info;
    } else{
        return cur_routine->info;
    }

}

void Engine::setCoroutineInfo(void* coroutine, void* new_info) {
    if (coroutine){
        auto ctx = static_cast<context*>(coroutine);
        ctx->info = new_info;
    } else{
        cur_routine->info = new_info;
    }
}

void Engine::deleteCoroutine(void *coroutine) {
    if (coroutine){
        auto ctx = static_cast<context*>(coroutine);

        if (ctx->prev != nullptr) {
            ctx->prev->next = ctx->next;
        }

        if (ctx->next != nullptr) {
            ctx->next->prev = ctx->prev;
        }

        if (alive == ctx) {
            alive = alive->next;
        }

        delete std::get<0>(ctx->Stack);
        delete ctx;

    }
}


void Engine::stop(){
    is_running = false;
}


bool Engine::isRunnging() {
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
