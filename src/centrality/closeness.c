/* -*- mode: C -*-  */
/* vim:set ts=4 sw=4 sts=4 et: */
/*
   IGraph library.
   Copyright (C) 2007-2020  The igraph development team <igraph@igraph.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "igraph_centrality.h"
#include "igraph_adjlist.h"
#include "igraph_interface.h"
#include "igraph_progress.h"
#include "igraph_dqueue.h"

#include "core/indheap.h"
#include "core/interruption.h"
#include "core/math.h"

/**
 * \ingroup structural
 * \function igraph_closeness
 * \brief Closeness centrality calculations for some vertices.
 *
 * </para><para>
 * The closeness centrality of a vertex measures how easily other
 * vertices can be reached from it (or the other way: how easily it
 * can be reached from the other vertices). It is defined as
 * the number of vertices minus one divided by the sum of the
 * lengths of all geodesics from/to the given vertex.
 *
 * </para><para>
 * If the graph is not connected, and there is no path between two
 * vertices, the number of vertices is used instead the length of the
 * geodesic. This is longer than the longest possible geodesic in case
 * of unweighted graphs, but may not be so in weighted graphs, so it is
 * best not to use this function on weighted graphs.
 *
 * </para><para>
 * If the graph has a single vertex only, the closeness centrality of
 * that single vertex will be NaN (because we are essentially dividing
 * zero with zero).
 *
 * \param graph The graph object.
 * \param res The result of the computation, a vector containing the
 *        closeness centrality scores for the given vertices.
 * \param vids The vertices for which the closeness centrality will be computed.
 * \param mode The type of shortest paths to be used for the
 *        calculation in directed graphs. Possible values:
 *        \clist
 *        \cli IGRAPH_OUT
 *          the lengths of the outgoing paths are calculated.
 *        \cli IGRAPH_IN
 *          the lengths of the incoming paths are calculated.
 *        \cli IGRAPH_ALL
 *          the directed graph is considered as an
 *          undirected one for the computation.
 *        \endclist
 * \param weights An optional vector containing edge weights for
 *        weighted closeness. Supply a null pointer here for
 *        traditional, unweighted closeness.
 * \param normalized Boolean, whether to normalize results by multiplying
 *        by the number of vertices minus one.
 * \return Error code:
 *        \clist
 *        \cli IGRAPH_ENOMEM
 *           not enough memory for temporary data.
 *        \cli IGRAPH_EINVVID
 *           invalid vertex id passed.
 *        \cli IGRAPH_EINVMODE
 *           invalid mode argument.
 *        \endclist
 *
 * Time complexity: O(n|E|),
 * n is the number
 * of vertices for which the calculation is done and
 * |E| is the number
 * of edges in the graph.
 *
 * \sa Other centrality types: \ref igraph_degree(), \ref igraph_betweenness().
 *   See \ref igraph_closeness_estimate() to estimate closeness values.
 */
int igraph_closeness(const igraph_t *graph, igraph_vector_t *res,
                     const igraph_vs_t vids, igraph_neimode_t mode,
                     const igraph_vector_t *weights,
                     igraph_bool_t normalized) {
    return igraph_closeness_estimate(graph, res, vids, mode, -1, weights,
                                     normalized);
}

