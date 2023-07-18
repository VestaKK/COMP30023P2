#include "helper.h"

bool is_valid_name(const char* name) {

    // Iterate over each character and check if each are valid
    for (int i=0; i<strlen(name); i++) {
        char character = name[i];
        if (character < ' ' || character > 132)
            return false;
    }

    // Check length does not go over the designated amount
    return strlen(name) <= UINT16_MAX;
}

bool socket_recv(int socketfd, void* buff, size_t nbytes) {
    uint8_t* p_buff = buff;
    size_t bytes_to_read = nbytes;

    while(bytes_to_read > 0) {
        ssize_t bytes_read = recv(socketfd, p_buff, bytes_to_read, 0);

        if (bytes_read <= 0)
            return false;

        bytes_to_read -= bytes_read;
        p_buff += bytes_read;
    }

    return true;
}

bool socket_send(int socketfd, void* buff, size_t nbytes) {
    uint8_t* p_buff = buff;
    size_t bytes_to_write = nbytes;

    while (bytes_to_write > 0) {
        ssize_t bytes_written = send(socketfd, p_buff, bytes_to_write, MSG_NOSIGNAL);

        if (bytes_written < 0)
            return false;
        
        bytes_to_write -= bytes_written;
        p_buff += bytes_written;
    }

    return true;
}

char* int_to_string(int integer) {
    int length = snprintf(NULL, 0, "%d", integer);
    char* string = malloc(length + 1);
    snprintf(string, length + 1, "%d", integer);
    return string;
}

rpc_error check_data(hw_profile* profile, rpc_data* data) {

    // If client hasn't completed the initialisation protocol, 
    // you can't continue checking the data.
    if (!profile->initialised)
        return RPC_ERROR_CXN_INVALID;

    // Null data is not a valid data point
    if (data == NULL)
        return RPC_ERROR_DATA_INVALID;
    
    // Scan for errors in data
    rpc_error error_flags = RPC_ERROR_NONE;

    // Check for int overflow
    if (data->data1 > profile->int_max || 
        data->data1 < profile->int_min) {
        error_flags |= RPC_ERROR_DATA_INT_OVF;
    }

    // Check for invalid data
    if ((data->data2_len == 0 && data->data2 != NULL) ||
        (data->data2_len > 0 && data->data2 == NULL)) {
            error_flags |= RPC_ERROR_DATA_INVALID;
    }

    // Check for buffer overflow
    if (data->data2_len > profile->size_max) {
        error_flags |= RPC_ERROR_DATA_BUFF_OVF;
    }

    return error_flags;
}

rpc_data_flags gen_data_flags(rpc_data* data) {
    if(data == NULL)
        return RPC_DATA_NONE;
    

    // Tag data based on the contents of data
    rpc_data_flags flags = RPC_DATA_NONE;
    
    flags |= RPC_DATA_INT;

    if (data->data2_len != 0 && 
        data->data2 != NULL) {
        flags |= RPC_DATA_BUFF;
    }

    return flags;
}

bool socket_send_data(int fd, rpc_data* input) {
    if (input == NULL)
        return true;

    // Generate and send necessary data flags
    rpc_data_flags flags_out = gen_data_flags(input);
    quick_check(socket_send(fd, &flags_out, sizeof(rpc_data_flags)));
    
    // Send out data according to the data flags
    if (flags_out & RPC_DATA_INT) {
        int64_t be_data1 = hton64(input->data1);
        quick_check(socket_send(fd, &be_data1, sizeof(int64_t)));
    }

    if (flags_out & RPC_DATA_BUFF) {
        uint64_t be_data2_len = hton64(input->data2_len);
        quick_check(socket_send(fd, &be_data2_len, sizeof(uint64_t)));
        quick_check(socket_send(fd, input->data2, input->data2_len));
    }

    return true;
}

bool socket_recv_data(int fd, rpc_data** output) {

    if (output == NULL)
        return true; 
    else
        *output = NULL;

    // Read in data flags
    rpc_data_flags flags_in;
    if (!socket_recv(fd, &flags_in, sizeof(rpc_data_flags))) {
        return false;
    }
    
    // Read in data payload based of data flags
    rpc_data* recv_data = calloc(1, sizeof(rpc_data));

    if (flags_in & RPC_DATA_INT) {
        int64_t be_data1;
        if (!socket_recv(fd, &be_data1, sizeof(int64_t))) {
            free(recv_data);
            return false;
        }
        recv_data->data1 = ntoh64(be_data1);
    }

    if (flags_in & RPC_DATA_BUFF) {
        uint64_t be_data2_len;
        if (!socket_recv(fd, &be_data2_len, sizeof(uint64_t))) {
            free(recv_data);
            return false;
        }
        recv_data->data2_len = ntoh64(be_data2_len);

        uint8_t* net_data2 = malloc(recv_data->data2_len); 
        if (!socket_recv(fd, net_data2, recv_data->data2_len)) {
            free(net_data2);
            free(recv_data);
        }
        recv_data->data2 = net_data2;
    }

    *output = recv_data;

    return true;
}