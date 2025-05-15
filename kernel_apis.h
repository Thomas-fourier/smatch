#ifndef KERNEL_APIS_H
#define KERNEL_APIS_H
static struct kernel_api_func {
    const char *api_name;
    const char *api_file;
    const char *api_field;
    const char *api_func;
} kernel_api_funcs[] = {
#if __has_include("kernel_apis.inc.h")
#include "kernel_apis.inc.h"
#endif
};
#endif