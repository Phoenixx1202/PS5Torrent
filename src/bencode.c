#include "bencode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* Internal parser state */
typedef struct {
    const unsigned char *data;
    size_t               pos;
    size_t               len;
} parse_state_t;

/* Forward declarations */
static bcode_node_t *parse_value(parse_state_t *st);

static bcode_node_t *bcode_new_node(bcode_type_t type)
{
    bcode_node_t *node = calloc(1, sizeof(bcode_node_t));
    if (node)
        node->type = type;
    return node;
}

static int bcode_add_child(bcode_node_t *parent, bcode_node_t *child)
{
    if (parent->child_count >= parent->child_cap) {
        size_t new_cap = parent->child_cap ? parent->child_cap * 2 : 8;
        bcode_node_t **newc = realloc(parent->children,
                                       new_cap * sizeof(bcode_node_t *));
        if (!newc) return -1;
        parent->children = newc;
        parent->child_cap = new_cap;
    }
    parent->children[parent->child_count++] = child;
    return 0;
}

static int parse_peek(parse_state_t *st)
{
    if (st->pos >= st->len) return -1;
    return st->data[st->pos];
}

static int parse_advance(parse_state_t *st)
{
    if (st->pos >= st->len) return -1;
    return st->data[st->pos++];
}

static int64_t parse_integer(parse_state_t *st)
{
    int64_t val = 0;
    int neg = 0;

    if (parse_advance(st) != 'i') return 0;
    if (st->pos >= st->len) return 0;

    if (st->data[st->pos] == '-') {
        neg = 1;
        st->pos++;
    }

    while (st->pos < st->len && st->data[st->pos] >= '0' &&
           st->data[st->pos] <= '9') {
        val = val * 10 + (st->data[st->pos++] - '0');
    }

    if (st->pos >= st->len || st->data[st->pos] != 'e') return 0;
    st->pos++; // consume 'e'

    return neg ? -val : val;
}

static unsigned char *parse_string(parse_state_t *st, size_t *len_out)
{
    size_t slen = 0;

    while (st->pos < st->len && st->data[st->pos] >= '0' &&
           st->data[st->pos] <= '9') {
        slen = slen * 10 + (st->data[st->pos++] - '0');
    }

    if (st->pos >= st->len || st->data[st->pos] != ':') return NULL;
    st->pos++; // consume ':'

    if (st->pos + slen > st->len) return NULL;

    unsigned char *str = malloc(slen);
    if (!str) return NULL;

    memcpy(str, st->data + st->pos, slen);
    st->pos += slen;
    *len_out = slen;
    return str;
}

static bcode_node_t *parse_list(parse_state_t *st)
{
    bcode_node_t *node = bcode_new_node(BCODE_LIST);
    if (!node) return NULL;

    if (parse_advance(st) != 'l') { free(node); return NULL; }

    while (st->pos < st->len && st->data[st->pos] != 'e') {
        bcode_node_t *child = parse_value(st);
        if (!child) { bcode_free(node); return NULL; }
        bcode_add_child(node, child);
    }

    if (st->pos >= st->len) { bcode_free(node); return NULL; }
    st->pos++; // consume 'e'

    return node;
}

static bcode_node_t *parse_dict(parse_state_t *st)
{
    bcode_node_t *node = bcode_new_node(BCODE_DICT);
    if (!node) return NULL;

    if (parse_advance(st) != 'd') { free(node); return NULL; }

    while (st->pos < st->len && st->data[st->pos] != 'e') {
        bcode_node_t *entry = bcode_new_node(BCODE_STR);
        if (!entry) { bcode_free(node); return NULL; }

        entry->key = parse_string(st, &entry->key_len);
        if (!entry->key) { free(entry); bcode_free(node); return NULL; }

        bcode_node_t *val = parse_value(st);
        if (!val) { free(entry->key); free(entry); bcode_free(node); return NULL; }

        /* Transfer key to the entry node */
        val->key = entry->key;
        val->key_len = entry->key_len;
        free(entry);

        bcode_add_child(node, val);
    }

    if (st->pos >= st->len) { bcode_free(node); return NULL; }
    st->pos++; // consume 'e'

    return node;
}

static bcode_node_t *parse_value(parse_state_t *st)
{
    int c = parse_peek(st);
    if (c < 0) return NULL;

    if (c == 'i') {
        bcode_node_t *node = bcode_new_node(BCODE_INT);
        if (!node) return NULL;
        node->int_val = parse_integer(st);
        return node;
    }

    if (c >= '0' && c <= '9') {
        bcode_node_t *node = bcode_new_node(BCODE_STR);
        if (!node) return NULL;
        node->str_val = parse_string(st, &node->str_len);
        if (!node->str_val) { free(node); return NULL; }
        return node;
    }

    if (c == 'l') return parse_list(st);
    if (c == 'd') return parse_dict(st);

    return NULL;
}

bcode_node_t *bcode_parse(const unsigned char *data, size_t data_len)
{
    if (!data || data_len == 0) return NULL;

    parse_state_t st;
    st.data = data;
    st.pos = 0;
    st.len = data_len;

    return parse_value(&st);
}

