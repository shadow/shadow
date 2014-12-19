/*
 * basic test loading a graphml file with igraph
 * compile with:
 * gcc -I/usr/include/igraph -o loadgraph loadgraph.c -ligraph
 */

#include <stdio.h>
#include <time.h>
#include <igraph.h>

int main(int argc, char* argv[]) {
  if(argc != 2) {
    printf("USAGE: %s path/to/file.graphml.xml\n", argv[0]);
    return -1;
  }

  char* fileName = argv[1];

  igraph_attribute_table_t* oldHandler = igraph_i_set_attribute_table(&igraph_cattribute_table);
  FILE* graphFile = fopen(fileName, "r");
  if(!graphFile) {
    printf("error opening graph file at %s\n", fileName);
    return -2;
  }

  igraph_t graph;
  time_t start = time(NULL);
  int r = igraph_read_graph_graphml(&graph, graphFile, 0);
  time_t end = time(NULL);
  fclose(graphFile);

  if(r != IGRAPH_SUCCESS) {
    printf("error loading graph file at %s\n", fileName);
    r = -3;
  } else {
    printf("successfully loaded graph file at %s in %li seconds\n", fileName, (long int)(end-start));
    r = 0;
  }

  printf("graph has %li vertices and %li edges\n", (long int)igraph_vcount(&graph), (long int)igraph_ecount(&graph));

  igraph_integer_t largestCliqueSize = 0;
  start = time(NULL);
  r = igraph_clique_number(&graph, &largestCliqueSize);
  end = time(NULL);

  if(r != IGRAPH_SUCCESS) {
    printf("error computing igraph_clique_number\n");
    r = -4;
  } else {
    printf("igraph_clique_number = %i in %li seconds\n", (int) largestCliqueSize, (long int)(end-start));
    r = 0;
  }

  start = time(NULL);
  igraph_destroy(&graph);
  end = time(NULL);
  printf("igraph_destroy finished in %li seconds\n", (long int)end-start);

  return r;
}
