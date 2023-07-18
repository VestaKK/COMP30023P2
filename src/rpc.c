#define NONBLOCKING

#include "defines.h"
#include "rpc.h"
#include "rpc_types.h"
#include "helper.h"
#include "hashtable.h"
#include "linked_list.h"

#include <unistd.h>
#include <endian.h>
#include <sys/socket.h>
#include <pthread.h>

#define THREAD_POOL_SIZE 10
#define SOCKET_BACKLOG 10
#define SOCKET_NULL_HANDLE -1

// Thread related functions
static void* thread_work(void* arg);
static void handle_client(int clientfd, rpc_server* srv);

// Each of these functions are simply a wrapper for socket reading/writing logic,
// They return true if the one side did not disconnect from the other for the duration of the 
// function. Otherwise they will return false and the machine is expected to close the given
// socket

// Functions called by server
static bool svr_handle_msg_connect(int clientfd, hw_profile* cl_profile);
static bool svr_handle_msg_find(int clientfd, hw_profile* cl_profile, hash_table* ht_fnc);
static bool svr_handle_msg_call(int clientfd, hw_profile* cl_profile, hash_table* ht_fnc);
static bool svr_handle_rtn_error(int clientfd, rpc_error error);

// Functions called by client
static bool cl_handle_proc_connect(int serverfd, hw_profile* svr_profile);
static bool cl_handle_proc_find(int serverfd, char* char_buff, uint16_t length, rpc_handle** output);
static bool cl_handle_proc_call(int serverfd, rpc_handle* handle, rpc_data* input, rpc_data** output);
static bool cl_handle_rtn_error(int serverfd);
static void cl_print_rtn_error(rpc_error error);

// Standardised destroy functions for client and server
static void rpc_destroy_server(rpc_server* srv);
static void rpc_destroy_client(rpc_client* cl);

struct rpc_server {
    hash_table* hash_table;
    list* list_fd;
    pthread_cond_t client_cond;
    pthread_mutex_t mutex_list_fd;
    int masterfd;
};

rpc_server* rpc_init_server(int port) {

    if (!valid_port(port)) 
        return NULL;

    // Allocate set server data to default
    rpc_server* new_srv = calloc(1, sizeof(rpc_server));
    new_srv->hash_table = ht_create();
    new_srv->list_fd = list_create(true);
    pthread_cond_init(&new_srv->client_cond, NULL);
    pthread_mutex_init(&new_srv->mutex_list_fd, NULL);
    new_srv->masterfd = SOCKET_NULL_HANDLE;

    // Generate information about local machine
    char* port_string = int_to_string(port);
    struct addrinfo* svr_info = NULL;
    struct addrinfo hints = {
        .ai_family = AF_INET6,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_PASSIVE,
    };
    
    int ai_error;
    if ((ai_error = getaddrinfo(NULL, port_string, &hints, &svr_info)) != 0) {
        FREE(port_string);
        rpc_destroy_server(new_srv);
        fprintf(stderr, "[ERROR]: %s\n", gai_strerror(ai_error));
        return NULL;
    }
    FREE(port_string);
    
    // Iterate and find suitable socket info
    struct addrinfo* chosen_info = NULL;
    for (struct addrinfo* curr_info = svr_info; curr_info!=NULL; curr_info = curr_info->ai_next) {

        // Must be IPv6
        if (curr_info->ai_family != hints.ai_family) 
            continue;

        // Check if this info allows us to socket
        if ((new_srv->masterfd = socket(
                                    curr_info->ai_family, 
                                    curr_info->ai_socktype, 
                                    curr_info->ai_protocol)) >= 0) 
            {
            chosen_info = curr_info;
            break;
        }
    }

    // Check if we failed to socket
    if (new_srv->masterfd < 0) {
        rpc_destroy_server(new_srv);
        perror("socket() failed!\n");
        return NULL;
    }

    // Setup socket options
    int sockopt_error;
    int opt_val = true;
    if ((sockopt_error = setsockopt(
                            new_srv->masterfd, 
                            SOL_SOCKET, 
                            SO_REUSEADDR, 
                            &opt_val, sizeof(opt_val))) < 0) 
        {
        perror("setsocketopt() failed!\n");
        return NULL;
    }

    // Create half-socket
    int bind_error;
    if ((bind_error = bind(
                        new_srv->masterfd, 
                        chosen_info->ai_addr, 
                        chosen_info->ai_addrlen)) < 0) 
        {
        perror("bind() failed!\n");
        return NULL;
    }

    // Start listening for clients
    if (listen(new_srv->masterfd, SOCKET_BACKLOG) == -1) {
        perror("listen() failed!\n");
        return NULL;
    }

    // Remember to free the linked list
    freeaddrinfo(svr_info);

    // Return server to user
    return new_srv;
}

