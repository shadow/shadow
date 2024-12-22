/*!
Object types that represent a parsed GML graph.
*/

use std::borrow::Cow;
use std::collections::HashMap;

/// An item that represents a key-value pair. For example, `node [ ... ]`, `directed 0`,
/// `label "abc"`, etc.
#[derive(Debug, Clone, PartialEq)]
pub enum GmlItem<'a> {
    Node(Node<'a>),
    Edge(Edge<'a>),
    Directed(bool),
    KeyValue((Cow<'a, str>, Value<'a>)),
}

impl GmlItem<'_> {
    /// Convert any borrowed references to owned values.
    pub fn upgrade_to_owned(&self) -> GmlItem<'static> {
        match self {
            Self::Node(node) => GmlItem::Node(node.upgrade_to_owned()),
            Self::Edge(edge) => GmlItem::Edge(edge.upgrade_to_owned()),
            Self::Directed(directed) => GmlItem::Directed(*directed),
            Self::KeyValue((name, value)) => GmlItem::KeyValue((
                Cow::Owned(name.clone().into_owned()),
                value.upgrade_to_owned(),
            )),
        }
    }
}

/// A graph node with an `id` and `other` key-value pairs.
#[derive(Debug, Clone, PartialEq)]
pub struct Node<'a> {
    pub id: Option<u32>,
    pub other: HashMap<Cow<'a, str>, Value<'a>>,
}

impl<'a> Node<'a> {
    pub fn new<K>(id: Option<u32>, other: HashMap<K, Value<'a>>) -> Self
    where
        K: Into<Cow<'a, str>>,
    {
        let other = other.into_iter().map(|(k, v)| (k.into(), v)).collect();
        Self { id, other }
    }

    /// Convert any borrowed references to owned values.
    pub fn upgrade_to_owned(&self) -> Node<'static> {
        Node {
            id: self.id,
            other: self
                .other
                .iter()
                .map(|(k, v)| (Cow::Owned(k.clone().into_owned()), v.upgrade_to_owned()))
                .collect(),
        }
    }
}

/// A graph edge from node `source` to node `target` with `other` key-value pairs.
#[derive(Debug, Clone, PartialEq)]
pub struct Edge<'a> {
    pub source: u32,
    pub target: u32,
    pub other: HashMap<Cow<'a, str>, Value<'a>>,
}

impl<'a> Edge<'a> {
    pub fn new<K>(source: u32, target: u32, other: HashMap<K, Value<'a>>) -> Self
    where
        K: Into<Cow<'a, str>>,
    {
        let other = other.into_iter().map(|(k, v)| (k.into(), v)).collect();
        Self {
            source,
            target,
            other,
        }
    }

    /// Convert any borrowed references to owned values.
    pub fn upgrade_to_owned(&self) -> Edge<'static> {
        Edge {
            source: self.source,
            target: self.target,
            other: self
                .other
                .iter()
                .map(|(k, v)| (Cow::Owned(k.clone().into_owned()), v.upgrade_to_owned()))
                .collect(),
        }
    }
}

/// The base value types supported by GML.
#[derive(Debug, Clone, PartialEq)]
pub enum Value<'a> {
    Int(i32),
    Float(f32),
    Str(Cow<'a, str>),
}

impl<'a> Value<'a> {
    /// Returns a string if the value is a string. Otherwise returns `None`.
    pub fn as_str(self) -> Option<Cow<'a, str>> {
        if let Self::Str(s) = self {
            return Some(s);
        }
        None
    }

    /// Returns a float if the value is a float. Otherwise returns `None`.
    pub fn as_float(self) -> Option<f32> {
        if let Self::Float(f) = self {
            return Some(f);
        }
        None
    }

    /// Convert any borrowed references to owned values.
    pub fn upgrade_to_owned(&self) -> Value<'static> {
        match self {
            Self::Int(x) => Value::Int(*x),
            Self::Float(x) => Value::Float(*x),
            Self::Str(s) => Value::Str(Cow::Owned(s.clone().into_owned())),
        }
    }
}

/// A GML graph.
#[derive(Debug, PartialEq)]
pub struct Gml<'a> {
    pub directed: bool,
    pub nodes: Vec<Node<'a>>,
    pub edges: Vec<Edge<'a>>,
    pub other: HashMap<Cow<'a, str>, Value<'a>>,
}

impl Gml<'_> {
    /// Convert any borrowed references to owned values.
    pub fn upgrade_to_owned(&self) -> Gml<'static> {
        Gml {
            directed: self.directed,
            nodes: self.nodes.iter().map(|n| n.upgrade_to_owned()).collect(),
            edges: self.edges.iter().map(|e| e.upgrade_to_owned()).collect(),
            other: self
                .other
                .iter()
                .map(|(k, v)| (Cow::Owned(k.clone().into_owned()), v.upgrade_to_owned()))
                .collect(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn upgrade_to_owned() {
        let node;
        {
            // a string with a short lifetime
            let local_str = "abc".to_string();

            let mut node_options = HashMap::new();
            node_options.insert(&local_str, Value::Int(5));
            let node_with_reference = Node::new(Some(0), node_options);

            node = node_with_reference.upgrade_to_owned();
        }

        println!("{:?}", node);
    }
}
