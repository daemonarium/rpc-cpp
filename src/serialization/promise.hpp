#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <exception>
#include <coroutine>
#include "type_concepts.hpp"

namespace serialization {

  template <typename T>
  struct is_promise {
    private:
      template <typename U>
      static auto test(int) -> decltype(
        std::declval<typename U::promised_type>(),
        std::true_type{}
      );
      
      template <typename>
      static std::false_type test(...);
      
    public:
      static constexpr bool value = decltype(test<T>(0))::value;
  };

  template <typename T>
  concept Promise = is_promise<T>::value;

  template <typename T>
  concept PromisePointer = is_shared_ptr<T>::value && Promise<typename T::element_type>;

  template <typename T>
  requires(PromisePointer<T>)
  static T::element_type::promised_type depromisify(T value) {
    return typename T::element_type::promised_type();
  }
  template <typename T>
  requires(!PromisePointer<T>)
  static T depromisify(T value) {
    return value;
  }

  enum class promise_state {
    pending = 0,
    resolved = 1,
    rejected = 2
  };

  template<typename T> class promise : public std::enable_shared_from_this<promise<T>> {
    public:

    using resolve_callback = std::function<void(T& result)>;
    using reject_callback = std::function<void(std::exception_ptr exception)>;
    using promised_type = T;

    promise_state state;

    T result;
    std::exception_ptr exception;

    std::vector<resolve_callback> resolve_callbacks;
    std::vector<reject_callback> reject_callbacks;

    promise() : state(promise_state::pending) {
    }
    ~promise() {}

    void run(std::function<void(std::shared_ptr<promise>)> fun) {
      try {
        fun(this->shared_from_this());
      } catch (...) {
        reject(std::current_exception());
      }
    }

    void resolve(T result) {
      state = promise_state::resolved;
      this->result = result;
      for(auto& cb : resolve_callbacks) {
        cb(this->result);
      }
      resolve_callbacks.clear();
      reject_callbacks.clear();
    }
    void reject(std::exception_ptr exception) {
      if(state == promise_state::rejected) return; // already rejected
      state = promise_state::rejected;
      this->exception = exception;
      for(auto& cb : reject_callbacks) {
        cb(this->exception);
      }
      resolve_callbacks.clear();
      if(reject_callbacks.size() == 0) {
        reject_callbacks.clear();
        std::rethrow_exception(exception);
      }
      reject_callbacks.clear();
    }

    void chain(std::shared_ptr<promise<T>> to) {
      on_rejected([to](std::exception_ptr ex){
        to->reject(ex);
      });
      on_resolved([to](T res){
        to->resolve(res);
      });
    }

    void on_resolved(resolve_callback callback) {
      if(state == promise_state::pending) {
        resolve_callbacks.push_back(callback);
      } else if(state == promise_state::resolved) {
        callback(this->result);
      }
    }
    void on_rejected(reject_callback callback) {
      if(state == promise_state::pending) {
        reject_callbacks.push_back(callback);
      } else if(state == promise_state::rejected) {
        callback(this->exception);
      }
    }

    void handle(std::function<void(T& result)> resolved, std::function<void(std::exception_ptr exception)> rejected) {
      on_resolved(resolved);
      on_rejected(rejected);
    }

    template<typename R> std::shared_ptr<promise<R>> then(std::function<std::shared_ptr<promise<R>>(T& result)> fun) {
      auto res = std::make_shared<promise<R>>();
      on_resolved([res, fun](T& result){
        fun(result)->chain(res);
      });
      on_rejected([res](std::exception_ptr exception) {
        res->reject(exception);
      });
      return res;
    }

    template<typename R> std::shared_ptr<promise<R>> then(std::function<R(T& result)> fun) {
      auto res = std::make_shared<promise<R>>();
      on_resolved([res, fun](T& result){
        try {
          res->resolve(fun(result));
        } catch(...) {
          res->reject(std::current_exception());
        }
      });
      on_rejected([res](std::exception_ptr exception) {
        res->reject(exception);
      });
      return res;
    }

    template<typename R> std::shared_ptr<promise<R>> then(std::function<std::shared_ptr<promise<R>>(T& result)> fun,
                                                          std::function<std::shared_ptr<promise<R>>(std::exception_ptr exception)> err) {
      auto res = std::make_shared<promise<R>>();
      on_resolved([res, fun](T& result){
        fun(result)->chain(res);
      });
      on_rejected([res, err](std::exception_ptr exception) {
        err(exception)->chain(res);
      });
      return res;
    }

    template<typename R> std::shared_ptr<promise<R>> then(std::function<std::shared_ptr<promise<R>>(T result)> fun,
                                                          std::function<void(std::exception_ptr exception)> err) {
      auto res = std::make_shared<promise<R>>();
      on_resolved([res, fun](T& result){
        fun(result)->chain(res);
      });
      on_rejected(err);
      on_rejected([res](std::exception_ptr exception) {
        res->reject(exception);
      });
      return res;
    }
    template<typename R> std::shared_ptr<promise<R>> then(std::function<void(T& result)> fun,
                                                           std::function<void(std::exception_ptr exception)> err) {
      auto res = std::make_shared<promise<R>>();
      on_resolved(fun);
      on_rejected(err);
      chain(res);
      return res;
    }

    template<typename R> std::shared_ptr<promise<R>> grab(std::function<void(std::exception_ptr exception)> err) {
      auto res = std::make_shared<promise<R>>();
      on_rejected(err);
      chain(res);
      return res;
    }

    template<typename R> std::shared_ptr<promise<R>> grab(std::function<std::shared_ptr<promise<R>> (std::exception_ptr exception)> err) {
      auto res = std::make_shared<promise<R>>();
      on_rejected([res, err](std::exception_ptr exception){
        err(exception)->chain(res);
      });
      on_resolved([res](T& result) {
        res->resolve(result);
      });
      return res;
    }

    static std::shared_ptr<promise<T>> resolved(T result) {
      auto p = std::make_shared<promise<T>>();
      p->resolve(result);
      return p;
    }
    static std::shared_ptr<promise<T>> rejected(std::exception_ptr exception) {
      auto p = std::make_shared<promise<T>>();
      p->reject(exception);
      return p;
    }
  };

