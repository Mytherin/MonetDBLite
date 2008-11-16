/**
 * @file
 *
 * This property phase follows a list of columns starting from a start
 * operator until a specified goal operator is reached.
 *
 * Copyright Notice:
 * -----------------
 *
 * The contents of this file are subject to the Pathfinder Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License.  You may obtain a copy of the License at
 * http://monetdb.cwi.nl/Legal/PathfinderLicense-1.1.html
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.  See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is the Pathfinder system.
 *
 * The Original Code has initially been developed by the Database &
 * Information Systems Group at the University of Konstanz, Germany and
 * is now maintained by the Database Systems Group at the Technische
 * Universitaet Muenchen, Germany.  Portions created by the University of
 * Konstanz and the Technische Universitaet Muenchen are Copyright (C)
 * 2000-2005 University of Konstanz and (C) 2005-2008 Technische
 * Universitaet Muenchen, respectively.  All Rights Reserved.
 *
 * $Id$
 */

/* always include pathfinder.h first! */
#include "pathfinder.h"
#include <assert.h>
#include <stdio.h>

#include "properties.h"
#include "alg_dag.h"
#include "oops.h"
#include "mem.h"

/* Easily access subtree-parts */
#include "child_mnemonic.h"

#define CUR_AT(n,i) (((name_pair_t *) PFarray_at ((n), (i)))->unq)
#define ORI_AT(n,i) (((name_pair_t *) PFarray_at ((n), (i)))->ori)

/**
 * Add a new original name/current name pair to the list of name pairs @a np
 */
static void
add_name_pair (PFarray_t *np, PFalg_att_t ori, PFalg_att_t cur)
{
    assert (np);

    *(name_pair_t *) PFarray_add (np)
        = (name_pair_t) { .ori = ori, .unq = cur };
}

/**
 * diff_np marks the name pair invalid that is associated
 * with the current attribute name @a cur.
 */
static void
diff_np (PFarray_t *np_list, PFalg_att_t cur)
{
    for (unsigned int i = 0; i < PFarray_last (np_list); i++)
        if (CUR_AT(np_list, i) == cur) {
            /* mark the name pair as invalid */
            CUR_AT(np_list, i) = att_NULL;
            break;
        }
}

/**
 * Worker for PFprop_trace_names(). It recursively propagates
 * the name pair list.
 */