int rpc_register(rpc_server* srv, char* name, rpc_handler handler) {
    if (srv == NULL || name == NULL || handler == NULL)
        return -1;

    // Check for valid name
    if (!is_valid_name(name))
        return -1;
    
    // Otherwise create name handler pair in hashtable
    ht_insert(srv->hash_table, name, handler);
    return 1;
}

void rpc_serve_all(rpc_server* srv) {
    if (srv == NULL)
        return;

    // Initialise thread pool
    pthread_t thread_pool[THREAD_POOL_SIZE];
    for (int i=0; i<THREAD_POOL_SIZE; i++) {
        pthread_create(&thread_pool[i], NULL, thread_work, srv);
        pthread_detach(thread_pool[i]);
    }

    while(true) {
        int new_clientfd = SOCKET_NULL_HANDLE;
        struct sockaddr_in cl_addr;
        socklen_t cl_addr_len = sizeof(cl_addr);

        // Accept new connections when they come
        if (((new_clientfd = accept(
                                srv->masterfd, 
                                &cl_addr, 
                                &cl_addr_len)) < 0)) 
            {
            perror("accept() failed!\n");
            break;
        }

        // Lock the list of clients so we can add a new client
        pthread_mutex_lock(&srv->mutex_list_fd);
        int* temp = malloc(sizeof(int));
        *temp = new_clientfd;
        list_insert_tail(srv->list_fd, temp);
        pthread_cond_signal(&srv->client_cond);
        pthread_mutex_unlock(&srv->mutex_list_fd);
    }
}

struct rpc_client {
    int serverfd;
    hw_profile srv_profile;
    bool is_active;
};

struct rpc_handle {
    uint64_t hash_value;
};

rpc_client* rpc_init_client(char* addr, int port) {
    if (addr == NULL || !valid_port(port)) 
        return NULL;

    // Allocate and initialise memory for a new client
    rpc_client* new_cl = calloc(1, sizeof(rpc_client));
    new_cl->serverfd = SOCKET_NULL_HANDLE;
    new_cl->is_active = true;

    // Generate information about local machine
    char* port_string = int_to_string(port);
    struct addrinfo* svr_info = NULL; 
    struct addrinfo hints = {
        .ai_family = AF_INET6,
        .ai_socktype = SOCK_STREAM,
    };

    int ai_error;
    if ((ai_error = getaddrinfo(addr, port_string, &hints, &svr_info)) != 0) {
        fprintf(stderr, "%s\n", gai_strerror(ai_error));
        return NULL;
    }
    FREE(port_string);

    // Iterate through results to find correct addrinfo and create socket
    for (struct addrinfo* curr_info = svr_info; curr_info != NULL; curr_info = curr_info->ai_next) {
        
        // Must be IPv6
        if (curr_info->ai_family != hints.ai_family) 
            continue;

        // If we can't socket we go to the next item in the list
        if ((new_cl->serverfd = socket(
                                    curr_info->ai_family, 
                                    curr_info->ai_socktype, 
                                    curr_info->ai_protocol)) < 0) 
            {
            continue;
        }

        // Try to connect to the server
        if (connect(
                new_cl->serverfd, 
                curr_info->ai_addr, 
                curr_info->ai_addrlen) != -1) 
            {
            break;
        }

        // Go to next info if we can't connect
        close(new_cl->serverfd);
        new_cl->serverfd = SOCKET_NULL_HANDLE;
    }
    freeaddrinfo(svr_info);   

    // We couldn't connect to the server for some reason
    if (new_cl->serverfd == SOCKET_NULL_HANDLE) {
        rpc_destroy_client(new_cl);
        perror("connect() failed!\n");
        return NULL;
    }

    // Check that connection to server was successful
    if (!cl_handle_proc_connect(new_cl->serverfd, &new_cl->srv_profile)) {
        rpc_destroy_client(new_cl);
        return NULL;
    }

    // Otherwise we have successfully connected to the server
    return new_cl;
}

