/* -*- mode: C -*-  */
/* 
   IGraph library.
   Copyright (C) 2010  Gabor Csardi <csardi.gabor@gmail.com>
   Rue de l'Industrie 5, Lausanne 1005, Switzerland
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 
   02110-1301 USA

*/

#include <igraph.h>

int main() {

  igraph_t g, gbar;
  igraph_integer_t k1, k2 = (igraph_integer_t) IGRAPH_INFINITY;
  igraph_real_t tmpk;
  long int i, j, n;

  /* --------------------------------------------------- */
  
  igraph_famous(&g, "meredith");
  igraph_even_tarjan_reduction(&g, &gbar, /*capacity=*/ 0);
  
  igraph_vertex_connectivity(&g, &k1, /* checks= */ 0);

  n=igraph_vcount(&g);
  for (i=0; i<n; i++) {
    for (j=i+1; j<n; j++) {
      igraph_bool_t conn;
      igraph_are_connected(&g, i, j, &conn);
      if (conn) { continue; }
      igraph_maxflow_value(&gbar, &tmpk, 
			   /* source= */ i + n, 
			   /* target= */ j, 
			   /* capacity= */ 0);
      if (tmpk < k2) { k2=tmpk; }
    }
  }

  igraph_destroy(&gbar);
  igraph_destroy(&g);
  
  if (k1 != k2) {
    printf("k1 = %ld while k2 = %ld\n", (long int) k1, (long int) k2);
    return 1;
  }

  return 0;
}