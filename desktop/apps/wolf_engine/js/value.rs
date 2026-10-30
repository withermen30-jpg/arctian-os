use alloc::borrow::ToOwned;
use alloc::rc::Rc;
use alloc::string::String;
use alloc::vec::Vec;
use core::cell::RefCell;

use super::ast::Stmt;
use super::interp::Interp;

#[derive(Clone)]
pub enum Value {
    Undefined,
    Null,
    Bool(bool),
    Number(f64),
    Str(Rc<String>),
    Object(Rc<RefCell<Obj>>),
    Function(Rc<Func>),
}

pub enum ObjKind {
    Plain,
    Array(Vec<Value>),
    Host { handle: i64, tag: String },
    Style { handle: i64 },
    ClassList { handle: i64 },
}

pub struct Obj {
    pub kind: ObjKind,
    pub props: Vec<(String, Value)>,
}

pub type NativeFn = fn(&mut Interp, &Value, &[Value]) -> Result<Value, Control>;

pub struct Func {
    pub name: String,
    pub params: Vec<String>,
    pub body: Rc<Vec<Stmt>>,
    pub env: Rc<Env>,
    pub native: Option<NativeFn>,
    pub is_arrow: bool,
    pub is_ctor: bool,
    pub props: RefCell<Vec<(String, Value)>>,
}

pub enum Control {
    Return(Value),
    Break,
    Continue,
    Throw(Value),
    Error(String),
}

pub struct Env {
    pub vars: RefCell<Vec<(String, Value)>>,
    pub parent: Option<Rc<Env>>,
}

impl Env {
    pub fn new(parent: Option<Rc<Env>>) -> Rc<Env> {
        Rc::new(Env { vars: RefCell::new(Vec::new()), parent })
    }

    pub fn declare(&self, name: &str, v: Value) {
        let mut vars = self.vars.borrow_mut();
        for slot in vars.iter_mut() {
            if slot.0 == name { slot.1 = v; return; }
        }
        vars.push((name.to_owned(), v));
    }

    pub fn get(&self, name: &str) -> Option<Value> {
        for slot in self.vars.borrow().iter() {
            if slot.0 == name { return Some(slot.1.clone()); }
        }
        match &self.parent {
            Some(p) => p.get(name),
            None => None,
        }
    }

    pub fn assign(&self, name: &str, v: Value) -> bool {
        {
            let mut vars = self.vars.borrow_mut();
            for slot in vars.iter_mut() {
                if slot.0 == name { slot.1 = v; return true; }
            }
        }
        match &self.parent {
            Some(p) => p.assign(name, v),
            None => false,
        }
    }
}

pub fn new_object(kind: ObjKind) -> Value {
    Value::Object(Rc::new(RefCell::new(Obj { kind, props: Vec::new() })))
}

pub fn new_array(items: Vec<Value>) -> Value {
    new_object(ObjKind::Array(items))
}

pub fn num_to_string(n: f64) -> String {
    if n.is_nan() { return String::from("NaN"); }
    if n.is_infinite() { return String::from(if n > 0.0 { "Infinity" } else { "-Infinity" }); }
    if n == 0.0 { return String::from("0"); }
    if n == (n as i64) as f64 && n.abs() < 9.007199254740992e15 {
        return alloc::format!("{}", n as i64);
    }
    let mut s = alloc::format!("{}", n);
    if s.contains('e') {
        if let Ok(v) = s.parse::<f64>() {
            s = alloc::format!("{}", v);
        }
    }
    s
}

impl Value {
    pub fn str(s: &str) -> Value { Value::Str(Rc::new(String::from(s))) }
    pub fn string(s: String) -> Value { Value::Str(Rc::new(s)) }

    pub fn as_str(&self) -> Option<&str> {
        match self { Value::Str(s) => Some(s.as_str()), _ => None }
    }

    pub fn is_truthy(&self) -> bool {
        match self {
            Value::Undefined | Value::Null => false,
            Value::Bool(b) => *b,
            Value::Number(n) => *n != 0.0 && !n.is_nan(),
            Value::Str(s) => !s.is_empty(),
            _ => true,
        }
    }

    pub fn to_number(&self) -> f64 {
        match self {
            Value::Undefined => f64::NAN,
            Value::Null => 0.0,
            Value::Bool(b) => if *b { 1.0 } else { 0.0 },
            Value::Number(n) => *n,
            Value::Str(s) => str_to_number(s),
            _ => f64::NAN,
        }
    }