rpc_handle* rpc_find(rpc_client* cl, char* name) {
    if (cl == NULL || name == NULL)
        return NULL;

    // Check that the client is active
    if (!cl->is_active)
        return NULL;

    // Comply with protocol
    if (strlen(name) > UINT16_MAX) {
        fprintf(stderr, "Name was too long!\n");
        return NULL;
    }

    rpc_handle* handle = NULL;

    // Check that communication with server didn't cut
    if (!cl_handle_proc_find(cl->serverfd, name, strlen(name), &handle))
        return NULL;

    // Handle will be null if the procedure fails, otherwise
    // it will be a heap allocated address to an rpc_handle
    return handle;
}

rpc_data* rpc_call(rpc_client* cl, rpc_handle* h, rpc_data* payload) {
    if (cl == NULL || h == NULL || payload == NULL)
        return NULL; 

    // Check that the client is active
    if (!cl->is_active)
        return NULL;

    // Comply with protocol
    // Check that the data will not overflow on the server
    rpc_error error = check_data(&cl->srv_profile, payload);
    if (error) {

        if (error & RPC_ERROR_DATA_INT_OVF)
            fprintf(stderr, "Payload.data1 value too large for server!\n");

        if (error & RPC_ERROR_DATA_BUFF_OVF)
            fprintf(stderr, "Payload.data2 contains too much data for the server!\n");

        if (error & RPC_ERROR_DATA_INVALID)
            fprintf(stderr, "Payload is invalid!\n");

        return NULL;
    }
    
    // Check that communication with the server did not cut
    rpc_data* output = NULL;
    if (!cl_handle_proc_call(cl->serverfd, h, payload, &output))
        return NULL;

    // Output will be NULL if the procedure fails, otherwise
    // it will be a heap allocated address to an rpc_data
    return output;
}

void rpc_close_client(rpc_client* cl) {
    if (cl == NULL)
        return;

    if (!cl->is_active)
        return;

    // We don't care about whether or not the server has disconnected
    // orderly or disorderly we just have to send the message
    // to follow the protocol
    rpc_message message = RPC_MSG_DISCONNECT;
    socket_send(cl->serverfd, &message, sizeof(rpc_message));
    rpc_destroy_client(cl);
}

void rpc_data_free(rpc_data* data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}

// Static functions

static void* thread_work(void* arg) {

    rpc_server* srv = arg;

    while(true) {

        // Every loop the thread processes a new client
        int clientfd;

        pthread_mutex_lock(&srv->mutex_list_fd);

        // Thread waits for main thread to add new clients
        if (srv->list_fd->head == NULL)
            pthread_cond_wait(&srv->client_cond, &srv->mutex_list_fd);  

        // Make sure to dequeue client from the list  
        clientfd = *(int*)srv->list_fd->head->data;
        list_pop_head(srv->list_fd);

        pthread_mutex_unlock(&srv->mutex_list_fd);

        // Handle the client for an indeterminant amound of time
        handle_client(clientfd, srv);

        // Close the socket
        close(clientfd);
    }

    return NULL;
}

static void handle_client(int clientfd, rpc_server* srv) {

    bool is_connected = true;

    hw_profile cl_profile = {
        .int_max = 0,
        .int_min = 0,
        .size_max = 0,
        .initialised = false,
    };

    while(is_connected) {
        rpc_message message = 0;

        // Try to read in the message
        if (!socket_recv(clientfd, &message, sizeof(rpc_message)))
            break;

        // Handle the message
        switch(message) {
            case RPC_MSG_CONNECT:
                is_connected = svr_handle_msg_connect(clientfd, &cl_profile);
                break;
            case RPC_MSG_FUNC_FIND:
                is_connected = svr_handle_msg_find(clientfd, &cl_profile, srv->hash_table);
                break;
            case RPC_MSG_FUNC_CALL:
                is_connected = svr_handle_msg_call(clientfd,&cl_profile, srv->hash_table);
                break;
            case RPC_MSG_DISCONNECT:
                is_connected = false;
                break;
            default:
                is_connected = svr_handle_rtn_error(clientfd, RPC_ERROR_MSG_INVALID);
                break;
        }
    }
}

