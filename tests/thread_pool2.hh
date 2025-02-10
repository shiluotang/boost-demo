#ifndef THREAD_POOL2_HH_INCLUDED
#define THREAD_POOL2_HH_INCLUDED

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/system/system_error.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
// #include <boost/asio/steady_timer.hpp>
// #include <boost/asio/system_timer.hpp>
// #include <boost/asio/high_resolution_timer.hpp>
#include <boost/bind/placeholders.hpp>

#include "logging.hh"

namespace {

class thread_pool2 {
    public:
        typedef boost::asio::io_service service_type;
        /// use xxx_timer::clock_type will introduce "std", "boost" chaos.
        // boost::asio::steady_timer
        // boost::asio::system_timer
        // boost::asio::high_resolution_timer
        typedef boost::chrono::system_clock clock_type;
        typedef boost::asio::basic_waitable_timer<clock_type> timer_type;

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
            LOG_FUNC_ENTRY();
            start_internal();
        }

        virtual ~thread_pool2() {
            LOG_FUNC_ENTRY();
            stop_internal();
        }

        void shutdown() {
            LOG_FUNC_ENTRY();
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
                    time_point tp = _M_next;
                    _M_next += _M_interval;
                    return tp;
                }
            protected:
            private:
                duration _M_delay;
                clock_type::duration _M_interval;
                time_point _M_next;
        };

        struct runner_group;
        struct runner
            : public boost::enable_shared_from_this<runner> {
            typedef boost::function<void(boost::system::error_code const &e)>
                handler_type;
            typedef boost::function<void()> function_type;

            runner(
                    runner_group &group,
                    boost::asio::io_service &srv,
                    time_point_sequencer const &seq,
                    function_type &&f)
                : _M_group(group)
                , _M_service(srv)
                , _M_wait_timer(new timer_type(srv))
                , _M_seq(seq)
                , _M_timeout_handler()
                // FIXME is move neccessary?
                , _M_func(boost::forward<function_type&&>(f))
            {
            }

            ~runner() {
                LOG_FUNC_ENTRY();
                this->stop(false);
            }

            void start() {
#if BOOST_VERSION > 105300
                using boost::placeholders::_1;
#endif
                LOG_FUNC_ENTRY();
                if (!_M_wait_timer)
                    _M_wait_timer = boost::shared_ptr<timer_type>(
                            new timer_type(_M_service));
                LOGD("self = this->shared_from_this()");
                boost::shared_ptr<runner> self = this->shared_from_this();
                boost::weak_ptr<runner> wself(self);
                LOGD("self.use_count() = " << self.use_count());
                if (!_M_timeout_handler) {
                    LOGD("_M_timeout_handler = boost::bind(...)");
                    _M_timeout_handler = boost::bind(
                            &on_timer_expire,
                            _1,
                            wself);
                    LOGD("self.use_count() = " << self.use_count());
                }
                time_point_sequencer::time_point tp = _M_seq.next();
                LOGD("expires_at(" << timestamp(tp) << ")");
                _M_wait_timer->expires_at(tp);
                LOGD("_M_wait_timer->async_wait(_M_timeout_handler)");
                _M_wait_timer->async_wait(_M_timeout_handler);
                LOGD("self.use_count() = " << self.use_count());
            }

            void stop(bool remove_from_group = true) {
                LOG_FUNC_ENTRY();
                if (!!_M_wait_timer) {
                    LOGD("_M_wait_timer->cancel()");
                    _M_wait_timer->cancel();
                    LOGD("_M_wait_timer.reset()");
                    _M_wait_timer.reset();
                }
                if (!!_M_timeout_handler) {
                    LOGD("_M_timeout_handler = 0");
                    _M_timeout_handler = 0;
                }
                if (!!_M_func) {
                    LOGD("_M_func = 0");
                    _M_func = 0;
                }
                if (remove_from_group) {
                    LOGD("_M_group.erase(this->shared_from_this())");
                    _M_group.erase(this->shared_from_this());
                }
            }

            operator bool() const { return !!_M_func; }

            void do_run() {
                // calculate next round time point.
                time_point_sequencer::time_point tp = _M_seq.next();
                if (!!_M_func) {
                    _M_func();
                    // schedule next round.
                    _M_wait_timer->expires_at(tp);
                    _M_wait_timer->async_wait(_M_timeout_handler);
                }
            }

            static
            void on_timer_expire(
                    boost::system::error_code const &ec,
                    boost::weak_ptr<runner> r) {
                boost::shared_ptr<runner> ref = r.lock();
                if (!ref)
                    return;
                if (!!ec) {
                    LOGW("ec = " << ec << ", " << ec.message());
                    ref->stop();
                    return;
                }
                ref->do_run();
            }

            runner_group &_M_group;
            boost::asio::io_service &_M_service;
            boost::shared_ptr<timer_type> _M_wait_timer;
            time_point_sequencer _M_seq;
            handler_type _M_timeout_handler;
            function_type _M_func;
        };

        struct runner_group {
            runner_group()
                : _M_mutex()
                , _M_runners()
            {
                LOG_FUNC_ENTRY();
            }

            ~runner_group() {
                LOG_FUNC_ENTRY();
            }

            void push_back(boost::shared_ptr<runner> r) {
                boost::lock_guard<boost::mutex> guard(_M_mutex);
                _M_runners.push_back(r);
            }

            bool erase(boost::shared_ptr<runner> r) {
                boost::lock_guard<boost::mutex> guard(_M_mutex);
                auto it = std::find(_M_runners.begin(), _M_runners.end(), r);
                if (it == _M_runners.end())
                    return false;
                _M_runners.erase(it);
                return true;
            }

            void clear() {
                boost::lock_guard<boost::mutex> guard(_M_mutex);
                _M_runners.clear();
            }

            boost::mutex _M_mutex;
            std::vector<boost::shared_ptr<runner> > _M_runners;
        };

        template <
            typename Duration0,
            typename Duration1,
            typename F
                 >
        // boost::shared_ptr<timer_type>
        void
        fixed_rate(
                F &&fn,
                Duration0 const &d0,
                Duration1 const &d1) {
            using boost::chrono::duration_cast;
            time_point_sequencer seq(clock_type::now(), d0, d1);
            return fixed_rate<F>(boost::forward<F&&>(fn), seq);
        }


        template <typename F>
        // boost::shared_ptr<timer_type>
        void
        fixed_rate(F &&fn, time_point_sequencer seq) {
            boost::shared_ptr<runner> r(new runner(
                        _M_runner_group,
                        _M_service,
                        seq,
                        std::forward<F&&>(fn)));
            LOGD("r.use_count() = " << r.use_count());
            _M_runner_group.push_back(r);
            r->start();
            LOGD("r.use_count() = " << r.use_count());
        }
    protected:
        void start_internal() {
            LOG_FUNC_ENTRY();
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
            LOG_FUNC_ENTRY();
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
        runner_group _M_runner_group;
};

} // namespace anonymous

#endif // THREAD_POOL2_HH_INCLUDED