    pub fn type_of(&self) -> &'static str {
        match self {
            Value::Undefined => "undefined",
            Value::Null => "object",
            Value::Bool(_) => "boolean",
            Value::Number(_) => "number",
            Value::Str(_) => "string",
            Value::Object(_) => "object",
            Value::Function(_) => "function",
        }
    }

    pub fn strict_eq(&self, other: &Value) -> bool {
        match (self, other) {
            (Value::Undefined, Value::Undefined) => true,
            (Value::Null, Value::Null) => true,
            (Value::Bool(a), Value::Bool(b)) => a == b,
            (Value::Number(a), Value::Number(b)) => a == b,
            (Value::Str(a), Value::Str(b)) => a == b,
            (Value::Object(a), Value::Object(b)) => Rc::ptr_eq(a, b),
            (Value::Function(a), Value::Function(b)) => Rc::ptr_eq(a, b),
            _ => false,
        }
    }

    pub fn loose_eq(&self, other: &Value) -> bool {
        match (self, other) {
            (Value::Undefined | Value::Null, Value::Undefined | Value::Null) => true,
            (Value::Number(_), Value::Number(_)) => self.strict_eq(other),
            (Value::Str(_), Value::Str(_)) => self.strict_eq(other),
            (Value::Bool(_), Value::Bool(_)) => self.strict_eq(other),
            (Value::Number(a), Value::Str(_)) => *a == other.to_number(),
            (Value::Str(_), Value::Number(b)) => self.to_number() == *b,
            (Value::Bool(_), _) => Value::Number(self.to_number()).loose_eq(other),
            (_, Value::Bool(_)) => self.loose_eq(&Value::Number(other.to_number())),
            _ => self.strict_eq(other),
        }
    }

    pub fn get_prop(&self, name: &str) -> Option<Value> {
        match self {
            Value::Object(o) => {
                for p in o.borrow().props.iter() {
                    if p.0 == name { return Some(p.1.clone()); }
                }
                None
            }
            Value::Function(f) => {
                for p in f.props.borrow().iter() {
                    if p.0 == name { return Some(p.1.clone()); }
                }
                None
            }
            _ => None,
        }
    }

    pub fn set_prop(&self, name: &str, v: Value) {
        match self {
            Value::Object(o) => {
                let mut o = o.borrow_mut();
                for p in o.props.iter_mut() {
                    if p.0 == name { p.1 = v; return; }
                }
                o.props.push((name.to_owned(), v));
            }
            Value::Function(f) => {
                let mut props = f.props.borrow_mut();
                for p in props.iter_mut() {
                    if p.0 == name { p.1 = v; return; }
                }
                props.push((name.to_owned(), v));
            }
            _ => {}
        }
    }

    pub fn arr_len(&self) -> Option<usize> {
        match self {
            Value::Object(o) => match &o.borrow().kind {
                ObjKind::Array(a) => Some(a.len()),
                _ => None,
            },
            _ => None,
        }
    }

    pub fn to_display(&self) -> String {
        match self {
            Value::Undefined => String::from("undefined"),
            Value::Null => String::from("null"),
            Value::Bool(b) => String::from(if *b { "true" } else { "false" }),
            Value::Number(n) => num_to_string(*n),
            Value::Str(s) => (**s).clone(),
            Value::Function(f) => alloc::format!("function {}() {{ [native code] }}", f.name),
            Value::Object(o) => {
                let b = o.borrow();
                match &b.kind {
                    ObjKind::Array(a) => {
                        let mut out = String::new();
                        for (i, v) in a.iter().enumerate() {
                            if i > 0 { out.push(','); }
                            if !matches!(v, Value::Undefined | Value::Null) {
                                out.push_str(&v.to_display());
                            }
                        }
                        out
                    }
                    ObjKind::Host { tag, .. } => alloc::format!("[object {}]", capitalize(tag)),
                    _ => String::from("[object Object]"),
                }
            }
        }
    }
}

pub fn capitalize(s: &str) -> String {
    let mut out = String::new();
    let mut first = true;
    for c in s.chars() {
        if first { out.extend(c.to_uppercase()); first = false; }
        else { out.push(c); }
    }
    out
}

pub fn str_to_number(s: &str) -> f64 {
    let t = s.trim();
    if t.is_empty() { return 0.0; }
    if t == "Infinity" || t == "+Infinity" { return f64::INFINITY; }
    if t == "-Infinity" { return f64::NEG_INFINITY; }
    if let Some(h) = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")) {
        return u64::from_str_radix(h, 16).map(|v| v as f64).unwrap_or(f64::NAN);
    }
    if let Some(h) = t.strip_prefix("0b").or_else(|| t.strip_prefix("0B")) {
        return u64::from_str_radix(h, 2).map(|v| v as f64).unwrap_or(f64::NAN);
    }
    t.parse::<f64>().unwrap_or(f64::NAN)
}
