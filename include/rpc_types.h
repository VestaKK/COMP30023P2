#ifndef RPC_TYPES_H
#define RPC_TYPES_H

#include "defines.h"

typedef uint8_t rpc_message;
typedef uint8_t rpc_data_flags;
typedef uint8_t rpc_error;

typedef struct hw_profile {
    int64_t int_max;
    int64_t int_min;
    uint64_t size_max;
    bool initialised;
} hw_profile;

enum RPC_MESSAGE {
    RPC_MSG_CONNECT = 0xCC,
    RPC_MSG_FUNC_FIND = 0xFF,
    RPC_MSG_FUNC_CALL = 0xFC,
    RPC_MSG_DISCONNECT = 0xDC,
    RPC_MSG_END = 0xED,
    RPC_RTN_SUCCESS = 0x55,
    RPC_RTN_ERROR = 0xEE,
};

enum RPC_DATA_FLAG {
    RPC_DATA_NONE = 0x0,
    RPC_DATA_INT = 0x1,
    RPC_DATA_BUFF = 0x80,
};

enum RPC_ERROR {
    RPC_ERROR_NONE = 0x0,
    RPC_ERROR_CXN_INVALID = 0x1,
    RPC_ERROR_FUNC_NOT_FOUND = 0x2,
    RPC_ERROR_DATA_INT_OVF = 0x4,
    RPC_ERROR_DATA_BUFF_OVF = 0x8,
    RPC_ERROR_DATA_INVALID = 0x10,
    RPC_ERROR_HNDL_INVALID = 0x20,
    RPC_ERROR_MSG_INVALID = 0x40,
    RPC_ERROR_PQT_INVALID = 0x80,
};

#endif 