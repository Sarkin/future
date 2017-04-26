#pragma once

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace kinan {

template <typename T>
class shared_state {
public:
    shared_state() : is_initialized_(false) { }

    template <typename U = T, typename std::enable_if<!std::is_void<U>::value, int>::type = 0>
    void set_value(U&& v);

    template <typename U = T, typename std::enable_if<std::is_void<U>::value, int>::type = 0>
    void set_value();

    void set_exception(std::exception_ptr e);

    T get();
    T try_get();
    void wait();

private:
    bool is_initialized_;
    std::exception_ptr error_;
    std::unique_ptr<T> state_;
    mutable std::mutex init_mutex_;
    std::condition_variable init_cv_;
};

template <>
class shared_state<void> {
public:
    shared_state() : is_initialized_(false) { }

    template <typename U = T, typename std::enable_if<!std::is_void<U>::value, int>::type = 0>
    void set_value(U&& v);

    template <typename U = T, typename std::enable_if<std::is_void<U>::value, int>::type = 0>
    void set_value();

    void set_exception(std::exception_ptr e);

    T get();
    T try_get();
    void wait();

private:
    bool is_initialized_;
    std::exception_ptr error_;
    std::unique_ptr<T> state_;
    mutable std::mutex init_mutex_;
    std::condition_variable init_cv_;
};

template <typename T>
class future {
public:
    future(std::shared_ptr<shared_state<T>> state) : state_(state) { }
    ~future() { }
    future(const future& rhs) = delete;
    future(future&&) = default;
    future& operator=(const future& rhs) = delete;
    future& operator=(future&&) = default;

    T get();
    T try_get();
    void wait();

private:
    std::shared_ptr<shared_state<T>> state_;
};

template <typename T>
class promise {
public:
    promise() : state_(std::make_shared<shared_state<T>>()) { }
    ~promise() { }
    promise(const promise& rhs) = delete;
    promise(promise&&) = default;
    promise& operator=(const promise& rhs) = delete;
    promise& operator=(promise&&) = default;

    template <typename U = T, typename std::enable_if<!std::is_void<U>::value, int>::type = 0>
    void set_value(U&& v);

    template <typename U = T, typename std::enable_if<std::is_void<U>::value, int>::type = 0>
    void set_value();

    void set_exception(std::exception_ptr e);

    future<T> get_future();

    /*
    template <typename U>
    future<T> then(future<U> f);
    */

private:
    std::shared_ptr<shared_state<T>> state_;
};

class thread_pool {
public:
    thread_pool(size_t);
    template <typename F, typename... Args>
    void run(F&& f, Args&&... args);
    ~thread_pool() = default;

private:
    std::vector<std::thread> workers_;
    std::mutex queue_mutex_;
};
/*

template <bool try_async = false, typename F, typename... Args>
auto async(F&& f, Args&&... args) -> future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;
    auto p = promise<return_type>();
    p.set_value(f(args...));
    return p.get_feature();
}


template <bool try_async = true, typename F, typename... Args>
auto async(F&& f, Args&&... args) -> future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;
    std::shared_ptr<promise<return_type>> p;
    tp.run([=] {
        try {
            p->set_value(f(args...));
        } catch (...) {
            p->set_exception(std::current_exception());
        }
    });
    return p->get_feature();
}
*/


template <typename T>
template <typename U, typename std::enable_if<!std::is_void<U>::value, int>::type>
void shared_state<T>::set_value(U&& v) {
    std::lock_guard<std::mutex> lg(init_mutex_);
    state_ = std::make_unique<T>(std::forward<U>(v));
    is_initialized_ = true;
    init_cv_.notify_all();
}

template <>
template <>
void shared_state<void>::set_value() {
    std::lock_guard<std::mutex> lg(init_mutex_);
    is_initialized_ = true;
    init_cv_.notify_all();
}

template <typename T>
void shared_state<T>::set_exception(std::exception_ptr e) {
    std::lock_guard<std::mutex> lg(init_mutex_);
    error_ = e;
    is_initialized_ = true;
    init_cv_.notify_all();
}

template <typename T>
void shared_state<T>::wait() {
    std::unique_lock<std::mutex> lk(init_mutex_);
    init_cv_.wait(lk, [this]{return is_initialized_;});
}

template <typename T>
T shared_state<T>::get() {
    wait();
    return try_get();
}

template <typename T>
T shared_state<T>::try_get() {
    if (error_) {
        std::rethrow_exception(error_);
    }
    if (state_) {
        return std::move(*state_);
    }
    throw std::logic_error("invalid state");
}

template <typename T>
template <typename U, typename std::enable_if<!std::is_void<U>::value, int>::type>
void promise<T>::set_value(U&& v) {
    state_->set_value(std::forward<U>(v));
}

template <>
template <>
void promise<void>::set_value() {
    state_->set_value();
}

template <typename T>
void promise<T>::set_exception(std::exception_ptr e) {
    state_->set_exception(e);
}

template <typename T>
future<T> promise<T>::get_future() {
    return future<T>(state_);
}

template <typename T>
T future<T>::get() {
    return state_->get();
}

template <typename T>
T future<T>::try_get() {
    return state_->try_get();
}

template <typename T>
void future<T>::wait() {
    return state_->wait();
}

/*
template <typename T>
template <typename U>
future<T> future<T>::then(future<U> f) {
}
*/

} // namespace kinan
