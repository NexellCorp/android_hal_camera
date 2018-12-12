#ifndef _NX_QUEUE_H
#define _NX_QUEUE_H

#include <deque>
#include <utils/Mutex.h>

namespace android {

template <class T>
class NXQueue
{
public:
    NXQueue() {
    };

    virtual ~NXQueue() {
    }

    void queue(const T& item) {
		Mutex::Autolock l(mLock);
		q.push_back(item);
    }

    const T& dequeue() {
		Mutex::Autolock l(mLock);
        const T& item = q.front();
        q.pop_front();
        return item;
    }

    bool isEmpty() {
		Mutex::Autolock l(mLock);
        return q.empty();
    }

    size_t size() {
		Mutex::Autolock l(mLock);
        return q.size();
    }

    const T& getHead() {
		Mutex::Autolock l(mLock);
        return q.front();
    }

    const T& getSecond() {
		Mutex::Autolock l(mLock);
		const T& second = q.at(1);
        return second;
    }

private:
	std::deque<T> q;
	Mutex mLock;
};

}; // namespace

#endif
