#define unlikely(expr) __builtin_expect(!!(expr), 0)
#define likely(expr) __builtin_expect(!!(expr), 1)

#define unlikely_exec(expr, function)                                          \
    ({                                                                         \
        bool _err = unlikely(expr);                                            \
        if (_err) {                                                            \
            function;                                                          \
        }                                                                      \
        _err;                                                                  \
    })

#define likely_exec(expr, function)                                            \
    ({                                                                         \
        bool _err = likely(expr);                                              \
        if (_err) {                                                            \
            function;                                                          \
        }                                                                      \
        _err;                                                                  \
    })

#define unlikely_err(expr, msg) unlikely_exec(expr, log_err(msg))
#define unlikely_warn(expr, msg) unlikely_exec(expr, log_warn(msg))