static int igraph_i_closeness_estimate_weighted(const igraph_t *graph,
                                                igraph_vector_t *res,
                                                const igraph_vs_t vids,
                                                igraph_neimode_t mode,
                                                igraph_real_t cutoff,
                                                const igraph_vector_t *weights,
                                                igraph_bool_t normalized) {

    /* See igraph_shortest_paths_dijkstra() for the implementation
       details and the dirty tricks. */

    igraph_real_t minweight;
    long int no_of_nodes = igraph_vcount(graph);
    long int no_of_edges = igraph_ecount(graph);

    igraph_2wheap_t Q;
    igraph_vit_t vit;
    long int nodes_to_calc;

    igraph_lazy_inclist_t inclist;
    long int i, j;

    igraph_vector_t dist;
    igraph_vector_long_t which;
    long int nodes_reached;

    int cmp_result;
    const double eps = IGRAPH_SHORTEST_PATH_EPSILON;

    igraph_real_t mindist = 0;

    igraph_bool_t warning_shown = 0;

    if (igraph_vector_size(weights) != no_of_edges) {
        IGRAPH_ERROR("Invalid weight vector length", IGRAPH_EINVAL);
    }

    minweight = igraph_vector_min(weights);
    if (minweight <= 0) {
        IGRAPH_ERROR("Weight vector must be positive", IGRAPH_EINVAL);
    } else if (minweight <= eps) {
        IGRAPH_WARNING("Some weights are smaller than epsilon, calculations may suffer from numerical precision.");
    }

    IGRAPH_CHECK(igraph_vit_create(graph, vids, &vit));
    IGRAPH_FINALLY(igraph_vit_destroy, &vit);

    nodes_to_calc = IGRAPH_VIT_SIZE(vit);

    IGRAPH_CHECK(igraph_2wheap_init(&Q, no_of_nodes));
    IGRAPH_FINALLY(igraph_2wheap_destroy, &Q);
    IGRAPH_CHECK(igraph_lazy_inclist_init(graph, &inclist, mode));
    IGRAPH_FINALLY(igraph_lazy_inclist_destroy, &inclist);

    IGRAPH_VECTOR_INIT_FINALLY(&dist, no_of_nodes);
    IGRAPH_CHECK(igraph_vector_long_init(&which, no_of_nodes));
    IGRAPH_FINALLY(igraph_vector_long_destroy, &which);

    IGRAPH_CHECK(igraph_vector_resize(res, nodes_to_calc));
    igraph_vector_null(res);

    for (i = 0; !IGRAPH_VIT_END(vit); IGRAPH_VIT_NEXT(vit), i++) {

        long int source = IGRAPH_VIT_GET(vit);
        igraph_2wheap_clear(&Q);
        igraph_2wheap_push_with_index(&Q, source, -1.0);
        VECTOR(which)[source] = i + 1;
        VECTOR(dist)[source] = 1.0;     /* actual distance is zero but we need to store distance + 1 */
        nodes_reached = 0;

        while (!igraph_2wheap_empty(&Q)) {
            igraph_integer_t minnei = (igraph_integer_t) igraph_2wheap_max_index(&Q);
            /* Now check all neighbors of minnei for a shorter path */
            igraph_vector_t *neis = igraph_lazy_inclist_get(&inclist, minnei);
            long int nlen = igraph_vector_size(neis);

            mindist = -igraph_2wheap_delete_max(&Q);

            VECTOR(*res)[i] += (mindist - 1.0);
            nodes_reached++;

            if (cutoff >= 0 && mindist > cutoff + 1.0) {
                continue;    /* NOT break!!! */
            }

            for (j = 0; j < nlen; j++) {
                long int edge = (long int) VECTOR(*neis)[j];
                long int to = IGRAPH_OTHER(graph, edge, minnei);
                igraph_real_t altdist = mindist + VECTOR(*weights)[edge];
                igraph_real_t curdist = VECTOR(dist)[to];
                if (curdist == 0) {
                    /* this means curdist is infinity */
                    cmp_result = -1;
                } else {
                    cmp_result = igraph_cmp_epsilon(altdist, curdist, eps);
                }

                if (VECTOR(which)[to] != i + 1) {
                    /* First non-infinite distance */
                    VECTOR(which)[to] = i + 1;
                    VECTOR(dist)[to] = altdist;
                    IGRAPH_CHECK(igraph_2wheap_push_with_index(&Q, to, -altdist));
                } else if (cmp_result < 0) {
                    /* This is a shorter path */
                    VECTOR(dist)[to] = altdist;
                    IGRAPH_CHECK(igraph_2wheap_modify(&Q, to, -altdist));
                }
            }

        } /* !igraph_2wheap_empty(&Q) */

        /* using igraph_real_t here instead of igraph_integer_t to avoid overflow */
        VECTOR(*res)[i] += ((igraph_real_t)no_of_nodes * (no_of_nodes - nodes_reached));
        VECTOR(*res)[i] = (no_of_nodes - 1) / VECTOR(*res)[i];

        if (((cutoff >= 0 && mindist <= cutoff + 1.0) || (cutoff < 0)) &&
            nodes_reached < no_of_nodes && !warning_shown) {
            IGRAPH_WARNING("closeness centrality is not well-defined for disconnected graphs");
            warning_shown = 1;
        }
    } /* !IGRAPH_VIT_END(vit) */

    if (!normalized) {
        for (i = 0; i < nodes_to_calc; i++) {
            VECTOR(*res)[i] /= (no_of_nodes - 1);
        }
    }

    igraph_vector_long_destroy(&which);
    igraph_vector_destroy(&dist);
    igraph_lazy_inclist_destroy(&inclist);
    igraph_2wheap_destroy(&Q);
    igraph_vit_destroy(&vit);
    IGRAPH_FINALLY_CLEAN(5);

    return 0;
}

