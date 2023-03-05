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

/*
 * Open Red-Black-Tree Implementation
 * You're highly recommended to read a paper on rb-trees before reading this
 * code. The reader is expected to be familiar with the different cases that
 * might occur during insertion and removal of elements. The comments in this
 * file do not contain a full prove for correctness.
 */

#include "shared-rbtree.h"

RBNode *rbnode_leftmost(RBNode *n) {
        if (n)
                while (n->left)
                        n = n->left;
        return n;
}

RBNode *rbnode_rightmost(RBNode *n) {
        if (n)
                while (n->right)
                        n = n->right;
        return n;
}

RBNode *rbtree_first(RBTree *t) {
        return rbnode_leftmost(t->root);
}

RBNode *rbtree_last(RBTree *t) {
        return rbnode_rightmost(t->root);
}

RBNode *rbnode_next(RBNode *n) {
        RBNode *p;

        if (!rbnode_linked(n))
                return NULL;

        if (n->right)
                return rbnode_leftmost(n->right);

        while ((p = rbnode_parent(n)) && n == p->right)
                n = p;

        return p;
}

RBNode *rbnode_prev(RBNode *n) {
        RBNode *p;

        if (!rbnode_linked(n))
                return NULL;

        if (n->left)
                return rbnode_rightmost(n->left);

        while ((p = rbnode_parent(n)) && n == p->left)
                n = p;

        return p;
}

static inline void rbnode_reparent(RBNode *n, RBNode *p, unsigned long c) {
        /* change color and/or parent of a node */
        rbtree_assert(!((unsigned long)p & 1));
        rbtree_assert(c < 2);
        n->__parent_and_color = (RBNode*)((unsigned long)p | c);
}

static inline void rbtree_reparent(RBTree *t, RBNode *p, RBNode *old, RBNode *new) {
        /* change previous parent/root from old to new node */
        if (p) {
                if (p->left == old)
                        p->left = new;
                else
                        p->right = new;
        } else {
                t->root = new;
        }
}

static inline RBNode *rbtree_paint_one(RBTree *t, RBNode *n) {
        RBNode *p, *g, *gg, *u, *x;

        /*
         * Paint a single node according to RB-Tree rules. The node must
         * already be linked into the tree and painted red.
         * We repaint the node or rotate the tree, if required. In case a
         * recursive repaint is required, the next node to be re-painted
         * is returned.
         *      p: parent
         *      g: grandparent
         *      gg: grandgrandparent
         *      u: uncle
         *      x: temporary
         */

        /* node is red, so we can access the parent directly */
        p = n->__parent_and_color;

        if (!p) {
                /* Case 1:
                 * We reached the root. Mark it black and be done. As all
                 * leaf-paths share the root, the ratio of black nodes on each
                 * path stays the same. */
                rbnode_reparent(n, p, RBNODE_BLACK);
                n = NULL;
        } else if (rbnode_black(p)) {
                /* Case 2:
                 * The parent is already black. As our node is red, we did not
                 * change the number of black nodes on any path, nor do we have
                 * multiple consecutive red nodes. */
                n = NULL;
        } else if (p == p->__parent_and_color->left) { /* parent is red, so grandparent exists */
                g = p->__parent_and_color;
                gg = rbnode_parent(g);
                u = g->right;

                if (u && rbnode_red(u)) {
                        /* Case 3:
                         * Parent and uncle are both red. We know the
                         * grandparent must be black then. Repaint parent and
                         * uncle black, the grandparent red and recurse into
                         * the grandparent. */
                        rbnode_reparent(p, g, RBNODE_BLACK);
                        rbnode_reparent(u, g, RBNODE_BLACK);
                        rbnode_reparent(g, gg, RBNODE_RED);
                        n = g;
                } else {
                        /* parent is red, uncle is black */

                        if (n == p->right) {
                                /* Case 4:
                                 * We're the right child. Rotate on parent to
                                 * become left child, so we can handle it the
                                 * same as case 5. */
                                x = n->left;
                                p->right = n->left;
                                n->left = p;
                                if (x)
                                        rbnode_reparent(x, p, RBNODE_BLACK);
                                rbnode_reparent(p, n, RBNODE_RED);
                                p = n;
                        }

                        /* 'n' is invalid from here on! */
                        n = NULL;

                        /* Case 5:
                         * We're the red left child or a red parent, black
                         * grandparent and uncle. Rotate on grandparent and
                         * switch color with parent. Number of black nodes on
                         * each path stays the same, but we got rid of the
                         * double red path. As the grandparent is still black,
                         * we're done. */
                        x = p->right;
                        g->left = x;
                        p->right = g;
                        if (x)
                                rbnode_reparent(x, g, RBNODE_BLACK);
                        rbnode_reparent(p, gg, RBNODE_BLACK);
                        rbnode_reparent(g, p, RBNODE_RED);
                        rbtree_reparent(t, gg, g, p);
                }
        } else /* if (p == p->__parent_and_color->left) */ { /* same as above, but mirrored */
                g = p->__parent_and_color;
                gg = rbnode_parent(g);
                u = g->left;

                if (u && rbnode_red(u)) {
                        rbnode_reparent(p, g, RBNODE_BLACK);
                        rbnode_reparent(u, g, RBNODE_BLACK);
                        rbnode_reparent(g, gg, RBNODE_RED);
                        n = g;
                } else {
                        if (n == p->left) {
                                x = n->right;
                                p->left = n->right;
                                n->right = p;
                                if (x)
                                        rbnode_reparent(x, p, RBNODE_BLACK);
                                rbnode_reparent(p, n, RBNODE_RED);
                                p = n;
                        }

                        n = NULL;

                        x = p->left;
                        g->right = x;
                        p->left = g;
                        if (x)
                                rbnode_reparent(x, g, RBNODE_BLACK);
                        rbnode_reparent(p, gg, RBNODE_BLACK);
                        rbnode_reparent(g, p, RBNODE_RED);
                        rbtree_reparent(t, gg, g, p);
                }
        }

        return n;
}

