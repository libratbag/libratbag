#pragma once

/***
  This file is part of ratbagd.

  Copyright 2015 David Herrmann <dh.herrmann@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a
  copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice (including the next
  paragraph) shall be included in all copies or substantial portions of the
  Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
***/

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#ifdef RBTREE_DEBUG
#  include <assert.h>
#  define rbtree_assert(_x) assert(_x)
#else
#  define rbtree_assert(_x)
#endif

typedef struct RBTree RBTree;
typedef struct RBNode RBNode;

enum {
        RBNODE_RED      = 0,
        RBNODE_BLACK    = 1,
};

struct RBTree {
        RBNode *root;
};

struct RBNode {
        RBNode *__parent_and_color;
        RBNode *left;
        RBNode *right;
};

#define rbnode_of(_ptr, _type, _member)                                                 \
        __extension__ ({                                                                \
                const typeof( ((_type*)0)->_member ) *__ptr = (_ptr);                   \
                __ptr ? (_type*)( (char*)__ptr - offsetof(_type, _member) ) : NULL;     \
        })

#define RBNODE_INIT(var) ((RBNode){ .__parent_and_color = (&var) })

static inline RBNode *rbnode_init(RBNode *n) {
        *n = RBNODE_INIT(*n);
        return n;
}

static inline RBNode *rbnode_parent(RBNode *n) {
        return (RBNode*)((unsigned long)n->__parent_and_color & ~1UL);
}

static inline int rbnode_linked(RBNode *n) {
        return n && n->__parent_and_color != n;
}

static inline unsigned long rbnode_color(RBNode *n) {
        return (unsigned long)n->__parent_and_color & 1UL;
}

static inline int rbnode_red(RBNode *n) {
        return rbnode_color(n) == RBNODE_RED;
}

static inline int rbnode_black(RBNode *n) {
        return rbnode_color(n) == RBNODE_BLACK;
}

RBNode *rbnode_leftmost(RBNode *n);
RBNode *rbnode_rightmost(RBNode *n);

RBNode *rbtree_first(RBTree *t);
RBNode *rbtree_last(RBTree *t);
RBNode *rbnode_next(RBNode *n);
RBNode *rbnode_prev(RBNode *n);

void rbtree_add(RBTree *t, RBNode *p, RBNode **l, RBNode *n);
void rbtree_remove(RBTree *t, RBNode *n);

#ifdef __cplusplus
}
#endif
