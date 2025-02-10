#ifndef LOGGING_HH_INCLUDED
#define LOGGING_HH_INCLUDED

#include <iomanip>
#include <iostream>
#include <string>

#include <chrono>
#include <boost/chrono.hpp>
#include <boost/thread.hpp>

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
    std::ostringstream oss;
    oss << std::put_time(tm_ptr, "%F %T");
    oss << "." << std::setw(3) << std::setfill('0')
        << duration_cast<milliseconds>(d - duration_cast<seconds>(d)).count();
    oss << " " << std::put_time(tm_ptr, "%z");
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
    std::ostringstream oss;
    oss << std::put_time(tm_ptr, "%F %T");
    oss << "." << std::setw(3) << std::setfill('0')
        << duration_cast<milliseconds>(d - duration_cast<seconds>(d)).count();
    oss << " " << std::put_time(tm_ptr, "%z");
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
struct scope_logging {
    public:
        scope_logging(std::string const &name)
            :_M_name(name)
        {
            LOGD(">>> " << _M_name);
        }

        ~scope_logging() {
            LOGD("<<< " << _M_name);
        }
    protected:
    private:
        std::string _M_name;
};
} // namespace anonymous

#define LOG_SCOPE(name, sname) scope_logging ___scope_of_##name(sname)
#define LOG_FUNC_ENTRY() LOG_SCOPE(__FUNCTION__, __PRETTY_FUNCTION__)

#endif // LOGGING_HH_INCLUDED