bcode_node_t *bcode_dict_get(const bcode_node_t *dict, const char *key)
{
    if (!dict || dict->type != BCODE_DICT) return NULL;

    size_t key_len = strlen(key);

    for (size_t i = 0; i < dict->child_count; i++) {
        bcode_node_t *child = dict->children[i];
        if (child->key_len == key_len &&
            memcmp(child->key, key, key_len) == 0) {
            return child;
        }
    }

    return NULL;
}

const unsigned char *bcode_str_val(const bcode_node_t *node, size_t *len)
{
    if (!node || node->type != BCODE_STR) return NULL;
    if (len) *len = node->str_len;
    return node->str_val;
}

int64_t bcode_int_val(const bcode_node_t *node)
{
    if (!node || node->type != BCODE_INT) return 0;
    return node->int_val;
}

bcode_node_t *bcode_list_get(const bcode_node_t *node, size_t index)
{
    if (!node || node->type != BCODE_LIST) return NULL;
    if (index >= node->child_count) return NULL;
    return node->children[index];
}

size_t bcode_list_len(const bcode_node_t *node)
{
    if (!node || node->type != BCODE_LIST) return 0;
    return node->child_count;
}

void bcode_free(bcode_node_t *node)
{
    if (!node) return;

    if (node->key) free(node->key);
    if (node->str_val) free(node->str_val);

    for (size_t i = 0; i < node->child_count; i++)
        bcode_free(node->children[i]);

    if (node->children) free(node->children);
    free(node);
}

/* ---- Encoding ---- */

/* Forward declaration */
static int bcode_encode_node(const bcode_node_t *node,
                              unsigned char **buf, size_t *pos, size_t *cap);

static int buf_ensure(unsigned char **buf, size_t *pos, size_t *cap, size_t need)
{
    if (*pos + need < *cap) return 0;
    size_t new_cap = *cap ? *cap * 2 : 256;
    while (*pos + need >= new_cap) new_cap *= 2;
    unsigned char *new_buf = realloc(*buf, new_cap);
    if (!new_buf) return -1;
    *buf = new_buf;
    *cap = new_cap;
    return 0;
}

static int buf_append(unsigned char **buf, size_t *pos, size_t *cap,
                       const void *data, size_t len)
{
    if (buf_ensure(buf, pos, cap, len)) return -1;
    memcpy(*buf + *pos, data, len);
    *pos += len;
    return 0;
}

static int encode_string(unsigned char **buf, size_t *pos, size_t *cap,
                          const unsigned char *s, size_t slen)
{
    char len_str[32];
    int len = snprintf(len_str, sizeof(len_str), "%zu:", slen);
    if (len < 0) return -1;
    if (buf_append(buf, pos, cap, len_str, (size_t)len)) return -1;
    if (buf_append(buf, pos, cap, s, slen)) return -1;
    return 0;
}

static int encode_int(unsigned char **buf, size_t *pos, size_t *cap,
                       int64_t val)
{
    char num[32];
    int n;
    if (buf_ensure(buf, pos, cap, 2)) return -1;
    (*buf)[(*pos)++] = 'i';
    n = snprintf(num, sizeof(num), "%" PRId64, val);
    if (n < 0) return -1;
    if (buf_append(buf, pos, cap, num, (size_t)n)) return -1;
    if (buf_ensure(buf, pos, cap, 1)) return -1;
    (*buf)[(*pos)++] = 'e';
    return 0;
}

static int bcode_encode_node(const bcode_node_t *node,
                              unsigned char **buf, size_t *pos, size_t *cap)
{
    if (!node) return -1;

    switch (node->type) {
    case BCODE_STR:
        return encode_string(buf, pos, cap, node->str_val, node->str_len);

    case BCODE_INT:
        return encode_int(buf, pos, cap, node->int_val);

    case BCODE_LIST:
        if (buf_ensure(buf, pos, cap, 1)) return -1;
        (*buf)[(*pos)++] = 'l';
        for (size_t i = 0; i < node->child_count; i++) {
            if (bcode_encode_node(node->children[i], buf, pos, cap))
                return -1;
        }
        if (buf_ensure(buf, pos, cap, 1)) return -1;
        (*buf)[(*pos)++] = 'e';
        return 0;

    case BCODE_DICT:
        if (buf_ensure(buf, pos, cap, 1)) return -1;
        (*buf)[(*pos)++] = 'd';
        for (size_t i = 0; i < node->child_count; i++) {
            bcode_node_t *child = node->children[i];
            if (encode_string(buf, pos, cap, child->key, child->key_len))
                return -1;
            if (bcode_encode_node(child, buf, pos, cap))
                return -1;
        }
        if (buf_ensure(buf, pos, cap, 1)) return -1;
        (*buf)[(*pos)++] = 'e';
        return 0;

    default:
        return -1;
    }
}

unsigned char *bcode_encode(const bcode_node_t *node, size_t *out_len)
{
    if (!node || !out_len) return NULL;

    unsigned char *buf = NULL;
    size_t pos = 0, cap = 0;

    if (bcode_encode_node(node, &buf, &pos, &cap)) {
        free(buf);
        return NULL;
    }

    *out_len = pos;
    return buf;
}