static bool svr_handle_msg_connect(int clientfd, hw_profile* cl_profile) {
    if (cl_profile == NULL)
        return true;

    // Read in int_max of client
    uint8_t sizeof_int_cl;
    quick_check(socket_recv(clientfd, &sizeof_int_cl, sizeof(uint8_t)));
    cl_profile->int_max = MAX_SINT(sizeof_int_cl);
    cl_profile->int_min = -cl_profile->int_max - 1;

    // Read in size_max of client
    uint8_t sizeof_size_t_cl;
    quick_check(socket_recv(clientfd, &sizeof_size_t_cl, sizeof(uint8_t)));
    cl_profile->size_max = MAX_UINT(sizeof_size_t_cl);

    // Check that the client has ended the packet at this point
    rpc_message cl_msg_end;
    quick_check(socket_recv(clientfd, &cl_msg_end, sizeof(uint8_t)));
    if (cl_msg_end != RPC_MSG_END)
        return svr_handle_rtn_error(clientfd, RPC_ERROR_PQT_INVALID);

    // Client has followed the connection procedure
    cl_profile->initialised = true;

    // Send success message to client
    rpc_message message = RPC_RTN_SUCCESS;
    quick_check(socket_send(clientfd, &message, sizeof(rpc_message)));

    // Write out int_max of the server
    uint8_t sizeof_int_srv = sizeof(int);
    quick_check(socket_send(clientfd, &sizeof_int_srv, sizeof(uint8_t)));

    // Write out size_max of the server
    uint8_t sizeof_size_t_srv = sizeof(size_t);
    quick_check(socket_send(clientfd, &sizeof_size_t_srv, sizeof(uint8_t)));

    // End message
    rpc_message svr_msg_end = RPC_MSG_END;
    quick_check(socket_send(clientfd, &svr_msg_end, sizeof(rpc_message)));

    return true;
}

static bool svr_handle_msg_find(int clientfd, hw_profile* cl_profile, hash_table* ht_fnc) {
    if (cl_profile == NULL || ht_fnc == NULL)
        return true;

    // Make sure the client has initialised the connection properly
    if (!cl_profile->initialised)
        return svr_handle_rtn_error(clientfd, RPC_ERROR_CXN_INVALID);

    // Read in length of function name
    uint16_t be_len_name;
    quick_check(socket_recv(clientfd, &be_len_name, sizeof(uint16_t)));
    uint16_t len_name = ntohs(be_len_name);

    // Read in char_buffer using length
    char* name = malloc(len_name + 1);
    if(!socket_recv(clientfd, name, len_name)) {
        FREE(name);
        return false;
    }
    name[len_name] = 0;

    // Validate client packet
    rpc_message cl_msg_end;
    if(!socket_recv(clientfd, &cl_msg_end, sizeof(uint8_t))) {
        FREE(name);
        return false;
    }
    if (cl_msg_end != RPC_MSG_END)
        return svr_handle_rtn_error(clientfd, RPC_ERROR_PQT_INVALID);

    // Attempt to find handler using name
    rpc_handler handler = ht_index(ht_fnc, name);
    FREE(name);

    // Check if the handler exists
    if (handler == NULL)
        return svr_handle_rtn_error(clientfd, RPC_ERROR_FUNC_NOT_FOUND);

    // Send success message
    rpc_message message = RPC_RTN_SUCCESS;
    quick_check(socket_send(clientfd, &message, sizeof(rpc_message)));

    // Send function hash value
    uint64_t hash_value = ht_retrieve_hash(ht_fnc, handler);
    hash_value = hton64(hash_value);
    quick_check(socket_send(clientfd, &hash_value, sizeof(uint64_t)));

    // Comply with protocol
    rpc_message svr_msg_end = RPC_MSG_END;
    quick_check(socket_send(clientfd, &svr_msg_end, sizeof(rpc_message)));

    return true;
}

static bool svr_handle_msg_call(int clientfd, hw_profile* cl_profile, hash_table* ht_fnc) {
    if (cl_profile == NULL || ht_fnc == NULL)
        return true; 

    // Make sure the client has initialised the connection properly
    if (!cl_profile->initialised)
        return svr_handle_rtn_error(clientfd, RPC_ERROR_CXN_INVALID);

    // Scan in data
    rpc_data* input;
    quick_check(socket_recv_data(clientfd, &input));

    // Scan in function handle
    uint64_t hash_value;
    if (!socket_recv(clientfd, &hash_value, sizeof(uint64_t))) {
        rpc_data_free(input);
        return false;
    }
    hash_value = ntoh64(hash_value);

    // Validate client packet
    rpc_message cl_msg_end;
    if (!socket_recv(clientfd, &cl_msg_end, sizeof(uint8_t))) {
        rpc_data_free(input);
        return false;
    }
    if (cl_msg_end != RPC_MSG_END) {
        rpc_data_free(input);
        return svr_handle_rtn_error(clientfd, RPC_ERROR_PQT_INVALID);
    }

    // Run the function
    rpc_handler handler = ht_index_with_hash(ht_fnc, hash_value);
    if (handler == NULL) {
        rpc_data_free(input);
        return svr_handle_rtn_error(clientfd, RPC_ERROR_HNDL_INVALID);
    }
    rpc_data* output = handler(input);
    rpc_data_free(input);

    // Check for errors in data
    rpc_error error;
    if ((error = check_data(cl_profile, output))) {
        rpc_data_free(output);
        return svr_handle_rtn_error(clientfd, error);
    }

    // Send success message with output from function call
    rpc_message message = RPC_RTN_SUCCESS;
    if (!socket_send(clientfd, &message, sizeof(rpc_message)) ||
        !socket_send_data(clientfd, output)) {
        rpc_data_free(output);
        return false;
    }

    // Comply with protocol
    rpc_message svr_msg_end = RPC_MSG_END;
    if(!socket_send(clientfd, &svr_msg_end, sizeof(rpc_message))) {
        rpc_data_free(output);
        return false;
    }

    rpc_data_free(output);
    return true;
}

