#include <stddef.h>
#include <stdint.h>

struct AVLNode {
    AVLNode *parent = NULL;
    AVLNode *left = NULL;
    AVLNode *right = NULL;

    uint32_t height = 0;
    uint32_t cnt = 0;
};

void avl_init(AVLNode *node);

uint32_t avl_height(AVLNode *node);
uint32_t avl_cnt(AVLNode *node);

// API
AVLNode *avl_fix(AVLNode *node);
AVLNode *avl_del(AVLNode *node);