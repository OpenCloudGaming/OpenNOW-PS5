// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <pthread.h>

namespace opennow {
enum : std::uint8_t { modifierShift=1, modifierCtrl=2, modifierAlt=4 };
struct KeyStroke { std::uint16_t vk=0, scan=0; std::uint8_t modifiers=0; };
struct InputEvent {
    enum class Kind : std::uint8_t { move, button, key, cancel } kind=Kind::move;
    int dx=0, dy=0;
    std::uint8_t button=0;
    bool down=false;
    KeyStroke key;
};
class InputQueue {
public:
    static constexpr unsigned capacity=32, keyLimit=capacity/2, moveLimit=65535;
    InputQueue() noexcept {pthread_mutex_init(&lock_,nullptr);}
    ~InputQueue() {pthread_mutex_destroy(&lock_);}
    InputQueue(const InputQueue&)=delete;
    InputQueue& operator=(const InputQueue&)=delete;
    void move(int dx,int dy) noexcept {
        if(!dx&&!dy)return;
        pthread_mutex_lock(&lock_);
        InputEvent* tail=count_?&events_[(head_+count_-1)%capacity]:nullptr;
        if(tail&&tail->kind==InputEvent::Kind::move) {
            tail->dx=std::clamp(tail->dx+dx,-static_cast<int>(moveLimit),static_cast<int>(moveLimit));
            tail->dy=std::clamp(tail->dy+dy,-static_cast<int>(moveLimit),static_cast<int>(moveLimit));
        } else if(count_<capacity) {
            InputEvent e;e.dx=std::clamp(dx,-static_cast<int>(moveLimit),static_cast<int>(moveLimit));
            e.dy=std::clamp(dy,-static_cast<int>(moveLimit),static_cast<int>(moveLimit));
            append(e);
        }
        pthread_mutex_unlock(&lock_);
    }
    bool button(std::uint8_t button,bool down) noexcept {
        InputEvent e;e.kind=InputEvent::Kind::button;e.button=button;e.down=down;
        return push(e,capacity);
    }
    bool key(const KeyStroke& key) noexcept {
        InputEvent e;e.kind=InputEvent::Kind::key;e.key=key;
        return push(e,keyLimit);
    }
    void cancel() noexcept {pthread_mutex_lock(&lock_);head_=count_=0;cancelled_=true;pthread_mutex_unlock(&lock_);}
    bool take(InputEvent& out,bool keyReady) noexcept {
        pthread_mutex_lock(&lock_);
        bool ok=cancelled_;
        if(ok){out=InputEvent{};out.kind=InputEvent::Kind::cancel;cancelled_=false;}
        else if(count_&&(keyReady||events_[head_].kind!=InputEvent::Kind::key)){ok=true;out=events_[head_];head_=(head_+1)%capacity;--count_;}
        pthread_mutex_unlock(&lock_);
        return ok;
    }
    void clear() noexcept {pthread_mutex_lock(&lock_);head_=count_=0;pthread_mutex_unlock(&lock_);}
    unsigned size() noexcept {pthread_mutex_lock(&lock_);const unsigned n=count_;pthread_mutex_unlock(&lock_);return n;}
private:
    bool push(const InputEvent& e,unsigned limit) noexcept {
        pthread_mutex_lock(&lock_);
        const bool ok=count_<limit;
        if(ok)append(e);
        pthread_mutex_unlock(&lock_);
        return ok;
    }
    void append(const InputEvent& e) noexcept {events_[(head_+count_)%capacity]=e;++count_;}
    pthread_mutex_t lock_;
    InputEvent events_[capacity]{};
    unsigned head_=0, count_=0;
    bool cancelled_=false;
};
}
