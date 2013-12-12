/*
 * basic test loading a graphml file with igraph
 * compile with:
 * gcc -I/usr/include/igraph -o loadgraph loadgraph.c -ligraph
 */

#include <stdio.h>
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
  int r = igraph_read_graph_graphml(&graph, graphFile, 0);
  fclose(graphFile);

  if(r != IGRAPH_SUCCESS) {
	printf("error loading graph file at %s\n", fileName);
	r = -3;
  } else {
	printf("sucessfully loaded graph file at %s\n", fileName);
	r = 0;
  }

  igraph_destroy(&graph);
  return r;
}
