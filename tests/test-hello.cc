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

namespace {

template <typename Clock>
std::string timestamp(std::chrono::time_point<Clock> const &tp) {
    using std::chrono::duration_cast;
    using std::chrono::seconds;
    using std::chrono::milliseconds;
    using duration = typename Clock::duration;
    duration d = tp.time_since_epoch();
    std::time_t t = duration_cast<seconds>(d).count();
    struct tm *tm_ptr = std::localtime(&t);
    std::vector<char> buffer(50);
    size_t n = std::strftime(&buffer[0], buffer.size(), "%F %T", tm_ptr);
    if (n <= 0)
        throw std::runtime_error("strftime");
    std::ostringstream oss;
    oss << &buffer[0];
    oss << "." << std::setw(3) << std::setfill('0')
        << duration_cast<milliseconds>(d - duration_cast<seconds>(d)).count();
    return oss.str();
}

template <typename Clock>
std::string timestamp(boost::chrono::time_point<Clock> const &tp) {
    using boost::chrono::duration_cast;
    using boost::chrono::seconds;
    using boost::chrono::milliseconds;
    using duration = typename Clock::duration;
    duration d = tp.time_since_epoch();
    std::time_t t = duration_cast<seconds>(d).count();
    struct tm *tm_ptr = std::localtime(&t);
    std::vector<char> buffer(50);
    size_t n = std::strftime(&buffer[0], buffer.size(), "%F %T %z", tm_ptr);
    if (n <= 0)
        throw std::runtime_error("strftime");
    std::ostringstream oss;
    oss << &buffer[0];
    oss << "." << std::setw(3) << std::setfill('0')
        << duration_cast<milliseconds>(d - duration_cast<seconds>(d)).count();
    return oss.str();
}

std::string timestamp() {
    return timestamp(std::chrono::system_clock::now());
}

} // namespace anonymous

#define LOGX(level, x) \
    do { \
        using boost::this_thread::get_id; \
        std::ostringstream oss; \
        oss << std::boolalpha; \
        oss << timestamp(); \
        oss << " " << level; \
        oss << " tid " << get_id(); \
        oss << " " << x; \
        oss << std::endl; \
        std::cout << oss.str(); \
    } while (false)

#define LOGD(x) LOGX("D", x)
#define LOGI(x) LOGX("I", x)
#define LOGW(x) LOGX("W", x)
#define LOGE(x) LOGX("E", x)

namespace {

class thread_pool {
    public:
        typedef boost::asio::io_service service_type;
        typedef boost::chrono::system_clock clock_type;
        typedef boost::asio::basic_waitable_timer<clock_type>
            waitable_timer_type;

        explicit
        thread_pool(
                int min_workers = 0,
                int max_workers = boost::thread::hardware_concurrency())
            : _M_received_stop_signal(false)
            , _M_min_workers(min_workers)
            , _M_max_workers(max_workers)
            , _M_service()
            , _M_thread_group()
            , _M_work()
        {
            start_internal();
        }

        virtual ~thread_pool() {
            stop_internal();
        }

        void shutdown() {
            stop_internal();
        }

        std::size_t get_min_workers() const { return _M_min_workers; }
        std::size_t get_max_workers() const { return _M_max_workers; }
        void set_min_workers(int value) { _M_min_workers = value; }
        void set_max_workers(int value) { _M_max_workers = value; }
        boost::asio::io_service& get_service() {
            return _M_service;
        }

        class time_point_sequencer {
            public:
                typedef clock_type::time_point time_point;
                typedef clock_type::duration duration;

                time_point_sequencer(
                        time_point const &start_tp,
                        duration const &delay,
                        duration const &interval)
                    : _M_delay(delay)
                    , _M_interval(interval)
                    , _M_next()
                {
                    _M_next = start_tp + _M_delay;
                }

                ~time_point_sequencer() {
                }

                time_point next() {
                    using boost::chrono::duration_cast;
                    using boost::chrono::seconds;
                    time_point tp = _M_next;
                    // LOGI("next timepoint is " << timestamp(tp));
                    _M_next += _M_interval;
                    return tp;
                }
            protected:
            private:
                duration _M_delay;
                clock_type::duration _M_interval;
                time_point _M_next;
        };

        template <
            typename Rep0,
            typename Period0,
            typename Rep1,
            typename Period1,
            typename F
                 >
        // boost::shared_ptr<waitable_timer_type>
        void
        fixed_delay(
                F &&fn,
                boost::chrono::duration<Rep0, Period0> const &d0,
                boost::chrono::duration<Rep1, Period1> const &d1) {
            time_point_sequencer seq(clock_type::now(), d0, d1);
            return fixed_delay<F>(boost::forward<F&&>(fn), seq);
        }

