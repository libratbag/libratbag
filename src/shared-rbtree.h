#pragma once

/***
  This file is part of rbtree.

  Copyright 2015 David Herrmann <dh.herrmann@gmail.com>

  rbtree is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  rbtree is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with rbtree; If not, see <http://www.gnu.org/licenses/>.
***/

#include "config.h"

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
