#include <assert.h>
#include <stdlib.h>

#include "hashtable.h"

static void h_init(HTable *htable, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0);            // n must be a power of 2

    htable->table = (HNode**)calloc(n, sizeof(HNode*));
    htable->mask = n - 1;
    htable->size = 0;
}

static void h_insert(HTable *htable, HNode *node) {
    size_t pos = node->hcode & htable->mask;        // hash(key) % n
    HNode *next = htable->table[pos];
    
    node->next = next;
    htable->table[pos] = node;
    htable->size++;
}

static HNode **h_lookup(HTable *htable, HNode *key, bool (*eq)(HNode*, HNode*)) {
    if (!htable->table) {
        return NULL;
    }

    size_t pos = key->hcode & htable->mask;
    HNode **from = &htable->table[pos];

    for (HNode *cur; (cur = *from) != NULL; from = &cur->next) {
        if (cur->hcode == key->hcode && eq(cur, key)) {     // check if data matches (outside intrusive node)
            return from;
        }
    }

    return NULL;
}