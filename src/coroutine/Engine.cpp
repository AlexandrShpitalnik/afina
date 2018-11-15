#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <csetjmp>
#include <string.h>

namespace Afina {
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

void Engine::yield() {
    if(this->cur_routine){
        if (setjmp(this->cur_routine->Environment) > 0) {
            return;
        }
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
            Store(*this->cur_routine);
        }
        auto arg = static_cast<context*>(routine_);
        Restore(*arg);
    }
}

} // namespace Coroutine
} // namespace Afina