static void
map_names (PFla_op_t *n, PFla_op_t *goal, PFarray_t *par_np_list,
           PFalg_att_t twig_iter)
{
    PFarray_t  *np_list = n->prop->name_pairs;

    assert (n);
    assert (np_list);
    assert (par_np_list);

    /* we do not trace along fragment edges */
    if (n->kind == la_frag_union ||
        n->kind == la_empty_frag ||
        n->kind == la_fragment)
        ;
    else
        /* collect all name pair lists of the parent operators and
           include (possibly) new matching columns in the name pairs list */
        for (unsigned int i = 0; i < PFarray_last (par_np_list); i++)
            add_name_pair (np_list,
                           ORI_AT(par_np_list, i),
                           CUR_AT(par_np_list, i));

    /* nothing to do if we haven't collected
       all incoming name pair lists of that node */
    PFprop_refctr (n) = PFprop_refctr (n) - 1;
    if (PFprop_refctr (n) > 0)
        return;

    /* If we reached our goal we can return.
       (The resulting name pair list is accessible
        using the reference to operator goal.) */
    if (n == goal)
        return;

    /* Remove all the name pairs from the list whose current column name
       is generated by this operator and map the current name in case
       this operator is a renaming projection. */
    switch (n->kind) {
        case la_serialize_seq:
        case la_serialize_rel:
        case la_select:
        case la_pos_select:
        case la_distinct:
        case la_disjunion:
        case la_intersect:
        case la_difference:
        case la_type_assert:
        case la_roots:
        case la_error:
        case la_proxy:
        case la_proxy_base:
        case la_dummy:
            break;

        case la_lit_tbl:
        case la_empty_tbl:
        case la_ref_tbl:
            /* All name tracing columns that reach base operators
               are bound to be problematic for the goal operator.
               They are either introduced inside the proxy or
               conflict with a goal input column. So we let the
               goal operator know about the generated columns. */
            for (unsigned int i = 0; i < PFarray_last (np_list); i++) {
                add_name_pair (goal->prop->name_pairs,
                               ORI_AT(np_list, i),
                               att_NULL);
            }
            break;

        case la_attach:
            diff_np (np_list, n->sem.attach.res);
            break;

        case la_project:
        {
            unsigned int j;
            /* if we have no additional name pair list then create one */
            if (!n->prop->l_name_pairs)
               n->prop->l_name_pairs = PFarray (sizeof (name_pair_t), 10);

            for (unsigned int i = 0; i < PFarray_last (np_list); i++) {
                /* ensure that we don't forget anything about modified columns */
                if (CUR_AT(np_list, i) == att_NULL) {
                    add_name_pair (n->prop->l_name_pairs,
                                   ORI_AT(np_list, i),
                                   CUR_AT(np_list, i));
                }
                else {
                    /* Adjust all current column names for the columns
                       in the projection list and prune the out of scope
                       names. */
                    for (j = 0; j < n->sem.proj.count; j++)
                        if (n->sem.proj.items[j].new == CUR_AT(np_list, i)) {
                            add_name_pair (n->prop->l_name_pairs,
                                           ORI_AT(np_list, i),
                                           n->sem.proj.items[j].old);
                            break;
                        }
                }
            }
            map_names (L(n), goal, n->prop->l_name_pairs, att_NULL);
        }   return;

        case la_cross:
        case la_eqjoin:
        case la_semijoin:
        case la_thetajoin:
        {
            unsigned int j;
            /* if we have no additional name pair list then create one */
            if (!n->prop->l_name_pairs)
               n->prop->l_name_pairs = PFarray (sizeof (name_pair_t), 10);

            /* split up the name mappings */
            for (unsigned int i = 0; i < PFarray_last (np_list); i++) {
                /* ensure that we don't forget anything about
                   modified columns (thus add them into both branches) */
                if (CUR_AT(np_list, i) == att_NULL) {
                    add_name_pair (n->prop->l_name_pairs,
                                   ORI_AT(np_list, i),
                                   CUR_AT(np_list, i));
                }
                else {
                    for (j = 0; j < L(n)->schema.count; j++)
                        if (L(n)->schema.items[j].name == CUR_AT(np_list, i)) {
                            add_name_pair (n->prop->l_name_pairs,
                                           ORI_AT(np_list, i),
                                           CUR_AT(np_list, i));
                            break;
                        }
                }
            }
            map_names (L(n), goal, n->prop->l_name_pairs, att_NULL);
            PFarray_last (n->prop->l_name_pairs) = 0;

            if (n->kind == la_semijoin) {
                /* only propagate the name of the join column */
                for (unsigned int i = 0; i < PFarray_last (np_list); i++)
                    if (n->sem.eqjoin.att2 == CUR_AT(np_list, i)) {
                        add_name_pair (n->prop->l_name_pairs,
                                       ORI_AT(np_list, i),
                                       CUR_AT(np_list, i));
                        break;
                    }
            }
            else
                for (unsigned int i = 0; i < PFarray_last (np_list); i++) {
                    /* ensure that we don't forget anything about
                       modified columns (thus add them into both branches) */
                    if (CUR_AT(np_list, i) == att_NULL) {
                        add_name_pair (n->prop->l_name_pairs,
                                       ORI_AT(np_list, i),
                                       CUR_AT(np_list, i));
                    }
                    else {
                        for (j = 0; j < R(n)->schema.count; j++)
                            if (R(n)->schema.items[j].name == CUR_AT(np_list,
                                                                     i)) {
                                add_name_pair (n->prop->l_name_pairs,
                                               ORI_AT(np_list, i),
                                               CUR_AT(np_list, i));
                                break;
                            }
                    }
                }
            map_names (R(n), goal, n->prop->l_name_pairs, att_NULL);
            PFarray_last (n->prop->l_name_pairs) = 0;
        }   return;

        case la_fun_1to1:
            diff_np (np_list, n->sem.fun_1to1.res);
            break;

        case la_num_eq:
        case la_num_gt:
        case la_bool_and:
        case la_bool_or:
        case la_to:
            diff_np (np_list, n->sem.binary.res);
            break;

        case la_bool_not:
            diff_np (np_list, n->sem.unary.res);
            break;

        case la_count:
        case la_avg:
        case la_max:
        case la_min:
        case la_sum:
        case la_seqty1:
        case la_all:
            diff_np (np_list, n->sem.aggr.res);
            break;

        case la_rownum:
        case la_rowrank:
        case la_rank:
            diff_np (np_list, n->sem.sort.res);
            break;

        case la_rowid:
            diff_np (np_list, n->sem.rowid.res);
            break;

        case la_type:
        case la_cast:
            diff_np (np_list, n->sem.type.res);
            break;

        case la_step:
        case la_step_join:
        case la_guide_step:
        case la_guide_step_join:
            diff_np (np_list, n->sem.step.item_res);
            break;

        case la_doc_index_join:
            diff_np (np_list, n->sem.doc_join.item_res);
            break;

        case la_doc_tbl:
            diff_np (np_list, n->sem.doc_tbl.res);
            break;

        case la_doc_access:
            diff_np (np_list, n->sem.doc_access.res);
            break;

        case la_twig:
            diff_np (np_list, n->sem.iter_item.item);
            /* make sure the underlying constructors
               propagate the correct information */
            map_names (L(n), goal, np_list, n->sem.iter_item.iter);
            return;

        case la_fcns:
            map_names (L(n), goal, np_list, twig_iter);
            map_names (R(n), goal, np_list, twig_iter);
            break;

        case la_docnode:
            for (unsigned int i = 0; i < PFarray_last (np_list); i++)
                /* Adjust all current column names for the loop columns. */
                if (twig_iter == CUR_AT(np_list, i))
                    CUR_AT(np_list, i) = n->sem.docnode.iter;

            /* infer properties for children and
               return the resulting mapping */
            map_names (L(n), goal, np_list, att_NULL);
            map_names (R(n), goal, np_list, n->sem.docnode.iter);
            return;

        case la_element:
            for (unsigned int i = 0; i < PFarray_last (np_list); i++)
                /* Adjust all current column names for the loop columns. */
                if (twig_iter == CUR_AT(np_list, i))
                    CUR_AT(np_list, i) = n->sem.iter_item.iter;

            /* infer properties for children and
               return the resulting mapping */
            map_names (L(n), goal, np_list, att_NULL);
            map_names (R(n), goal, np_list, n->sem.iter_item.iter);
            return;

        case la_textnode:
        case la_comment:
            for (unsigned int i = 0; i < PFarray_last (np_list); i++)
                /* Adjust all current column names for the loop columns. */
                if (twig_iter == CUR_AT(np_list, i))
                    CUR_AT(np_list, i) = n->sem.iter_item.iter;

            /* infer properties for children and
               return the resulting mapping */
            map_names (L(n), goal, np_list, att_NULL);
            return;

        case la_attribute:
        case la_processi:
            for (unsigned int i = 0; i < PFarray_last (np_list); i++)
                /* Adjust all current column names for the loop columns. */
                if (twig_iter == CUR_AT(np_list, i))
                    CUR_AT(np_list, i) = n->sem.iter_item1_item2.iter;

            /* infer properties for children and
               return the resulting mapping */
            map_names (L(n), goal, np_list, att_NULL);
            return;

        case la_content:
            for (unsigned int i = 0; i < PFarray_last (np_list); i++)
                /* Adjust all current column names for the loop columns. */
                if (twig_iter == CUR_AT(np_list, i))
                    CUR_AT(np_list, i) = n->sem.iter_pos_item.iter;

            /* infer properties for children and
               return the resulting mapping */
            map_names (R(n), goal, np_list, att_NULL);

            /* empty the name pair list */
            PFarray_last (np_list) = 0;
            map_names (L(n), goal, np_list, att_NULL);
            return;

        case la_merge_adjacent:
            assert (n->sem.merge_adjacent.iter_res ==
                    n->sem.merge_adjacent.iter_in);
            diff_np (np_list, n->sem.merge_adjacent.item_res);
            break;

        case la_fragment:
        case la_frag_extract:
        case la_frag_union:
        case la_empty_frag:
            /* do not infer name pairs to the children */

            /* empty the name pair list */
            PFarray_last (np_list) = 0;
            break;

        case la_cond_err:
        case la_trace:
        case la_trace_msg:
        case la_trace_map:
            /* do the recursive calls by hand */
            map_names (L(n), goal, np_list, att_NULL);
            /* empty the name pair list */
            PFarray_last (np_list) = 0;
            map_names (R(n), goal, np_list, att_NULL);
            return;

        case la_nil:
            /* we do not have properties */
            break;

        case la_rec_fix:
        case la_rec_param:
        case la_rec_arg:
        case la_rec_base:
            PFoops (OOPS_FATAL,
                    "The column name tracing cannot "
                    "handle recursion operator.");
            break;

        case la_fun_call:
        case la_fun_param:
        case la_fun_frag_param:
            /* empty the name pair list */
            PFarray_last (np_list) = 0;
            break;

        case la_string_join:
            assert (n->sem.string_join.iter == n->sem.string_join.iter_res &&
                    n->sem.string_join.iter_sep == n->sem.string_join.iter_res);
            diff_np (np_list, n->sem.string_join.item_res);
            break;

        case la_internal_op:
            PFoops (OOPS_FATAL,
                    "internal optimization operator is not allowed here");
    }

    /* infer properties for children and
       return the resulting mapping */
    if (L(n)) map_names (L(n), goal, np_list, att_NULL);
    if (R(n)) map_names (R(n), goal, np_list, att_NULL);
}

