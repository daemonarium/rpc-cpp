#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <exception>

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
}