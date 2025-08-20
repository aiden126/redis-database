#include "heap.h"

static size_t heap_parent(size_t i) {
    return (i + 1) / 2 - 1;
}

static size_t heap_left(size_t i) {
    return i * 2 + 1;
}

static size_t heap_right(size_t i) {
    return i * 2 + 2;
}

static void heap_up(HeapItem *heap, size_t pos) {
    HeapItem target = heap[pos];
    while (pos > 0 && heap[heap_parent(pos)].val > target.val) {
        heap[pos] = heap[heap_parent(pos)];
        *heap[pos].ref = pos;
        pos = heap_parent(pos);
    }

    heap[pos] = target;
    *heap[pos].ref = pos;
}

static void heap_down(HeapItem *heap, size_t pos, size_t len) {
    HeapItem target = heap[pos];
    while (true) {
        size_t l = heap_left(pos);
        size_t r = heap_right(pos);
        size_t min_pos = pos;

        if (l < len && heap[l].val < heap[min_pos].val) {
            min_pos = l;
        }

        if (r < len && heap[r].val < heap[min_pos].val) {
            min_pos = r;
        }

        if (min_pos == pos) {
            break;
        }

        heap[pos] = heap[min_pos];
        *heap[pos].ref = pos;
        pos = min_pos;
    }

    heap[pos] = target;
    *heap[pos].ref = pos;
}

void heap_update(HeapItem *heap, size_t pos, size_t len) {
    if (pos > 0 && heap[heap_parent(pos)].val > heap[pos].val) {
        heap_up(heap, pos);
    } else {
        heap_down(heap, pos, len);
    }
}