#ifndef NDEBUG
/* check for the goal operator */
static bool
find_goal (PFla_op_t *n, PFla_op_t *goal)
{
    bool found_goal = n == goal;

    assert (n);

    /* nothing to do if we already visited that node */
    if (n->bit_dag)
        return false;

    n->bit_dag = true;

    /* infer properties for children */
    for (unsigned int i = 0; i < PFLA_OP_MAXCHILD && n->child[i]; i++)
        found_goal = find_goal (n->child[i], goal) || found_goal;

    return found_goal;
}
#endif

/* reset the old property information */
static void
reset_fun (PFla_op_t *n)
{
    /* reset the name mapping structure */
    if (n->prop->name_pairs)
        PFarray_last (n->prop->name_pairs) = 0;
    else
        n->prop->name_pairs = PFarray (sizeof (name_pair_t), 10);

    if (n->prop->l_name_pairs) PFarray_last (n->prop->l_name_pairs) = 0;
}

/**
 * Trace a list of column names starting from the start
 * operator until the goal operator is reached.
 */
PFalg_attlist_t
PFprop_trace_names (PFla_op_t *start,
                    PFla_op_t *goal,
                    PFalg_attlist_t list)
{
    PFalg_attlist_t new_list;
    unsigned int    i,
                    j;
    PFarray_t      *map_list = PFarray (sizeof (name_pair_t), list.count),
                   *new_map_list;

    /* collect number of incoming edges (parents) */
    PFprop_infer_refctr (start);

    /* reset the old property information */
    PFprop_reset (start, reset_fun);

    /* check for goal */
    assert (find_goal (start, goal));
    PFla_dag_reset (start);

    /* intialize the projection list */
    for (i = 0; i < list.count; i++)
        add_name_pair (map_list, list.atts[i], list.atts[i]);

    /* collect the mapped names */
    map_names (start, goal, map_list, att_NULL);
    new_map_list = goal->prop->name_pairs;
    assert (new_map_list);
    
    /* prune duplicate column name mappings and mark
       the name mappings that stem from multiple columns
       as invalid */
    for (i = 0; i < PFarray_last (new_map_list); i++) {
        for (j = i+1; j < PFarray_last (new_map_list); j++)
            if (ORI_AT(new_map_list, i) == ORI_AT(new_map_list, j)) {
                /* Mark the name pair as invalid if we have conflicting
                   current names. */
                if (CUR_AT(new_map_list, i) != CUR_AT(new_map_list, j))
                    CUR_AT(new_map_list, i) = att_NULL;

                /* remove the entry at index j */
                *(name_pair_t *) PFarray_at (new_map_list, j)
                    = *(name_pair_t *) PFarray_top (new_map_list);
                PFarray_del (new_map_list);
                j--;
            }
    }

    /* create new list */
    new_list.count = list.count;
    new_list.atts  = PFmalloc (list.count * sizeof (PFalg_att_t));

    /* fill the list of mapped variable names */
    for (i = 0; i < list.count; i++) {
        for (j = 0; j < PFarray_last (new_map_list); j++)
            if (list.atts[i] == ORI_AT(new_map_list, j)) {
                new_list.atts[i] = CUR_AT(new_map_list, j);
                break;
            }
        if (j == PFarray_last (new_map_list))
            /* fill in NULL if the name column was
               introduced after the goal operator */
            new_list.atts[i] = att_NULL;
    }

    return new_list;
}

/* vim:set shiftwidth=4 expandtab: */
