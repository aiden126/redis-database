#include "avl.h"

static uint32_t max(uint32_t lhs, uint32_t rhs) {
    return lhs > rhs ? lhs : rhs;
}

void avl_init(AVLNode *node) {
    node->parent = NULL;
    node->left = NULL;
    node->right = NULL;

    node->height = 1;
    node->cnt = 1;
}

uint32_t avl_height(AVLNode *node) {
    return node ? node->height : 0;
}

uint32_t avl_cnt(AVLNode *node) {
    return node ? node->cnt : 0;
}

void avl_update(AVLNode *node) {
    node->height = max(avl_height(node->left), avl_height(node->right)) + 1;
    node->cnt = avl_cnt(node->left) + avl_cnt(node->right) + 1;
}

static AVLNode *rotate_left(AVLNode *node) {
    AVLNode *parent = node->parent;
    AVLNode *new_node = node->right;
    AVLNode *inner = new_node->left;

    node->right = inner;
    if (inner) {
        inner->parent = node;
    }

    new_node->parent = parent;
    new_node->left = node;
    
    node->parent = new_node;

    // auxiliary data
    avl_update(node);
    avl_update(new_node);

    return new_node;
}

static AVLNode *rotate_right(AVLNode *node) {
    AVLNode *parent = node->parent;
    AVLNode *new_node = node->left;
    AVLNode *inner = new_node->right;

    node->left = inner;
    if (inner) {
        inner->parent = node;
    }

    new_node->parent = parent;
    new_node->right = node;
    
    node->parent = new_node;

    // auxiliary data
    avl_update(node);
    avl_update(new_node);

    return new_node;
}

static AVLNode *avl_fix_left(AVLNode *node) {
    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left = rotate_left(node->left);
    }
    return rotate_right(node);
}

static AVLNode *avl_fix_right(AVLNode *node) {
    if (avl_height(node->right->right) < avl_height(node->right->left)) {
        node->right = rotate_right(node->right);
    }
    return rotate_left(node);
}

// fix after insertion or deletion
AVLNode *avl_fix(AVLNode *node) {
    while (true) {
        AVLNode **from = &node;
        AVLNode *parent = node->parent;
        if (parent) {
            from = parent->left == node ? &parent->left : &parent->right;
        }

        avl_update(node);

        uint32_t l = avl_height(node->left);
        uint32_t r = avl_height(node->right);
        if (l == r + 2) {                                       // left subtree is too tall
            *from = avl_fix_left(node);
        } else if (l + 2 == r) {
            *from = avl_fix_right(node);
        }

        if (!parent) {
            return *from;
        }

        node = parent;
    }
}

// detach a node where one child is empty
static AVLNode *avl_detach(AVLNode *node) {
    AVLNode *child = node->left ? node->left : node->right;
    AVLNode *parent = node->parent;

    if (child) {
        child->parent = parent;
    }

    if (!parent) {
        return child;
    }

    AVLNode **from = parent->left == node ? &parent->left : &parent->right;       // update parent's child pointer to point new child
    *from = child;

    return avl_fix(parent);
}

AVLNode *avl_del(AVLNode *node) {
    if (!node->left || !node->right) {
        return avl_detach(node);
    }

    // find the successor (left-most node of right subtree)
    AVLNode *successor = node->right;
    while (successor->left) {
        successor = successor->left;
    }

    AVLNode *root = avl_detach(successor);

    *successor = *node;                                             // copy over node's position in tree (parent, left, right)
    if (successor->left) {
        successor->left->parent = successor;
    }
    if (successor->right) {
        successor->right->parent = successor;
    }

    AVLNode **from = &root;
    AVLNode *parent = node->parent;
    if (parent) {
        from = parent->left == node ? &parent->left : &parent->right;
    }

    *from = successor;
    return root;
}