/**
 * \ingroup structural
 * \function igraph_closeness_estimate
 * \brief Closeness centrality estimations for some vertices.
 *
 * </para><para>
 * The closeness centrality of a vertex measures how easily other
 * vertices can be reached from it (or the other way: how easily it
 * can be reached from the other vertices). It is defined as
 * the number of vertices minus one divided by the sum of the
 * lengths of all geodesics from/to the given vertex. When estimating
 * closeness centrality, igraph considers paths having a length less than
 * or equal to a prescribed cutoff value.
 *
 * </para><para>
 * If the graph is not connected, and there is no such path between two
 * vertices, the number of vertices is used instead the length of the
 * geodesic. This is always longer than the longest possible geodesic.
 *
 * </para><para>
 * Since the estimation considers vertex pairs with a distance greater than
 * the given value as disconnected, the resulting estimation will always be
 * lower than the actual closeness centrality.
 *
 * \param graph The graph object.
 * \param res The result of the computation, a vector containing the
 *        closeness centrality scores for the given vertices.
 * \param vids The vertices for which the closeness centrality will be estimated.
 * \param mode The type of shortest paths to be used for the
 *        calculation in directed graphs. Possible values:
 *        \clist
 *        \cli IGRAPH_OUT
 *          the lengths of the outgoing paths are calculated.
 *        \cli IGRAPH_IN
 *          the lengths of the incoming paths are calculated.
 *        \cli IGRAPH_ALL
 *          the directed graph is considered as an
 *          undirected one for the computation.
 *        \endclist
 * \param cutoff The maximal length of paths that will be considered.
 *        If negative, the exact closeness will be calculated (no upper
 *        limit on path lengths).
 * \param weights An optional vector containing edge weights for
 *        weighted closeness. Supply a null pointer here for
 *        traditional, unweighted closeness.
 * \param normalized Boolean, whether to normalize results by multiplying
 *        by the number of vertices minus one.
 * \return Error code:
 *        \clist
 *        \cli IGRAPH_ENOMEM
 *           not enough memory for temporary data.
 *        \cli IGRAPH_EINVVID
 *           invalid vertex id passed.
 *        \cli IGRAPH_EINVMODE
 *           invalid mode argument.
 *        \endclist
 *
 * Time complexity: O(n|E|),
 * n is the number
 * of vertices for which the calculation is done and
 * |E| is the number
 * of edges in the graph.
 *
 * \sa Other centrality types: \ref igraph_degree(), \ref igraph_betweenness().
 */
