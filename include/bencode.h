#ifndef PS5TORRENT_BENCODE_H
#define PS5TORRENT_BENCODE_H

#include <stdint.h>
#include <stddef.h>

/**
 * Bencode value types.
 */
typedef enum {
    BCODE_STR,    // Byte string
    BCODE_INT,    // Integer
    BCODE_LIST,   // List of values
    BCODE_DICT,   // Dictionary of key-value pairs
    BCODE_END,    // End marker (internal)
    BCODE_INVALID // Parse error
} bcode_type_t;

/**
 * Forward declaration for bencode node.
 */
typedef struct bcode_node bcode_node_t;

/**
 * A bencode node (recursive structure).
 */
struct bcode_node {
    bcode_type_t type;

    // For string values
    unsigned char *str_val;
    size_t str_len;

    // For integer values
    int64_t int_val;

    // For list/dict - children
    bcode_node_t **children;
    size_t child_count;
    size_t child_cap;

    // For dict entries - key
    unsigned char *key;
    size_t key_len;
};

/**
 * Parse a bencoded buffer into a tree of nodes.
 * @param data     The bencoded data
 * @param data_len Length of the data
 * @return Root node, or NULL on failure. Must be freed with bcode_free().
 */
bcode_node_t *bcode_parse(const unsigned char *data, size_t data_len);

/**
 * Look up a key in a dictionary node.
 * @param dict The dictionary node
 * @param key  The key to find (C string)
 * @return The value node, or NULL if not found
 */
bcode_node_t *bcode_dict_get(const bcode_node_t *dict, const char *key);

/**
 * Get a string value from a node.
 * @return Pointer to string data, or NULL
 */
const unsigned char *bcode_str_val(const bcode_node_t *node, size_t *len);

/**
 * Get an integer value from a node.
 * @return Integer value, or 0 if not an int
 */
int64_t bcode_int_val(const bcode_node_t *node);

/**
 * Get a list item from a list node.
 * @return The child at index, or NULL
 */
bcode_node_t *bcode_list_get(const bcode_node_t *node, size_t index);

/**
 * Get the number of items in a list.
 */
size_t bcode_list_len(const bcode_node_t *node);

/**
 * Free an entire bencode tree.
 */
void bcode_free(bcode_node_t *node);

/**
 * Encode a bencode node back into bencoded format.
 * @param node     The node to encode
 * @param out_len  Output: length of encoded data
 * @return Allocated buffer with encoded data, or NULL on failure
 */
unsigned char *bcode_encode(const bcode_node_t *node, size_t *out_len);

#endif /* PS5TORRENT_BENCODE_H */