static bool svr_handle_rtn_error(int clientfd, rpc_error error) {

    // Send error message
    rpc_message message = RPC_RTN_ERROR;
    quick_check(socket_send(clientfd, &message, sizeof(rpc_message)));

    // Send the error
    quick_check(socket_send(clientfd, &error, sizeof(rpc_error)));

    // Comply with protocol
    rpc_message svr_msg_end = RPC_MSG_END;
    quick_check(socket_send(clientfd, &svr_msg_end, sizeof(rpc_message)));
    return true;
}

static bool cl_handle_proc_connect(int serverfd, hw_profile* svr_profile) {
    if (svr_profile == NULL)
        return true;

    // Send out request
    rpc_message message = RPC_MSG_CONNECT;
    quick_check(socket_send(serverfd, &message, sizeof(message)));

    // Send size of int in bytes
    uint8_t sizeof_int_cl = sizeof(int);
    quick_check(socket_send(serverfd, &sizeof_int_cl, sizeof(uint8_t)));

    // Send size of size_t in bytes
    uint8_t sizeof_size_t_cl = sizeof(size_t);
    quick_check(socket_send(serverfd, &sizeof_size_t_cl, sizeof(uint8_t)));

    // Comply with protocol
    rpc_message cl_msg_end = RPC_MSG_END;
    quick_check(socket_send(serverfd, &cl_msg_end, sizeof(rpc_message)));

    // Output
    rpc_message return_val;
    quick_check(socket_recv(serverfd, &return_val, sizeof(rpc_message)));
    
    // Handle the error
    if (return_val == RPC_RTN_ERROR)
        return cl_handle_rtn_error(serverfd);

    // Scan in size of int in bytes
    uint8_t sizeof_int_svr;
    quick_check(socket_recv(serverfd, &sizeof_int_svr, sizeof(uint8_t)));
    svr_profile->int_max = MAX_SINT(sizeof_int_svr);
    svr_profile->int_min = -svr_profile->int_max - 1;

    // Scan in size of size_t in bytes
    uint8_t sizeof_size_t_svr;
    quick_check(socket_recv(serverfd, &sizeof_size_t_svr, sizeof(uint8_t)));
    svr_profile->size_max = MAX_UINT(sizeof_size_t_svr);
    svr_profile->initialised = true;

    // Check the server has ended its message
    rpc_message svr_msg_end;
    quick_check(socket_recv(serverfd, &svr_msg_end, sizeof(rpc_message)));
    if (svr_msg_end != RPC_MSG_END)
        return false;

    return true;
}

static bool cl_handle_proc_find(int serverfd, char* char_buff, 
                                uint16_t length, rpc_handle** output) {

    if (char_buff == NULL || output == NULL)
        return true;

    *output = NULL;

    // Send out request
    rpc_message message = RPC_MSG_FUNC_FIND;
    quick_check(socket_send(serverfd, &message, sizeof(rpc_message)));

    // Send length of function name followed by the name itself
    uint16_t be_length = htons(length);
    quick_check(socket_send(serverfd, &be_length, sizeof(uint16_t)));
    quick_check(socket_send(serverfd, char_buff, length));

    // End message
    rpc_message cl_msg_end = RPC_MSG_END;
    quick_check(socket_send(serverfd, &cl_msg_end, sizeof(rpc_message)));

    // Deal with return value
    rpc_message return_val;
    quick_check(socket_recv(serverfd, &return_val, sizeof(rpc_message)));

    // Handle the error
    if (return_val == RPC_RTN_ERROR)
        return cl_handle_rtn_error(serverfd);
    
    // Retrieve function handle
    uint64_t be_hash_value;
    quick_check(socket_recv(serverfd, &be_hash_value, sizeof(uint64_t)));
    
    // Validate server packet
    rpc_message svr_msg_end;
    quick_check(socket_recv(serverfd, &svr_msg_end, sizeof(rpc_message)));
    if (svr_msg_end != RPC_MSG_END)
        return false;

    // Return the handle to the client
    rpc_handle* handle = calloc(1, sizeof(rpc_handle));
    handle->hash_value = ntoh64(be_hash_value);
    *output = handle;

    return true;
}

