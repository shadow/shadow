/*!
A nom-based GML parser.
*/

use std::collections::HashMap;

use nom::{
    bytes::complete::{escaped_transform, is_not, tag, take, take_while},
    character::complete::{digit1, multispace0, multispace1, space0},
    character::{is_alphabetic, is_alphanumeric},
    combinator::{self, map_res, recognize, verify},
    error::{ErrorKind, FromExternalError, ParseError},
    sequence::tuple,
    IResult, Parser,
};

use crate::gml::{Edge, Gml, GmlItem, Node, Value};

pub trait GmlParseError<'a>:
    ParseError<&'a str>
    + FromExternalError<&'a str, std::num::ParseIntError>
    + FromExternalError<&'a str, std::num::ParseFloatError>
    + FromExternalError<&'a str, &'a str>
    + std::fmt::Debug
{
}
impl<'a, T> GmlParseError<'a> for T where
    T: ParseError<&'a str>
        + FromExternalError<&'a str, std::num::ParseIntError>
        + FromExternalError<&'a str, std::num::ParseFloatError>
        + FromExternalError<&'a str, &'a str>
        + std::fmt::Debug
{
}

/// Take `count` characters and make sure they all satisfy `cond`.
fn take_verify<'a, E: GmlParseError<'a>>(
    count: u32,
    cond: impl Fn(char) -> bool,
) -> impl Fn(&'a str) -> IResult<&'a str, &'a str, E> {
    move |i| verify(take(count), |s: &str| s.chars().all(&cond)).parse(i)
}

/// Parse a GML key.
pub fn key<'a, E: GmlParseError<'a>>(input: &'a str) -> IResult<&'a str, &'a str, E> {
    // a key starts with the a character [a-zA-Z_], and has remaining characters [a-zA-Z0-9_]
    let take_first = take_verify(1, |chr| is_alphabetic(chr as u8) || chr == '_');
    let take_remaining = take_while(|chr| is_alphanumeric(chr as u8) || chr == '_');
    let (input, key) = recognize(tuple((take_first, take_remaining))).parse(input)?;
    Ok((input, key))
}

/// Parse a GML item (a key + value).
pub fn item<'a, E: GmlParseError<'a>>(input: &'a str) -> IResult<&'a str, GmlItem<'a>, E> {
    match key(input)? {
        (input, "node") => node(input).map(|(input, node)| (input, GmlItem::Node(node))),
        (input, "edge") => edge(input).map(|(input, edge)| (input, GmlItem::Edge(edge))),
        (input, "directed") => {
            int_as_bool(input).map(|(input, value)| (input, GmlItem::Directed(value)))
        }
        (input, name) => {
            value(input).map(|(input, value)| (input, GmlItem::KeyValue((name.into(), value))))
        }
    }
}

/// Parse a GML graph.
pub fn gml<'a, E: GmlParseError<'a>>(input: &'a str) -> IResult<&'a str, Gml<'a>, E> {
    let (input, _) = multispace0(input)?;
    let (input, _) = tag("graph")(input)?;
    let (input, _) = space0(input)?;
    let (input, _) = tag("[")(input)?;
    let (input, _) = newline(input)?;

    let (input, (items, _)) = nom::multi::many_till(item, tag("]")).parse(input)?;

    let [nodes, edges, directed, others]: [Vec<_>; 4] = partition(items.into_iter(), |x| match x {
        GmlItem::Node(_) => 0,
        GmlItem::Edge(_) => 1,
        GmlItem::Directed(_) => 2,
        GmlItem::KeyValue(_) => 3,
    });

    let nodes: Vec<_> = nodes
        .into_iter()
        .map(|x| {
            if let GmlItem::Node(x) = x {
                x
            } else {
                panic!()
            }
        })
        .collect();
    let edges: Vec<_> = edges
        .into_iter()
        .map(|x| {
            if let GmlItem::Edge(x) = x {
                x
            } else {
                panic!()
            }
        })
        .collect();
    let others: Vec<_> = others
        .into_iter()
        .map(|x| {
            if let GmlItem::KeyValue(x) = x {
                x
            } else {
                panic!()
            }
        })
        .collect();

    if directed.len() > 1 {
        result_str_to_nom(
            input,
            Err("The 'directed' key must only be specified once"),
            ErrorKind::Fail,
        )?;
    }
    let directed = match directed.first() {
        Some(GmlItem::Directed(x)) => *x,
        Some(_) => panic!(),
        // GML graphs are undirected by default
        None => false,
    };

    let expected_len = others.len();
    let others: HashMap<_, _> = others.into_iter().collect();
    if others.len() != expected_len {
        result_str_to_nom(
            input,
            Err("Duplicate keys are not supported"),
            ErrorKind::Fail,
        )?;
    }

    let (input, _) = multispace0(input)?;

    Ok((
        input,
        Gml {
            directed,
            nodes,
            edges,
            other: others,
        },
    ))
}

