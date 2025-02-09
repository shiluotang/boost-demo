#ifndef THREAD_POOL_HH_INCLUDED
#define THREAD_POOL_HH_INCLUDED

#include <boost/asio.hpp>
#include <boost/chrono.hpp>
#include <boost/thread.hpp>
#include <boost/system/system_error.hpp>

#include "logging.hh"

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
            typename Duration0,
            typename Duration1,
            typename F
                 >
        // boost::shared_ptr<waitable_timer_type>
        void
        fixed_rate(
                F &&fn,
                Duration0 const &d0,
                Duration1 const &d1) {
            time_point_sequencer seq(clock_type::now(), d0, d1);
            return fixed_rate<F>(boost::forward<F&&>(fn), seq);
        }

        template <typename F>
        // boost::shared_ptr<waitable_timer_type>
        void
        fixed_rate(F &&fn, time_point_sequencer seq) {
            // scoped shared_ptr destruction lead to schedule cancellation
            boost::shared_ptr<waitable_timer_type> wait_timer(
                    new waitable_timer_type(this->get_service()));
            wait_timer->expires_at(seq.next());
            wait_timer->async_wait(
                    [this, fn, wait_timer, seq]
                    (boost::system::error_code const &ec) mutable {
                        if (!ec) {
                            fixed_rate(fn, seq);
                            fn();
                        } else {
                            LOGI("ec = " << ec << ", " << ec.message());
                            if (ec == boost::asio::error::operation_aborted) {
                                // require mutable lambda, otherwise wait_timer
                                // is captured as const
                                wait_timer.reset();
                            }
                        }
                    });
            return;
        }
    protected:
        void start_internal() {
            _M_work = boost::shared_ptr<service_type::work>(
                    new service_type::work(_M_service));
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

} // namespace anonymous

#endif // THREAD_POOL_HH_INCLUDED
