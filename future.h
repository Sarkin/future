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

    template <typename U = T>
    void set_value(U&& v);

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

    void set_value();

    void set_exception(std::exception_ptr e);

    void wait();

private:
    bool is_initialized_;
    std::exception_ptr error_;
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

    template <bool try_async, typename F, typename... Args, typename std::enable_if<try_async, int>::type = 0>
    void run(F&& f, Args&&... args);

    template <bool try_async, typename F, typename... Args, typename std::enable_if<!try_async, int>::type = 0>
    void run(F&& f, Args&&... args);

    ~thread_pool();

    thread_pool(const thread_pool& rhs) = delete;
    thread_pool(thread_pool&&) = delete;
    thread_pool& operator=(const thread_pool& rhs) = delete;
    thread_pool& operator=(thread_pool&&) = delete;

private:
    size_t num_available_;
    bool stop_;
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::condition_variable cv_;
    std::mutex mutex_;
} tp(3);

template <typename F, typename... Args>
using ReturnType = typename std::result_of<F(Args...)>::type;

template <typename ...T>
class TD;


template <bool try_async, typename F, typename... Args, typename std::enable_if<std::is_void<ReturnType<F, Args...>>::value, int>::type = 0>
auto async(F&& f, Args&&... args) -> future<ReturnType<F, Args...>> {
    auto p = std::make_shared<promise<ReturnType<F, Args...>>>();
    auto run = [p](auto&& f, auto&&... args) {
        try {
            f(std::forward<decltype(args)>(args)...);
            p->set_value();
        } catch (...) {
            p->set_exception(std::current_exception());
        }
    };
    tp.run<try_async>(std::move(run), std::forward<F>(f), std::forward<Args>(args)...);
    return p->get_future();
}

template <bool try_async, typename F, typename... Args, typename std::enable_if<!std::is_void<ReturnType<F, Args...>>::value, int>::type = 0>
auto async(F&& f, Args&&... args) -> future<ReturnType<F, Args...>> {
    auto p = std::make_shared<promise<ReturnType<F, Args...>>>();
    tp.run<try_async>([p](auto&& f, auto&&... args) {
        try {
            p->set_value(f(std::forward<decltype(args)>(args)...));
        } catch (...) {
            p->set_exception(std::current_exception());
        }
    }, std::forward<F>(f), std::forward<Args>(args)...);
    return p->get_future();
}

template <typename T>
template <typename U>
void shared_state<T>::set_value(U&& v) {
    std::lock_guard<std::mutex> lg(init_mutex_);
    state_ = std::make_unique<T>(std::forward<U>(v));
    is_initialized_ = true;
    init_cv_.notify_all();
}

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

void shared_state<void>::set_exception(std::exception_ptr e) {
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

void shared_state<void>::wait() {
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

thread_pool::thread_pool(size_t n_threads) : num_available_(0), stop_(false) {
    for (int i = 0; i < n_threads; i++) {
        workers_.emplace_back(
            [this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lk(this->mutex_);
                        num_available_++;
                        this->cv_.wait(lk, [this] { return !this->tasks_.empty() || this->stop_; });
                        if (this->stop_) {
                            return;
                        }
                        task = std::move(this->tasks_.front());
                        this->tasks_.pop();
                    }
                    task();
                }
            }
        );
    }
}

thread_pool::~thread_pool() {
    {
        std::lock_guard<std::mutex> lg(mutex_);
        stop_ = true;
        cv_.notify_all();
    }
    for (auto& thread : workers_) {
        thread.join();
    }
}

template <bool try_async, typename F, typename... Args, typename std::enable_if<try_async, int>::type>
void thread_pool::run(F&& f, Args&&... args) {
    bool run_sync = true;
    {
        std::lock_guard<std::mutex> lg(mutex_);
        if  (num_available_ > 0) {
            num_available_--;
            tasks_.emplace(
                [&f, &args...] {
                    f(std::forward<Args>(args)...);
                }
            );
            cv_.notify_one();
        }
        run_sync = false;
    }
    if (run_sync) {
        run<false>(std::forward<F>(f), std::forward<Args>(args)...);
    }
}

template <bool try_async, typename F, typename... Args, typename std::enable_if<!try_async, int>::type>
void thread_pool::run(F&& f, Args&&... args) {
    f(std::forward<Args>(args)...);
}

/*
template <typename T>
template <typename U>
future<T> future<T>::then(future<U> f) {
}
*/

} // namespace kinan
