#include <assert.h>
#include <stdlib.h>

#include "hashtable.h"

const size_t k_max_load_factor = 8;
const size_t k_max_rehashing_work = 128;

static void h_init(HTable *htable, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0);                        // n must be a power of 2

    htable->table = (HNode**)calloc(n, sizeof(HNode*));
    htable->mask = n - 1;
    htable->size = 0;
}

static void h_insert(HTable *htable, HNode *node) {
    size_t pos = node->hcode & htable->mask;                    // hash(key) % n
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
        if (cur->hcode == key->hcode && eq(cur, key)) {         // check if data matches (outside intrusive node)
            return from;
        }
    }

    return NULL;
}

static HNode *h_detach(HTable *htable, HNode **from) {
    HNode *target = *from;
    *from = target->next;
    htable->size--;

    return target;
}

static void hm_trigger_rehashing(HMap *hmap) {
    hmap->old_table = hmap->new_table;
    h_init(&hmap->new_table, (hmap->new_table.mask + 1) * 2);           // set new hash table to be double the size
    hmap->migrate_pos = 0;
}

static void hm_help_rehashing(HMap *hmap) {                             // migrate up to n nodes each call
    size_t nwork = 0;
    while (nwork < k_max_rehashing_work && hmap->old_table.size > 0) {
        HNode **from = &hmap->old_table.table[hmap->migrate_pos];
        if (!*from) {
            hmap->migrate_pos++;
            continue;
        }

        h_insert(&hmap->new_table, h_detach(&hmap->old_table, from));   // remove node from old table and insert into new
        nwork++;
    }

    if (hmap->old_table.size == 0 && hmap->old_table.table) {
        free(hmap->old_table.table);
        hmap->old_table = HTable{};
    }
}

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode*, HNode*)) {
    HNode **from = h_lookup(&hmap->new_table, key, eq);
    if (!from) {
        from = h_lookup(&hmap->old_table, key, eq);
    }

    return from ? *from : NULL;
}

HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode*, HNode*)) {
    if (HNode **from = h_lookup(&hmap->new_table, key, eq)) {
        return h_detach(&hmap->new_table, from);
    }

    if (HNode **from = h_lookup(&hmap->old_table, key, eq)) {
        return h_detach(&hmap->old_table, from);
    }

    return NULL;
}

void hm_insert(HMap *hmap, HNode *node) {
    if (!hmap->new_table.table) {
        h_init(&hmap->new_table, 4);
    }

    h_insert(&hmap->new_table, node);

    if (!hmap->old_table.table) {                           // check if we need to rehash (resize)
        size_t threshold = (hmap->new_table.mask + 1) * k_max_load_factor;
        if (hmap->new_table.size > threshold) {
            hm_trigger_rehashing(hmap);
        }
    }

    hm_help_rehashing(hmap);
}