  template <>
  class promise<void> {
    public:
    using resolve_callback = std::function<void()>;
    using reject_callback = std::function<void(std::exception_ptr exception)>;
    using promised_type = void;

    promise_state state;
    std::exception_ptr exception;
    std::vector<resolve_callback> resolve_callbacks;
    std::vector<reject_callback> reject_callbacks;

    promise() : state(promise_state::pending) {
    }
    ~promise() {}

    void resolve() {
      if (state != promise_state::pending) return;
      state = promise_state::resolved;
      for (auto& cb : resolve_callbacks) {
        cb();
      }
      resolve_callbacks.clear();
      reject_callbacks.clear();
    }
    
    void reject(std::exception_ptr e) {
      if (state != promise_state::pending) return;
      exception = e;
      state = promise_state::rejected;
      for (auto& cb : reject_callbacks) {
        cb(e);
      }
      resolve_callbacks.clear();
      reject_callbacks.clear();
    }

    void on_resolved(resolve_callback cb) {
      if (state == promise_state::resolved) {
        cb();
      } else if (state == promise_state::pending) {
        resolve_callbacks.push_back(cb);
      }
    }

    void on_rejected(reject_callback cb) {
      if (state == promise_state::rejected) {
        cb(exception);
      } else if (state == promise_state::pending) {
        reject_callbacks.push_back(cb);
      }
    }

    void handle(std::function<void()> resolved, std::function<void(std::exception_ptr exception)> rejected) {
      on_resolved(resolved);
      on_rejected(rejected);
    }

    template<typename R> std::shared_ptr<promise<R>> then(std::function<std::shared_ptr<promise<R>>()> fun,
                                                          std::function<std::shared_ptr<promise<R>>(std::exception_ptr exception)> err) {
      auto res = std::make_shared<promise<R>>();
      on_resolved([res, fun](){
        fun()->chain(res);
      });
      on_rejected([res, err](std::exception_ptr exception) {
        err(exception)->chain(res);
      });
      return res;
    }

    template<typename R> std::shared_ptr<promise<R>> then(std::function<std::shared_ptr<promise<R>>()> fun,
                                                          std::function<void(std::exception_ptr exception)> err) {
      auto res = std::make_shared<promise<R>>();
      on_resolved([res, fun](){
        fun()->chain(res);
      });
      on_rejected(err);
      on_rejected([res](std::exception_ptr exception) {
        res->reject(exception);
      });
      return res;
    }
    template<typename R> std::shared_ptr<promise<R>> then(std::function<void()> fun,
                                                           std::function<void(std::exception_ptr exception)> err) {
      auto res = std::make_shared<promise<R>>();
      on_resolved(fun);
      on_rejected(err);
      chain(res);
      return res;
    }


    template<typename R> std::shared_ptr<promise<R>> grab(std::function<void(std::exception_ptr exception)> err) {
      auto res = std::make_shared<promise<R>>();
      on_rejected([err](std::exception_ptr exception){
        err(exception);
      });
      on_resolved([res]() {
        if constexpr (!std::is_void_v<R>) {
          res->resolve();
        }
      });
      return res;
    }