        template <typename F>
        // boost::shared_ptr<waitable_timer_type>
        void
        fixed_delay(F &&fn, time_point_sequencer seq) {
            // scoped shared_ptr destruction lead to schedule cancellation
            boost::async([]{});
            boost::promise<void> p;
            boost::shared_ptr<waitable_timer_type> wait_timer(
                    new waitable_timer_type(this->get_service()));
            wait_timer->expires_at(seq.next());
            wait_timer->async_wait(
                    [this, fn, wait_timer, seq]
                    (boost::system::error_code const &ec) mutable {
                        if (!ec) {
                            fixed_delay(fn, seq);
                            fn();
                        } else {
                            LOGI("ec = " << ec << ", " << ec.message());
                            if (ec == boost::asio::error::operation_aborted) {
                                wait_timer.reset();
                            }
                        }
                    });
            return;
        }
    protected:
        void start_internal() {
            _M_work = boost::shared_ptr<service_type::work>(new service_type::work(_M_service));
            while (_M_thread_group.size() < get_min_workers())
                _M_thread_group.create_thread([this]() {
                            // "this" maybe dangling after lifetime, unless
                            // threads are completed before destruction
                            LOGD("io_service.run()");
                            try {
                                size_t n = _M_service.run();
                                LOGD("io_service.run() = " << n);
                            } catch (boost::system::system_error const &e) {
                                LOGW("io_service.run system_error: " << e.code() << ", " << e.what());
                            } catch (std::exception const &e) {
                                LOGW("io_service.run exception: " << e.what());
                            } catch (boost::thread_interrupted const &e) {
                                if (!_M_received_stop_signal)
                                    LOGW("io_service.run boost thread interrupted: ");
                            } catch (...) {
                                LOGE("io_service.run excepted: " << "<UNKNOWN CAUSE>");
                                // throw;
                            }
                            LOGD("io_service.stopped() = " << _M_service.stopped());
                        });
        }

        void stop_internal() {
            _M_received_stop_signal = true;
            _M_work.reset();
            if (!_M_service.stopped())
                _M_service.stop();
            _M_thread_group.interrupt_all();
            _M_thread_group.join_all();
        }

    private:
        volatile bool _M_received_stop_signal;
        int _M_min_workers;
        int _M_max_workers;
        service_type _M_service;
        boost::thread_group _M_thread_group;
        boost::shared_ptr<service_type::work> _M_work;
};

class thread_pool2 {
    public:
        typedef boost::asio::io_service service_type;
        typedef boost::chrono::system_clock clock_type;
        typedef boost::asio::basic_waitable_timer<clock_type>
            waitable_timer_type;

        explicit
        thread_pool2(
                int min_workers = 0,
                int max_workers = boost::thread::hardware_concurrency())
            : _M_received_stop_signal(false)
            , _M_min_workers(min_workers)
            , _M_max_workers(max_workers)
            , _M_service()
            , _M_thread_group()
            , _M_work()
        {
            start_internal();
        }

        virtual ~thread_pool2() {
            stop_internal();
        }

        void shutdown() {
            stop_internal();
        }

        std::size_t get_min_workers() const { return _M_min_workers; }
        std::size_t get_max_workers() const { return _M_max_workers; }
        void set_min_workers(int value) { _M_min_workers = value; }
        void set_max_workers(int value) { _M_max_workers = value; }
        boost::asio::io_service& get_service() {
            return _M_service;
        }

        class time_point_sequencer {
            public:
                typedef clock_type::time_point time_point;
                typedef clock_type::duration duration;

                time_point_sequencer(
                        time_point const &start_tp,
                        duration const &delay,
                        duration const &interval)
                    : _M_delay(delay)
                    , _M_interval(interval)
                    , _M_next()
                {
                    _M_next = start_tp + _M_delay;
                }

                ~time_point_sequencer() {
                }

                time_point next() {
                    using boost::chrono::duration_cast;
                    using boost::chrono::seconds;
                    time_point tp = _M_next;
                    // LOGI("next timepoint is " << timestamp(tp));
                    _M_next += _M_interval;
                    return tp;
                }
            protected:
            private:
                duration _M_delay;
                clock_type::duration _M_interval;
                time_point _M_next;
        };

        template <
            typename Rep0,
            typename Period0,
            typename Rep1,
            typename Period1,
            typename F
                 >
        // boost::shared_ptr<waitable_timer_type>
        void
        fixed_delay(
                F &&fn,
                boost::chrono::duration<Rep0, Period0> const &d0,
                boost::chrono::duration<Rep1, Period1> const &d1) {
            time_point_sequencer seq(clock_type::now(), d0, d1);
            return fixed_delay<F>(boost::forward<F&&>(fn), seq);
        }

