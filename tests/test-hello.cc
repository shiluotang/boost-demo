#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <stdexcept>
#include <memory>
#include <utility>
#include <queue>
#include <random>
#include <valarray>
#include <functional>

#include <thread>
#include <future>
#include <mutex>
#include <chrono>

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/coroutine/coroutine.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/atomic.hpp>

#include <gtest/gtest.h>

#include "logging.hh"
#include "thread_pool.hh"
#include "thread_pool2.hh"
#include "thread_pool3.hh"

namespace {

struct foo {
    explicit
    foo(std::string const &s) :_M_name(s) {
        LOGI(__PRETTY_FUNCTION__ << ", name = " << _M_name);
    }

    ~foo() {
        LOGI(__PRETTY_FUNCTION__ << ", name = " << _M_name);
    }

    std::string _M_name;
};

} // namespace anonymous

TEST(boost_async, test_pool) {
    using boost::chrono::seconds;
    using boost::chrono::milliseconds;
    using boost::this_thread::sleep_for;
    boost::atomic_int counter(0);
    {
    thread_pool3 p(1);
    boost::shared_ptr<foo> ptr_to_f(new foo("outside-lambda"));
    auto x = p.fixed_rate([&counter, ptr_to_f]() {
                foo f2("inside-lambda");
                // sleep_for(milliseconds(10));
                LOGI("hello " << __PRETTY_FUNCTION__);
                ++counter;
            },
            milliseconds(0),
            milliseconds(10)
            );
    ptr_to_f.reset();
    sleep_for(milliseconds(1000));
    LOGI("x->stop()");
    x->stop();
    LOGI("x.reset()");
    x.reset();
    LOGI("p.shutdown()");
    p.shutdown();
    LOGI("sleep_for(milliseconds(10))");
    sleep_for(milliseconds(10));
    }
    LOGI("counter = " << counter);
    ASSERT_TRUE(counter <= 1000 / 10 + 1 && counter >= 1000 / 10);
}

TEST(boost_async, test_waitable) {
    typedef boost::chrono::system_clock clock_type;
    typedef boost::asio::basic_waitable_timer<clock_type> waitable_timer_type;
    using boost::chrono::milliseconds;
    using boost::this_thread::sleep_for;

    boost::asio::io_service srv;
    boost::thread_group tg;
    boost::shared_ptr<boost::asio::io_service::work> w(new boost::asio::io_service::work(srv));
    tg.create_thread([&srv]{
                LOGI("srv.run()");
                size_t n = srv.run();
                LOGI("srv.run() = " << n);
            });
    waitable_timer_type wt(srv);
    wt.expires_at(clock_type::now());
    typedef boost::function<void(boost::system::error_code const &ec)>
        handler_type;
    handler_type lambda =
        [&wt, &lambda](boost::system::error_code const &ec){
                 if (!!ec) {
                     LOGW("ec = " << ec);
                     return;
                 }
                 std::size_t n = wt.expires_from_now(milliseconds(1));
                 LOGI("expires_from_now() = " << n);
                 LOGI("lambda handler");
                 wt.async_wait(lambda);
            };
    wt.async_wait(lambda);
    sleep_for(milliseconds(10));
    w.reset();
    if (!srv.stopped())
        srv.stop();
    tg.join_all();
    LOGI("done");
}
