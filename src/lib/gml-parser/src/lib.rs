/*!
A parser for the [Graph Modelling Language (GML)](https://web.archive.org/web/20190303094704/http://www.fim.uni-passau.de:80/fileadmin/files/lehrstuhl/brandenburg/projekte/gml/gml-technical-report.pdf) format.

Example graph:

```gml
graph [
  directed 1
  node [
    id 0
    label "Node 0"
  ]
  node [
    id 1
    label "Node 1"
  ]
  edge [
    source 0
    target 0
  ]
  edge [
    source 1
    target 0
  ]
]
```
*/

// https://github.com/rust-lang/rfcs/blob/master/text/2585-unsafe-block-in-unsafe-fn.md
#![deny(unsafe_op_in_unsafe_fn)]

pub mod gml;
mod parser;

use nom::Finish;

/// Parse the graph string into a [`gml::Gml`] object. If the graph contains syntax errors, a
/// human-readable error message will be returned.
/// ```
/// let graph = r#"
/// graph [
///   node [
///     id 0
///   ]
///   edge [
///     source 0
///     target 0
///   ]
/// ]"#;
/// let graph = match gml_parser::parse(graph) {
///     Ok(g) => g,
///     Err(e) => panic!("Could not parse graph: {}", e),
/// };
/// ```
pub fn parse(gml_str: &str) -> Result<gml::Gml, String> {
    match parser::gml::<nom_language::error::VerboseError<&str>>(gml_str).finish() {
        Ok((_remaining, graph)) => Ok(graph),
        Err(e) => Err(nom_language::error::convert_error(gml_str, e)),
    }
}
