#include "hashtable.h"

// This is what the hashing function is based on
#define CHOSEN_PRIME 97

typedef struct hash_item {
    uint64_t hash_value;
    rpc_handler handler;
} hash_item;

typedef struct hash_table {
    size_t capacity;
    size_t count;
    hash_item* table;
} hash_table;

// Resizes hashtable by RESIZE_FACTOR
static void _ht_resize(hash_table* pHt);

// Generates the hash using CHOSEN_PRIME
static uint64_t generate_hash(char* string);

// Default hashing function
static uint64_t hash(const char* string, const uint64_t prime);

// hashes name into a 64-bit integer based on the given prime
// chance of collision between two strings is is 1/modulo
// ref: https://byby.dev/polynomial-rolling-hash#:~:text=Hash%20functions%20are%20used%20to,keys%20by%20comparing%20their%20fingerprints.
static uint64_t hash(const char* string, const uint64_t prime) {
    uint64_t modulo = UINT64_MAX - 58; // Largest prime under 2^64
    uint64_t hash_value = 0;
    uint64_t base = prime;
    for (int i=0; i<strlen(string); i++) {
        hash_value = (hash_value*base + (string[i] - ' ' + 1)) % modulo;
    }
    return hash_value;
}

uint64_t generate_hash(char* string) {
    return hash(string, CHOSEN_PRIME);
}

hash_table* _ht_create(uint64_t initial_capacity) {
    hash_table* pHt = calloc(1, sizeof(hash_table));
    pHt->table = calloc(initial_capacity, sizeof(hash_item));
    pHt->capacity = initial_capacity;
    pHt->count = 0;
    return pHt;
}

void _ht_destroy(hash_table** ppHt) {
    if (ppHt == NULL) 
        return;

    // Deinitialise internal table
    memset((*ppHt)->table, 0, sizeof(hash_item)*(*ppHt)->capacity);
    FREE((*ppHt)->table);

    // Deinitialise hash_table struct
    (*ppHt)->capacity = 0;
    (*ppHt)->count = 0;
    FREE((*ppHt)->table);
    FREE(*ppHt);
}

void ht_insert(hash_table* pHt, char* string, rpc_handler handler) {
    if (pHt == NULL || string == NULL || handler == NULL)
        return;
        
    uint64_t hash_value = generate_hash(string);

    // Check if name already exists in table 
    for (int i=0; i<pHt->count; i++) {
        hash_item* item = &pHt->table[i];
        if (item->hash_value == hash_value) {
            item->handler = handler;
            return;
        }
    }

    // Otherwise try to insert a new element
    if (pHt->count >= pHt->capacity)
        _ht_resize(pHt);

    pHt->table[pHt->count].hash_value = hash_value;
    pHt->table[pHt->count].handler = handler;
    pHt->count++;
}

void ht_delete(hash_table* pHt, char* string){
    
    if(pHt == NULL || string == NULL) 
        return;
    
    uint64_t hash_value = generate_hash(string);
    hash_item* chosen = NULL;
    size_t offset = 0;

    // Search for item to delete
    for (int i=0; i<pHt->count; i++) {
        hash_item* item = &pHt->table[i];
        if(item->hash_value == hash_value) {
            chosen = item;
            offset = i;
            break;
        }
    }

    // Return if item not found in hash_table
    if (chosen == NULL) 
        return;

    // Mimicking a pop in the hashtable
    if (offset == pHt->capacity - 1) {
        memset(&pHt->table[pHt->capacity - 1], 0, sizeof(hash_item)); // Last item has to be zeroed out
        pHt->count--;
    } else {
        size_t offset_to_end = pHt->capacity - offset;
        memcpy(&pHt->table[offset], &pHt->table[offset + 1], sizeof(hash_item) * (offset_to_end - 1));
        memset(&pHt->table[pHt->capacity - 1], 0, sizeof(hash_item)); // Last item has to be zeroed out
        pHt->count--;
    }
}

static void _ht_resize(hash_table* pHt) {
    // Resize or retrieve appropriate memory block
    size_t alloc_size = sizeof(hash_item) * pHt->capacity * RESIZE_FACTOR;
    pHt->table = realloc(pHt->table, alloc_size);

    // Zeroing leftover bytes of memory not used to store data
    size_t bytes_to_zero_out = sizeof(hash_item) * ((pHt->capacity * RESIZE_FACTOR) - pHt->capacity);  
    memset(&pHt->table[pHt->capacity], 0, bytes_to_zero_out);
    pHt->capacity *= RESIZE_FACTOR;
}

rpc_handler ht_index(hash_table* pHt, char* string) {
    uint64_t hash_value = generate_hash(string);
    return ht_index_with_hash(pHt, hash_value);
} 

rpc_handler ht_index_with_hash(hash_table* pHt, uint64_t hash_value) {

    // Search for hash_value in hashtable
    for (int i=0; i<pHt->count; i++) {
        hash_item* item = &pHt->table[i];
        if (item->hash_value == hash_value) {
            return item->handler;
        }
    }

    // Didn't find any hash_item containing hash_value
    return NULL;
}

uint64_t ht_retrieve_hash(hash_table* pHt, rpc_handler handler) {

    // Search for handler in hash_table
    for (int i=0; i<pHt->count; i++) {
        hash_item* item = &pHt->table[i];
        if (item->handler == handler) {
            return item->hash_value;
        }
    }

    // Didn't a hash_value associated with the handler
    // Since hash value is always less than INT64_MAX - CHOSEN_PRIME,
    // this is never a valid handle
    return UINT64_MAX;
}