static inline void rbtree_paint(RBTree *t, RBNode *n) {
        while (n)
                n = rbtree_paint_one(t, n);
}

void rbtree_add(RBTree *t, RBNode *p, RBNode **l, RBNode *n) {
        n->__parent_and_color = p;
        n->left = n->right = NULL;
        *l = n;

        rbtree_paint(t, n);
}

static inline RBNode *rbtree_rebalance_one(RBTree *t, RBNode *p, RBNode *n) {
        RBNode *s, *x, *y, *g;

        /*
         * Rebalance tree after a node was removed. This happens only if you
         * remove a black node and one path is now left with an unbalanced
         * number or black nodes.
         * This function assumes all paths through p and n have one black node
         * less than all other paths. If recursive fixup is required, the
         * current node is returned.
         */

        if (n == p->left) {
                s = p->right;
                if (rbnode_red(s)) {
                        /* Case 3:
                         * We have a red node as sibling. Rotate it onto our
                         * side so we can later on turn it black. This way, we
                         * gain the additional black node in our path. */
                        g = rbnode_parent(p);
                        x = s->left;
                        p->right = x;
                        s->left = p;
                        rbnode_reparent(x, p, RBNODE_BLACK);
                        rbnode_reparent(s, g, rbnode_color(p));
                        rbnode_reparent(p, s, RBNODE_RED);
                        rbtree_reparent(t, g, p, s);
                        s = x;
                }

                x = s->right;
                if (!x || rbnode_black(x)) {
                        y = s->left;
                        if (!y || rbnode_black(y)) {
                                /* Case 4:
                                 * Our sibling is black and has only black
                                 * children. Flip it red and turn parent black.
                                 * This way we gained a black node in our path,
                                 * or we fix it recursively one layer up, which
                                 * will rotate the red sibling as parent. */
                                rbnode_reparent(s, p, RBNODE_RED);
                                if (rbnode_black(p))
                                        return p;

                                rbnode_reparent(p, rbnode_parent(p), RBNODE_BLACK);
                                return NULL;
                        }

                        /* Case 5:
                         * Left child of our sibling is red, right one is black.
                         * Rotate on parent so the right child of our sibling is
                         * now red, and we can fall through to case 6. */
                        x = y->right;
                        s->left = y->right;
                        y->right = s;
                        p->right = y;
                        if (x)
                                rbnode_reparent(x, s, RBNODE_BLACK);
                        x = s;
                        s = y;
                }

                /* Case 6:
                 * The right child of our sibling is red. Rotate left and flip
                 * colors, which gains us an additional black node in our path,
                 * that was previously on our sibling. */
                g = rbnode_parent(p);
                y = s->left;
                p->right = y;
                s->left = p;
                rbnode_reparent(x, s, RBNODE_BLACK);
                if (y)
                        rbnode_reparent(y, p, rbnode_color(y));
                rbnode_reparent(s, g, rbnode_color(p));
                rbnode_reparent(p, s, RBNODE_BLACK);
                rbtree_reparent(t, g, p, s);
        } else /* if (!n || n == p->right) */ { /* same as above, but mirrored */
                s = p->left;
                if (rbnode_red(s)) {
                        g = rbnode_parent(p);
                        x = s->right;
                        p->left = x;
                        s->right = p;
                        rbnode_reparent(x, p, RBNODE_BLACK);
                        rbnode_reparent(s, g, RBNODE_BLACK);
                        rbnode_reparent(p, s, RBNODE_RED);
                        rbtree_reparent(t, g, p, s);
                        s = x;
                }

                x = s->left;
                if (!x || rbnode_black(x)) {
                        y = s->right;
                        if (!y || rbnode_black(y)) {
                                rbnode_reparent(s, p, RBNODE_RED);
                                if (rbnode_black(p))
                                        return p;

                                rbnode_reparent(p, rbnode_parent(p), RBNODE_BLACK);
                                return NULL;
                        }

                        x = y->left;
                        s->right = y->left;
                        y->left = s;
                        p->left = y;
                        if (x)
                                rbnode_reparent(x, s, RBNODE_BLACK);
                        x = s;
                        s = y;
                }

                g = rbnode_parent(p);
                y = s->right;
                p->left = y;
                s->right = p;
                rbnode_reparent(x, s, RBNODE_BLACK);
                if (y)
                        rbnode_reparent(y, p, rbnode_color(y));
                rbnode_reparent(s, g, rbnode_color(p));
                rbnode_reparent(p, s, RBNODE_BLACK);
                rbtree_reparent(t, g, p, s);
        }

        return NULL;
}