        template <typename F>
        // boost::shared_ptr<waitable_timer_type>
        void
        fixed_delay(F &&fn, time_point_sequencer seq) {
            // scoped shared_ptr destruction lead to schedule cancellation
            boost::shared_ptr<waitable_timer_type> wait_timer(
                    new waitable_timer_type(this->get_service()));
            wait_timer->expires_at(seq.next());
            wait_timer->async_wait(
                    [this, &fn, wait_timer, seq]
                    (boost::system::error_code const &ec) {
                        if (!ec) {
                            fixed_delay(fn, seq);
                            fn();
                        } else {
                            LOGI("ec = " << ec << ", " << ec.message());
                            if (ec == boost::asio::error::operation_aborted) {
                            }
                        }
                    });
            return;
        }
    protected:
        void start_internal() {
            _M_work = boost::shared_ptr<service_type::work>(new service_type::work(_M_service));
            while (_M_thread_group.size() < get_min_workers())
                _M_thread_group.create_thread([this]() {
                            // "this" maybe dangling after lifetime, unless
                            // threads are completed before destruction
                            LOGD("io_service.run()");
                            try {
                                size_t n = _M_service.run();
                                LOGD("io_service.run() = " << n);
                            } catch (boost::system::system_error const &e) {
                                LOGW("io_service.run system_error: " << e.code() << ", " << e.what());
                            } catch (std::exception const &e) {
                                LOGW("io_service.run exception: " << e.what());
                            } catch (boost::thread_interrupted const &e) {
                                if (!_M_received_stop_signal)
                                    LOGW("io_service.run boost thread interrupted: ");
                            } catch (...) {
                                LOGE("io_service.run excepted: " << "<UNKNOWN CAUSE>");
                                // throw;
                            }
                            LOGD("io_service.stopped() = " << _M_service.stopped());
                        });
        }

        void stop_internal() {
            _M_received_stop_signal = true;
            _M_work.reset();
            if (!_M_service.stopped())
                _M_service.stop();
            _M_thread_group.interrupt_all();
            _M_thread_group.join_all();
        }

    private:
        volatile bool _M_received_stop_signal;
        int _M_min_workers;
        int _M_max_workers;
        service_type _M_service;
        boost::thread_group _M_thread_group;
        boost::shared_ptr<service_type::work> _M_work;
};

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
    thread_pool p(2);
    boost::shared_ptr<foo> ptr_to_f(new foo("outside-lambda"));
    p.fixed_delay([&counter, ptr_to_f]() {
                foo f2("inside-lambda");
                // sleep_for(milliseconds(10));
                LOGI("hello " << __PRETTY_FUNCTION__);
                ++counter;
            },
            milliseconds(0),
            milliseconds(1)
            );
    ptr_to_f.reset();
    boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
    p.shutdown();
    }
    LOGI("counter = " << counter);
    ASSERT_TRUE(counter <= 1000 / 1 + 1 && counter >= 1000 / 1 );
}

TEST(boost_async, test_prior_queue) {
    typedef std::less_equal<int> lte;
    typedef std::binary_negate<lte> gt;
    typedef std::priority_queue<int, std::deque<int>, gt> pq;
    pq q{gt(lte())};
    std::default_random_engine rnd_engine;
    std::uniform_int_distribution<int> dist(1, 10);
    for (int i = 0, n = 50; i < n; ++i) {
        int val = dist(rnd_engine);
        // LOGI("random number => " << val);
        q.push(val);
    }
    std::vector<int> sorted;
    for (; !q.empty(); q.pop())  {
        int val = q.top();
        sorted.push_back(val);
        // LOGI("pop from priority_queue -> " << val);
    }
    ASSERT_TRUE(std::is_sorted(sorted.begin(), sorted.end()));
}

TEST(boost_async, test_run_without_work) {
    typedef boost::chrono::system_clock clock_type;
    typedef clock_type::time_point time_point_type;
    using boost::chrono::duration_cast;
    using boost::chrono::milliseconds;
    using boost::this_thread::sleep_for;
    using boost::this_thread::get_id;
    boost::asio::io_service srv;
    boost::thread_group tg;
    time_point_type tp1 = clock_type::now();
    time_point_type tp2;
    tg.create_thread([&]{
                srv.run();
                tp2 = clock_type::now();
            });
    sleep_for(milliseconds(100));
    tg.join_all();
    LOGI(duration_cast<milliseconds>(tp2 - tp1).count() << " ms");
    ASSERT_LT(tp2 - tp1, milliseconds(10));
}

TEST(boost_async, test_run_with_work) {
    typedef boost::chrono::system_clock clock_type;
    typedef clock_type::time_point time_point_type;
    using boost::chrono::duration_cast;
    using boost::chrono::milliseconds;
    boost::asio::io_service srv;
    boost::thread_group tg;
    time_point_type tp1 = clock_type::now();
    time_point_type tp2;
    {
    boost::asio::io_service::work w(srv);
    tg.create_thread([&]{
                size_t n = srv.run();
                tp2 = clock_type::now();
                LOGI("srv.run() = " << n);
            });
    boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
    }
    tg.join_all();
    LOGI(duration_cast<milliseconds>(tp2 - tp1).count() << " ms");
    ASSERT_GE(tp2 - tp1, milliseconds(100));
}
