#include <iostream>
#include "SimpleLRU.h"

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value) {
        auto it = _lru_index.find(map_key(key));
        size_t node_size = key.size()+value.size();
        if(it != _lru_index.end()) {
            _cur_size -= it->second.get().value.size();
        }
        if(node_size < _max_size) {
            while (node_size > (_max_size -_cur_size)) {
                _cur_size -= (_lru_tail->key.size() + _lru_tail->value.size());
                DeleteLastNode();
            }
            if(it == _lru_index.end()) {
                AddNode(key, value);
                _cur_size += node_size;
            } else {
                map_node tmp = it->second.get();
                MoveNode(tmp);
                tmp.get().value = value;
            }
            return true;
        } else
            return false;
    }

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) {
        auto it = _lru_index.find(map_key(key));
        size_t node_size = key.size()+value.size();
        if(it == _lru_index.end() && node_size <= _max_size){
            while (node_size > (_max_size -_cur_size)) {
                _cur_size -= (_lru_tail->key.size() + _lru_tail->value.size());
                DeleteLastNode();
            }
            AddNode(key, value);
            return true;
        } else
            return false;
    }

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) {
        auto it = _lru_index.find(map_key(key));
        if(it != _lru_index.end()){
            map_node tmp = it->second.get();
            MoveNode(tmp);
            tmp.get().value = value;
            return true;
        } else
            return false;
    }


// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key) {
        auto it = _lru_index.find(map_key(key));
        if(it != _lru_index.end()){
            map_node tmp = it->second.get();
            DeleteNode(tmp);
            _lru_index.erase(it);
            return true;
        } else
            return false;
    }

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value) const {
        auto it = _lru_index.find(map_key(key));
        if(it != _lru_index.end()){
            map_node tmp = it->second.get();
            MoveNode(tmp);
            value = _lru_head->value;
            return true;
        } else
            return false;
    }


void SimpleLRU::MoveNode(lru_node &tmp) const {
    if(_lru_head.get() != _lru_tail && tmp.prev) {
        if (tmp.next) {
            tmp.next->prev = tmp.prev;
            tmp.prev->next.release();
            tmp.prev->next = std::move(tmp.next);
            _lru_head->prev = &tmp;
            tmp.next = std::move(_lru_head);
            _lru_head.reset(&tmp);
            _lru_head->prev = nullptr;

        } else{                  //if node is the tail
            _lru_head->prev = _lru_tail;
            _lru_tail = tmp.prev;
            tmp.prev = nullptr;
            tmp.next = std::move(_lru_head);
            _lru_tail->next.release();
            _lru_head.reset(&tmp);
            _lru_tail->next.release();
        }
    }}


void SimpleLRU::AddNode(const std::string &key, const std::string &value) {
    auto tmp = new lru_node;
    if(!_lru_tail) {
        _lru_tail = tmp;
        _lru_head.reset(_lru_tail);
    } else {
        _lru_head->prev = tmp;
        tmp->next = std::move(_lru_head);
        _lru_head.reset(tmp);
    }
    _lru_head->key = key;
    _lru_head->value = value;
    _lru_index.emplace(map_pair(map_key(_lru_head->key), map_node(*_lru_head)));
    }


void SimpleLRU::DeleteLastNode() {
    if (!_lru_tail->prev) {
        _lru_index.erase(map_key(_lru_tail->key));
        _lru_head.reset();
    } else {
        lru_node *tmp = _lru_tail->prev;
        _lru_index.erase(map_key(_lru_tail->key));
        tmp->next.reset();
        _lru_tail = tmp;
    }
}


void SimpleLRU::DeleteNode(lru_node &tmp) {
    if(tmp.next.get() == tmp.prev) {
        _lru_head.reset();
        _lru_tail = nullptr;
    }else if(!tmp.prev){
        tmp.next->prev = nullptr;
        _lru_head = std::move(tmp.next);
    } else if(!tmp.next) {
        _lru_tail = tmp.prev;
        tmp.prev->next.reset();
    }else{
        tmp.next->prev = tmp.prev;
        tmp.prev->next = std::move(tmp.next);
    }
}

} // namespace Backend
} // namespace Afina
