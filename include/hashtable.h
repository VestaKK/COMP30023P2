/**
 * Honestly this is just a glorified dynamic array.
 * I could honestly make this generic. I don't want to make this
 * generic. Therefore its not going to be generic.
*/

#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include "rpc.h"
#include "defines.h"

#define DEFAULT_CAPACITY 10
#define RESIZE_FACTOR 2

typedef struct hash_item hash_item;
typedef struct hash_table hash_table;

/**
 * @brief
 * Allocates and creates a hashtable.
 * @param initial_capacity Initial capacity of the hashtable
 * @return 
 * A heap allocated hashtable. Ensure to destroy this
 * hashtable with ht_destroy().
*/
hash_table* _ht_create(uint64_t initial_capacity);

/**
 * Destroys and frees memory related to the hashtable
 * @param ppHt Pointer to a hashtable. Ideally this is just the
 * address of the initial hashtable.
*/
void _ht_destroy(hash_table** ppHt);

/**
 * @brief
 * Inserts a new string-handler pair into the given hashtable
 * @param pHt Pointer to a hashtable
 * @param string Null-terminated string
 * @param handler Function linked to the given string
 * @note
 * If the string already exists inside the hashtable, the given handler
 * will replace the previous handler linked to the string.
*/
void ht_insert(hash_table* pHt, char* string, rpc_handler handler);

/**
 * @brief
 * Deletes the string-handler pair linked to the given string
 * @param pHt Pointer to a hashtable
 * @param string Null-terminated string
 * @note
 * If the string is not paired with any function inside the hashtable,
 * this function will do nothing.
 *  
*/
void ht_delete(hash_table* pHt, char* string);

/**
 * @brief
 * Retrieves the handler linked to the given string
 * @param pHt Pointer to a hashtable
 * @param string Null-terminated string
 * @return
 * If the string is linked to a handler, this function will return the linked
 * handler. Otherwise, this function will return NULL.
*/
rpc_handler ht_index(hash_table* pHt, char* string);

/**
 * @brief
 * Retrieves the hash linked to the current handler
 * @param pHt Pointer to a hashtable
 * @param handler handler
 * @return
 * If the handler exists in the hashtable, this function will return the hash
 * linked to the given handler. Otherwise, this function will return UINT64_MAX
*/
uint64_t ht_retrieve_hash(hash_table* pHt, rpc_handler handler);

/**
 * @brief
 * Retrieves the handler linked to the given hash
 * @param pHt Pointer to a hashtable
 * @param hash_value 64-bit hash
 * @return
 * If the hash is linked to a handler, this function will return the linked
 * handler. Otherwise, this function will return NULL.
*/
rpc_handler ht_index_with_hash(hash_table* pHt, uint64_t hash_value);

// Creates hashtable with default capacity
#define ht_create() _ht_create(DEFAULT_CAPACITY)

// Destroys hashtable
#define ht_destroy(ht) _ht_destroy(&ht);
#endif