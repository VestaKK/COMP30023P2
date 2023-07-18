#ifndef HELPER_C
#define HELPER_C

#include "defines.h"
#include "rpc.h"
#include "rpc_types.h"

/**
 * This header contains many miscellaneous functions and macros that make coding a whole 
 * lot easier. Also contains functions like int_to_string that are simply there so I never 
 * have to be reminded that getaddrinfo() uses a string to process the port (Like why???). 
 * */

// 64-bit equivalents to htons/htonl and ntohs/ntohl 
#define hton64(host64) htobe64(host64)
#define ntoh64(net64) be64toh(net64) 

// For calculating interger limits
#define MAX_SINT(nbytes) (1ULL << (8*nbytes - 1)) - 1
#define MAX_UINT(nbytes) nbytes < 8 ? (1ULL << 8*nbytes) - 1 : UINT64_MAX

// Compressed conditional macros
#define valid_port(port) (0 < port && port <= UINT16_MAX)
#define quick_check(func) if (!func) return false

/**
 * @brief
 * Checks if each character is between 32 && 132,
 * checks if the length of the string is less than UINT16_MAX
 * @param name Null-terminated string
*/
bool is_valid_name(const char* name);

/**
 * @brief
 * Converts an integer into a heap-allocated string. Ensure to free this after
 * you finish using the string.
*/
char* int_to_string(int integer);

// Wrapper around recv()
// Returns whether or not this procedure was succesful
bool socket_recv(int fd, void* buff, size_t nbytes);

// Wrapper around send()
// Returns whether or not this procedure was succesful
bool socket_send(int fd, void* buff, size_t nbytes);

// Generates information related to the given data
rpc_data_flags gen_data_flags(rpc_data* data);

// Reads in an rpc_data into heap-allocated memory
// Returns whether or not this procedure was succesful
// if *output is NULL, this function has failed terribly
bool socket_recv_data(int fd, rpc_data** output);

// Sends in an rpc_data through the given socket
// Returns whether or not this procedure was succesful
bool socket_send_data(int fd, rpc_data* input);

// Scans the data for any possible issues
rpc_error check_data(hw_profile* profile, rpc_data* data);

#endif