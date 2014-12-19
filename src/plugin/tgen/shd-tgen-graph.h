/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_GRAPH_H_
#define SHD_TGEN_GRAPH_H_

typedef struct _TGenGraph TGenGraph;

void tgengraph_free(TGenGraph* g);
TGenGraph* tgengraph_new(gchar* path);
TGenAction* tgengraph_getStartAction(TGenGraph* g);
GQueue* tgengraph_getNextActions(TGenGraph* g, TGenAction* action);
gboolean tgengraph_hasEdges(TGenGraph* g);
const gchar* tgengraph_getGraphPath(TGenGraph* g);

#endif /* SHD_TGEN_GRAPH_H_ */