/// Parse a GML node.
fn node<'a, E: GmlParseError<'a>>(input: &'a str) -> IResult<&'a str, Node<'a>, E> {
    let (input, _) = space0(input)?;
    let (input, _) = tag("[")(input)?;
    let (input, _) = newline(input)?;

    let (input, (key_values, _)) =
        nom::multi::many_till(tuple((key, value)), tag("]")).parse(input)?;
    let expected_len = key_values.len();
    let mut key_values: HashMap<_, _> = key_values.into_iter().collect();
    if key_values.len() != expected_len {
        result_str_to_nom(
            input,
            Err("Duplicate keys are not supported"),
            ErrorKind::Fail,
        )?;
    }

    let (input, _) = newline(input)?;

    let id = match key_values.remove("id") {
        Some(Value::Int(x)) => Some(x as u32),
        Some(_) => result_str_to_nom(input, Err("Incorrect 'id' type"), ErrorKind::Fail)?,
        None => None,
    };

    Ok((input, Node::new(id, key_values)))
}

/// Parse a GML edge.
fn edge<'a, E: GmlParseError<'a>>(input: &'a str) -> IResult<&'a str, Edge<'a>, E> {
    let (input, _) = space0(input)?;
    let (input, _) = tag("[")(input)?;
    let (input, _) = newline(input)?;

    let (input, (key_values, _)) =
        nom::multi::many_till(tuple((key, value)), tag("]")).parse(input)?;
    let expected_len = key_values.len();
    let mut key_values: HashMap<_, _> = key_values.into_iter().collect();
    if key_values.len() != expected_len {
        result_str_to_nom(
            input,
            Err("Duplicate keys are not supported"),
            ErrorKind::Fail,
        )?;
    }

    let (input, _) = newline(input)?;

    let source = match key_values.remove("source") {
        Some(Value::Int(x)) => x,
        Some(_) => result_str_to_nom(input, Err("Incorrect 'source' type"), ErrorKind::Fail)?,
        None => result_str_to_nom(input, Err("'source' doesn't exist"), ErrorKind::NoneOf)?,
    };

    let target = match key_values.remove("target") {
        Some(Value::Int(x)) => x,
        Some(_) => result_str_to_nom(input, Err("Incorrect 'target' type"), ErrorKind::Fail)?,
        None => result_str_to_nom(input, Err("'target' doesn't exist"), ErrorKind::NoneOf)?,
    };

    Ok((input, Edge::new(source as u32, target as u32, key_values)))
}

fn value<'a, E: GmlParseError<'a>>(input: &'a str) -> IResult<&'a str, Value<'a>, E> {
    let (input, _) = space0(input)?;

    let (input, (value, _)) = nom::branch::alt((
        tuple((int, newline)),
        tuple((float, newline)),
        tuple((string, newline)),
    ))
    .parse(input)?;

    Ok((input, value))
}

fn int<'a, E: GmlParseError<'a>>(input: &'a str) -> IResult<&'a str, Value<'a>, E> {
    let (input, value) = map_res(recognize(digit1), str::parse).parse(input)?;
    Ok((input, Value::Int(value)))
}

fn float<'a, E: GmlParseError<'a>>(input: &'a str) -> IResult<&'a str, Value<'a>, E> {
    let (input, value) =
        map_res(nom::number::complete::recognize_float, str::parse).parse(input)?;
    Ok((input, Value::Float(value)))
}

/// Parse a GML string.
fn string<'a, E: GmlParseError<'a>>(input: &'a str) -> IResult<&'a str, Value<'a>, E> {
    let (input, _) = tag("\"")(input)?;
    let (input, value) = escaped_transform(
        is_not("\""),
        '\\',
        nom::branch::alt((
            combinator::value("\\", tag("\\")),
            combinator::value("\"", tag("\"")),
        )),
    )(input)?;
    let (input, _) = tag("\"")(input)?;

    Ok((input, Value::Str(value.into())))
}

fn newline<'a, E: GmlParseError<'a>>(input: &'a str) -> IResult<&'a str, &'a str, E> {
    recognize(tuple((space0, multispace1, space0))).parse(input)
}

fn int_to_bool(x: i32) -> Result<bool, &'static str> {
    match x {
        1 => Ok(true),
        0 => Ok(false),
        _ => Err("Bool must be 0 or 1"),
    }
}

fn int_as_bool<'a, E: GmlParseError<'a>>(input: &'a str) -> IResult<&'a str, bool, E> {
    let (input, value) = value(input)?;

    let value = match value {
        Value::Int(x) => result_str_to_nom(input, int_to_bool(x), ErrorKind::Fail)?,
        _ => result_str_to_nom(input, Err("Value was not an integer"), ErrorKind::Fail)?,
    };

    Ok((input, value))
}

fn result_str_to_nom<'a, T, E: GmlParseError<'a>>(
    input: &'a str,
    result: Result<T, &'a str>,
    error_kind: ErrorKind,
) -> Result<T, nom::Err<E>> {
    result.map_err(|e| nom::Err::Failure(E::from_external_error(input, error_kind, e)))
}

fn partition<I, B, F, const N: usize>(iter: I, f: F) -> [B; N]
where
    I: Iterator + Sized,
    B: Default + Extend<I::Item>,
    [B; N]: Default,
    F: FnMut(&I::Item) -> usize,
{
    #[inline]
    fn extend<'a, T, B: Extend<T>, const N: usize>(
        mut f: impl FnMut(&T) -> usize + 'a,
        collections: &'a mut [B; N],
    ) -> impl FnMut((), T) + 'a {
        move |(), x| {
            collections[f(&x)].extend(Some(x));
        }
    }

    let mut collections: [B; N] = Default::default();

    iter.fold((), extend(f, &mut collections));

    collections
}
