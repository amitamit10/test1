#pragma once
#include <cstdio>
#include <ctime>
#include <cstring>

#define BAMBU_LOG(level, ...) do { \
    time_t _t = time(nullptr); \
    char _ts[20]; \
    struct tm _tm; \
    localtime_r(&_t, &_tm); \
    strftime(_ts, sizeof(_ts), "%H:%M:%S", &_tm); \
    fprintf(stderr, "[%s][bambu][%s] ", _ts, level); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
    fflush(stderr); \
} while(0)

#define LOG_INFO(...) BAMBU_LOG("INFO", __VA_ARGS__)
#define LOG_ERR(...)  BAMBU_LOG("ERR ", __VA_ARGS__)
#define LOG_DBG(...)  BAMBU_LOG("DBG ", __VA_ARGS__)
