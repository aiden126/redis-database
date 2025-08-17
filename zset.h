#include "avl.h"
#include "hashtable.h"

struct ZSet {
    AVLNode *root;                  // can be null
    HMap hmap;
};

struct ZNode {
    // intrusive hooks
    AVLNode tree;
    HNode hmap;

    // payload
    double score = 0;
    size_t len = 0;
    char name[0];
};

bool zset_insert(ZSet *zset, const char *name, size_t len, double score);
ZNode *zset_lookup(ZSet *zset, const char *name, size_t len);
void zset_delete(ZSet *zset, ZNode *node);
void zset_clear(ZSet *zset);
