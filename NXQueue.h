#ifndef _NX_QUEUE_H
#define _NX_QUEUE_H

#include <queue>
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
		q.push(item);
    }

    const T& dequeue() {
		Mutex::Autolock l(mLock);
        const T& item = q.front();
        q.pop();
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

private:
	std::queue<T> q;
	Mutex mLock;
};

}; // namespace

#endif
