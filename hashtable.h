#include <stddef.h>
#include <stdint.h>

struct HNode {
    HNode *next = NULL;
    uint64_t hcode = 0;
};

struct HTable {
    HNode **table = NULL;
    size_t mask = 0;
    size_t size = 0;
};

struct HMap {
    HTable new_table;
    HTable old_table;
    size_t migrate_pos = 0;
};

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode*, HNode*));
HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode*, HNode*));
void hm_insert(HMap *hmap, HNode *node);
void hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg);
void hm_clear(HMap *hmap);

size_t hm_size(HMap *hmap);