static inline void rbtree_rebalance(RBTree *t, RBNode *p) {
        RBNode *n = NULL;

        while (p) {
                n = rbtree_rebalance_one(t, p, n);
                p = n ? rbnode_parent(n) : NULL;
        }
}

void rbtree_remove(RBTree *t, RBNode *n) {
        RBNode *p, *s, *gc, *x, *next = NULL;
        unsigned long c;

        /*
         * To remove an interior node from a binary tree, we simply find its
         * successor, swap both nodes and then remove the node. Therefore, the
         * only interesting case is were the node to be removed has at most one
         * child.
         *      p: parent
         *      s: successor
         *      gc: grand-...-child
         *      x: temporary
         *      next: next node to rebalance on
         */

        if (!n->left) {
                /* Case 1:
                 * We have at most one child, which must be red. We're
                 * guaranteed to be black then. Replace our node with the child
                 * (in case it exists), but turn it black.
                 * If no child exists, we need to rebalance, in case we were
                 * black as our path now lost a black node. */
                p = rbnode_parent(n);
                c = rbnode_color(n);
                rbtree_reparent(t, p, n, n->right);
                if (n->right)
                        rbnode_reparent(n->right, p, c);
                else
                        next = (c == RBNODE_BLACK) ? p : NULL;
        } else if (!n->right) {
                /* case 1 mirrored (but n->left guaranteed non-NULL) */
                p = rbnode_parent(n);
                c = rbnode_color(n);
                rbnode_reparent(n->left, p, c);
                rbtree_reparent(t, p, n, n->left);
        } else {
                /* Case 2:
                 * We're an interior node. Find our successor and swap it with
                 * our node. Then remove our node. For performance reasons we
                 * don't perform the full swap, but skip links that are about to
                 * be removed, anyway. */
                s = n->right;
                if (!s->left) {
                        /* right child is next, no need to touch grandchild */
                        p = s;
                        gc = s->right;
                } else {
                        /* find successor and swap partially */
                        s = rbnode_leftmost(s);
                        p = rbnode_parent(s);

                        gc = s->right;
                        p->left = s->right;
                        s->right = n->right;
                        rbnode_reparent(n->right, s, rbnode_color(n->right));
                }

                /* node is partially swapped, now remove as in case 1 */

                s->left = n->left;
                rbnode_reparent(n->left, s, rbnode_color(n->left));

                x = rbnode_parent(n);
                c = rbnode_color(n);
                rbtree_reparent(t, x, n, s);
                if (gc) {
                        rbnode_reparent(s, x, c);
                        rbnode_reparent(gc, p, RBNODE_BLACK);
                } else {
                        next = rbnode_black(s) ? p : NULL;
                        rbnode_reparent(s, x, c);
                }
        }

        if (next)
                rbtree_rebalance(t, next);
}