static bool cl_handle_proc_call(int serverfd, rpc_handle* handle, 
                                rpc_data* input, rpc_data** output) {
    if (handle == NULL || input == NULL || output == NULL)
        return true;

    *output = NULL;

    // Send out a request with data
    rpc_message message = RPC_MSG_FUNC_CALL;
    quick_check(socket_send(serverfd, &message, sizeof(rpc_message)));
    quick_check(socket_send_data(serverfd, input));
    
    // Send function handle
    uint64_t hash_value = hton64(handle->hash_value);
    quick_check(socket_send(serverfd, &hash_value, sizeof(uint64_t)));

    // Comply with protocol
    rpc_message cl_msg_end = RPC_MSG_END;
    quick_check(socket_send(serverfd, &cl_msg_end, sizeof(rpc_message)));

    // Deal with output
    rpc_message return_val;
    quick_check(socket_recv(serverfd, &return_val, sizeof(rpc_message)));

    // Handle error
    if (return_val == RPC_RTN_ERROR)
        return cl_handle_rtn_error(serverfd);

    // Otherwise scan in data
    rpc_data* data_in;
    quick_check(socket_recv_data(serverfd, &data_in));

    // Validate server packet
    rpc_message svr_msg_end;
    quick_check(socket_recv(serverfd, &svr_msg_end, sizeof(rpc_message)));
    if (svr_msg_end != RPC_MSG_END)
        return false;

    *output = data_in;

    return true;
}

static bool cl_handle_rtn_error(int serverfd) {

    // Read in error
    rpc_error error;
    quick_check(socket_recv(serverfd, &error, sizeof(rpc_error)));
    cl_print_rtn_error(error);

    // Validate server packet
    rpc_message svr_msg_end;
    quick_check(socket_recv(serverfd, &svr_msg_end, sizeof(rpc_message)));
    if (svr_msg_end != RPC_MSG_END)
        return false;
    
    return true;
}

static void cl_print_rtn_error(rpc_error error) {
    if (error & RPC_ERROR_CXN_INVALID)
        fprintf(stderr, "Invalid Connection to Server!\n");
    
    if (error & RPC_ERROR_FUNC_NOT_FOUND)
        fprintf(stderr, "Function not found on server!\n");

    if (error & RPC_ERROR_HNDL_INVALID)
        fprintf(stderr, "Invalid Handle provided!\n");

    if (error & RPC_ERROR_DATA_INVALID)
        fprintf(stderr, "Returned data was invalid\n");

    if (error & RPC_ERROR_DATA_INT_OVF)
        fprintf(stderr, "Output data.data1 too large for client!\n");

    if (error & RPC_ERROR_DATA_BUFF_OVF)
        fprintf(stderr, "Output data.data2 too large for client!\n");

    if (error & RPC_ERROR_MSG_INVALID)
        fprintf(stderr, "Message sent does not exist!\n");

    if (error & RPC_ERROR_PQT_INVALID)
        fprintf(stderr, "Packet sent to server was not formatted correctly");
}

static void rpc_destroy_server(rpc_server* srv) {
    if (srv == NULL) 
        return;

    if (srv->masterfd != SOCKET_NULL_HANDLE)
        close(srv->masterfd);

    // Data structures
    ht_destroy(srv->hash_table);
    list_destroy(srv->list_fd);
    
    // Thread state
    pthread_cond_destroy(&srv->client_cond);
    pthread_mutex_destroy(&srv->mutex_list_fd);

    // Zero state and free
    memset(srv, 0, sizeof(rpc_server));
    FREE(srv);
}

static void rpc_destroy_client(rpc_client* cl) {
    if (cl == NULL)
        return;
    
    // Close the socket
    if (cl->serverfd != SOCKET_NULL_HANDLE)
        close(cl->serverfd);
    
    // Zero state and free
    memset(cl, 0 , sizeof(rpc_client));
    FREE(cl);
}