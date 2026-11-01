#pragma once
#include <stddef.h>
extern "C" int mbedtls_md5_ret(const unsigned char *input, size_t length, unsigned char output[16]);
