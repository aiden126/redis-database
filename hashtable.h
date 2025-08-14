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