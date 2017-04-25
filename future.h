#pragma once

#include <exception>

template<typename T>
class shared_state {
public:
    void set_value(T&& v);
    void set_value(const T& v);
    void set_exception(std::exception_ptr e);

    T get();
    T try_get();
};

template<typename T>
class future {
public:
    future();
    ~future();
    future(const future& rhs) = delete;
    future(future&&) = default;
    future& operator=(const future& rhs) = delete;
    future& operator=(future&&) = default;

    T get();
    T try_get();
};

template<typename T>
class promise {
public:
    promise();
    ~promise();
    promise(const promise& rhs) = delete;
    promise(promise&&) = default;
    promise& operator=(const promise& rhs) = delete;
    promise& operator=(promise&&) = default;

    void set_value(T&& v);
    void set_value(const T& v);
    void set_exception(std::exception_ptr e);

    future<T> get_future();
};

template<typename T>
promise::promise() {

}