int igraph_closeness_estimate(const igraph_t *graph, igraph_vector_t *res,
                              const igraph_vs_t vids, igraph_neimode_t mode,
                              igraph_real_t cutoff,
                              const igraph_vector_t *weights,
                              igraph_bool_t normalized) {

    long int no_of_nodes = igraph_vcount(graph);
    igraph_vector_t already_counted;
    igraph_vector_int_t *neis;
    long int i, j;
    long int nodes_reached;
    igraph_adjlist_t allneis;

    long int actdist = 0;

    igraph_dqueue_t q;

    long int nodes_to_calc;
    igraph_vit_t vit;

    igraph_bool_t warning_shown = 0;

    if (weights) {
        return igraph_i_closeness_estimate_weighted(graph, res, vids, mode, cutoff,
                weights, normalized);
    }

    IGRAPH_CHECK(igraph_vit_create(graph, vids, &vit));
    IGRAPH_FINALLY(igraph_vit_destroy, &vit);

    nodes_to_calc = IGRAPH_VIT_SIZE(vit);

    if (mode != IGRAPH_OUT && mode != IGRAPH_IN &&
        mode != IGRAPH_ALL) {
        IGRAPH_ERROR("calculating closeness", IGRAPH_EINVMODE);
    }

    IGRAPH_VECTOR_INIT_FINALLY(&already_counted, no_of_nodes);
    IGRAPH_DQUEUE_INIT_FINALLY(&q, 100);

    IGRAPH_CHECK(igraph_adjlist_init(graph, &allneis, mode));
    IGRAPH_FINALLY(igraph_adjlist_destroy, &allneis);

    IGRAPH_CHECK(igraph_vector_resize(res, nodes_to_calc));
    igraph_vector_null(res);

    for (IGRAPH_VIT_RESET(vit), i = 0;
         !IGRAPH_VIT_END(vit);
         IGRAPH_VIT_NEXT(vit), i++) {
        igraph_dqueue_clear(&q);
        IGRAPH_CHECK(igraph_dqueue_push(&q, IGRAPH_VIT_GET(vit)));
        IGRAPH_CHECK(igraph_dqueue_push(&q, 0));
        nodes_reached = 1;
        VECTOR(already_counted)[(long int)IGRAPH_VIT_GET(vit)] = i + 1;

        IGRAPH_PROGRESS("Closeness: ", 100.0 * i / no_of_nodes, NULL);
        IGRAPH_ALLOW_INTERRUPTION();

        while (!igraph_dqueue_empty(&q)) {
            long int act = (long int) igraph_dqueue_pop(&q);
            actdist = (long int) igraph_dqueue_pop(&q);

            VECTOR(*res)[i] += actdist;

            if (cutoff >= 0 && actdist > cutoff) {
                continue;    /* NOT break!!! */
            }

            /* check the neighbors */
            neis = igraph_adjlist_get(&allneis, act);
            for (j = 0; j < igraph_vector_int_size(neis); j++) {
                long int neighbor = (long int) VECTOR(*neis)[j];
                if (VECTOR(already_counted)[neighbor] == i + 1) {
                    continue;
                }
                VECTOR(already_counted)[neighbor] = i + 1;
                nodes_reached++;
                IGRAPH_CHECK(igraph_dqueue_push(&q, neighbor));
                IGRAPH_CHECK(igraph_dqueue_push(&q, actdist + 1));
            }
        }

        /* using igraph_real_t here instead of igraph_integer_t to avoid overflow */
        VECTOR(*res)[i] += ((igraph_real_t)no_of_nodes * (no_of_nodes - nodes_reached));
        VECTOR(*res)[i] = (no_of_nodes - 1) / VECTOR(*res)[i];

        if (((cutoff >= 0 && actdist <= cutoff) || cutoff < 0) &&
            no_of_nodes > nodes_reached && !warning_shown) {
            IGRAPH_WARNING("closeness centrality is not well-defined for disconnected graphs");
            warning_shown = 1;
        }
    }

    if (!normalized) {
        for (i = 0; i < nodes_to_calc; i++) {
            VECTOR(*res)[i] /= (no_of_nodes - 1);
        }
    }

    IGRAPH_PROGRESS("Closeness: ", 100.0, NULL);

    /* Clean */
    igraph_dqueue_destroy(&q);
    igraph_vector_destroy(&already_counted);
    igraph_vit_destroy(&vit);
    igraph_adjlist_destroy(&allneis);
    IGRAPH_FINALLY_CLEAN(4);

    return 0;
}