    template<typename R> std::shared_ptr<promise<R>> grab(std::function<std::shared_ptr<promise<R>>(std::exception_ptr exception)> err) {
      auto res = std::make_shared<promise<R>>();
      on_rejected([err, res](std::exception_ptr exception){
        err(exception)->chain(res);
      });
      on_resolved([res]() {
        if constexpr (!std::is_void_v<R>) {
          res->resolve();
        }
      });
      return res;
    }

    static std::shared_ptr<promise<void>> resolved() {
      auto p = std::make_shared<promise<void>>();
      p->resolve();
      return p;
    }

    static std::shared_ptr<promise<void>> rejected(std::exception_ptr exception) {
      auto p = std::make_shared<promise<void>>();
      p->reject(exception);
      return p;
    }

    void chain(std::shared_ptr<promise<void>> to) {
      on_rejected([to](std::exception_ptr ex){
        to->reject(ex);
      });
      on_resolved([to](void){
        to->resolve();
      });
    }
  };

  // ========== CO_AWAIT SUPPORT ==========
  
  // Awaiter class for co_await support
  template<typename T>
  class promise_awaiter {
  private:
    std::shared_ptr<promise<T>> promise_ptr;
    
  public:
    explicit promise_awaiter(std::shared_ptr<promise<T>> p) : promise_ptr(p) {}
    
    bool await_ready() const noexcept {
      return promise_ptr->state != promise_state::pending;
    }
    
    void await_suspend(std::coroutine_handle<> handle) {
      promise_ptr->on_resolved([handle](T&) {
        handle.resume();
      });
      promise_ptr->on_rejected([handle](std::exception_ptr) {
        handle.resume();
      });
    }
    
    T await_resume() {
      if (promise_ptr->state == promise_state::rejected) {
        std::rethrow_exception(promise_ptr->exception);
      }
      return promise_ptr->result;
    }
  };

  // Specialization for void
  template<>
  class promise_awaiter<void> {
  private:
    std::shared_ptr<promise<void>> promise_ptr;
    
  public:
    explicit promise_awaiter(std::shared_ptr<promise<void>> p) : promise_ptr(p) {}
    
    bool await_ready() const noexcept {
      return promise_ptr->state != promise_state::pending;
    }
    
    void await_suspend(std::coroutine_handle<> handle) {
      promise_ptr->on_resolved([handle]() {
        handle.resume();
      });
      promise_ptr->on_rejected([handle](std::exception_ptr) {
        handle.resume();
      });
    }
    
    void await_resume() {
      if (promise_ptr->state == promise_state::rejected) {
        std::rethrow_exception(promise_ptr->exception);
      }
    }
  };

  // Free function operator co_await for std::shared_ptr<promise<T>>
  template<typename T>
  promise_awaiter<T> operator co_await(std::shared_ptr<promise<T>> p) {
    return promise_awaiter<T>(p);
  }

  // ========== CO_RETURN SUPPORT ==========
  
  // Coroutine promise type for co_return support
  // Usage example:
  //   std::shared_ptr<promise<int>> my_coroutine() {
  //     using promise_type = coroutine_promise<int>;
  //     auto result = co_await some_async_operation();
  //     co_return result + 1;  // Automatically resolves promise
  //   }
  template<typename T>
  class coroutine_promise {
  private:
    std::shared_ptr<promise<T>> result_promise;
    
  public:
    coroutine_promise() : result_promise(std::make_shared<promise<T>>()) {}
    
    std::shared_ptr<promise<T>> get_return_object() {
      return result_promise;
    }
    
    std::suspend_never initial_suspend() const noexcept {
      return {};
    }
    
    std::suspend_never final_suspend() const noexcept {
      return {};
    }
    
    void return_value(T value) {
      result_promise->resolve(std::move(value));
    }
    
    void unhandled_exception() {
      result_promise->reject(std::current_exception());
    }
  };

  // Specialization for void
  template<>
  class coroutine_promise<void> {
  private:
    std::shared_ptr<promise<void>> result_promise;
    
  public:
    coroutine_promise() : result_promise(std::make_shared<promise<void>>()) {}
    
    std::shared_ptr<promise<void>> get_return_object() {
      return result_promise;
    }
    
    std::suspend_never initial_suspend() const noexcept {
      return {};
    }
    
    std::suspend_never final_suspend() const noexcept {
      return {};
    }
    
    void return_void() {
      result_promise->resolve();
    }
    
    void unhandled_exception() {
      result_promise->reject(std::current_exception());
    }
  };
}

// Specialization of std::coroutine_traits for std::shared_ptr<serialization::promise<T>>
// This allows functions returning std::shared_ptr<promise<T>> to be coroutines
namespace std {
  template<typename T, typename... Args>
  struct coroutine_traits<std::shared_ptr<serialization::promise<T>>, Args...> {
    using promise_type = serialization::coroutine_promise<T>;
  };
}