use alloc::rc::Rc;
use alloc::string::{String, ToString};
use alloc::vec::Vec;

use super::ast::*;
use super::host::HostTable;
use super::value::*;

pub type LogFn = extern "C" fn(*const u8, u32);

pub struct Interp {
    pub globals: Rc<Env>,
    pub log: Option<LogFn>,
    pub host: HostTable,
    pub steps: u64,
    pub max_steps: u64,
    pub depth: u32,
    pub max_depth: u32,
    pub listeners: Vec<(i64, String, Value)>,
    pub rng: u64,
}

impl Interp {
    pub fn new(globals: Rc<Env>, log: Option<LogFn>) -> Interp {
        Interp {
            globals,
            log,
            host: HostTable::default(),
            steps: 0,
            max_steps: 10_000_000,
            depth: 0,
            max_depth: 600,
            listeners: Vec::new(),
            rng: 0x9E3779B97F4A7C15,
        }
    }

    pub fn random(&mut self) -> f64 {
        let mut x = self.rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.rng = x;
        (x >> 11) as f64 / (1u64 << 53) as f64
    }

    pub fn step(&mut self) -> Result<(), Control> {
        self.steps = self.steps.wrapping_add(1);
        if self.max_steps != 0 && self.steps > self.max_steps {
            return Err(Control::Error(String::from("script execution limit exceeded")));
        }
        Ok(())
    }

    pub fn run(&mut self, stmts: &[Stmt], env: &Rc<Env>) -> Result<Value, Control> {
        self.hoist(stmts, env);
        let mut last = Value::Undefined;
        for s in stmts {
            if let Some(v) = self.exec(s, env)? {
                last = v;
            }
        }
        Ok(last)
    }

    fn hoist(&self, stmts: &[Stmt], env: &Rc<Env>) {
        for s in stmts {
            if let Stmt::Func(fd) = s {
                let f = self.make_func(fd, env);
                env.declare(fd.name.as_deref().unwrap_or(""), f);
            }
        }
    }

    fn make_func(&self, fd: &FuncDecl, env: &Rc<Env>) -> Value {
        let body = if let Some(e) = &fd.expr_body {
            alloc::vec![Stmt::Return(Some((**e).clone()))]
        } else {
            fd.body.clone()
        };
        Value::Function(Rc::new(Func {
            name: fd.name.clone().unwrap_or_default(),
            params: fd.params.clone(),
            body: Rc::new(body),
            env: env.clone(),
            native: None,
            is_arrow: fd.is_arrow,
            is_ctor: !fd.is_arrow,
            props: core::cell::RefCell::new(Vec::new()),
        }))
    }

    pub fn exec(&mut self, s: &Stmt, env: &Rc<Env>) -> Result<Option<Value>, Control> {
        self.step()?;
        match s {
            Stmt::Empty => Ok(None),
            Stmt::Expr(e) => Ok(Some(self.eval(e, env)?)),
            Stmt::Var(_, decls) => {
                for (name, init) in decls {
                    let v = match init { Some(e) => self.eval(e, env)?, None => Value::Undefined };
                    env.declare(name, v);
                }
                Ok(None)
            }
            Stmt::Func(fd) => {
                let f = self.make_func(fd, env);
                env.declare(fd.name.as_deref().unwrap_or(""), f);
                Ok(None)
            }
            Stmt::Return(e) => {
                let v = match e { Some(e) => self.eval(e, env)?, None => Value::Undefined };
                Err(Control::Return(v))
            }
            Stmt::If(c, t, f) => {
                if self.eval(c, env)?.is_truthy() {
                    self.exec(t, env)
                } else if let Some(f) = f {
                    self.exec(f, env)
                } else {
                    Ok(None)
                }
            }
            Stmt::While(c, body) => {
                let mut last = None;
                while self.eval(c, env)?.is_truthy() {
                    self.step()?;
                    match self.exec(body, env) {
                        Ok(v) => { if v.is_some() { last = v; } }
                        Err(Control::Break) => break,
                        Err(Control::Continue) => continue,
                        Err(e) => return Err(e),
                    }
                }
                Ok(last)
            }
            Stmt::DoWhile(body, c) => {
                loop {
                    self.step()?;
                    match self.exec(body, env) {
                        Ok(_) => {}
                        Err(Control::Break) => break,
                        Err(Control::Continue) => {}
                        Err(e) => return Err(e),
                    }
                    if !self.eval(c, env)?.is_truthy() { break; }
                }
                Ok(None)
            }
            Stmt::For(init, cond, step, body) => {
                let scope = Env::new(Some(env.clone()));
                if let Some(i) = init { self.exec(i, &scope)?; }
                loop {
                    if let Some(c) = cond {
                        if !self.eval(c, &scope)?.is_truthy() { break; }
                    }
                    self.step()?;
                    match self.exec(body, &scope) {
                        Ok(_) => {}
                        Err(Control::Break) => break,
                        Err(Control::Continue) => {}
                        Err(e) => return Err(e),
                    }
                    if let Some(st) = step { self.eval(st, &scope)?; }
                }
                Ok(None)
            }
            Stmt::ForIn(name, iter, body, is_of) => {
                let it = self.eval(iter, env)?;
                if *is_of {
                    let items = self.iter_values(&it)?;
                    for v in items {
                        let scope = Env::new(Some(env.clone()));
                        scope.declare(name, v);
                        match self.exec(body, &scope) {
                            Ok(_) => {}
                            Err(Control::Break) => break,
                            Err(Control::Continue) => continue,
                            Err(e) => return Err(e),
                        }
                    }
                } else {
                    let keys = self.iter_keys(&it);
                    for k in keys {
                        let scope = Env::new(Some(env.clone()));
                        scope.declare(name, Value::string(k));
                        match self.exec(body, &scope) {
                            Ok(_) => {}
                            Err(Control::Break) => break,
                            Err(Control::Continue) => continue,
                            Err(e) => return Err(e),
                        }
                    }
                }
                Ok(None)
            }
            Stmt::Block(stmts) => {
                let scope = Env::new(Some(env.clone()));
                self.hoist(stmts, &scope);
                let mut last = None;
                for st in stmts {
                    if let Some(v) = self.exec(st, &scope)? { last = Some(v); }
                }
                Ok(last)
            }
            Stmt::Break => Err(Control::Break),
            Stmt::Continue => Err(Control::Continue),
            Stmt::Throw(e) => {
                let v = self.eval(e, env)?;
                Err(Control::Throw(v))
            }
            Stmt::Try(body, catch, finally) => {
                let r = self.exec(body, env);
                let mut result = match r {
                    Ok(v) => Ok(v),
                    Err(Control::Throw(v)) => {
                        if let Some((name, cbody)) = catch {
                            let scope = Env::new(Some(env.clone()));
                            scope.declare(name, v);
                            self.exec(cbody, &scope)
                        } else {
                            Err(Control::Throw(v))
                        }
                    }
                    Err(e) => Err(e),
                };
                if let Some(f) = finally {
                    if let Err(e) = self.exec(f, env) {
                        return Err(e);
                    }
                }
                if let Err(Control::Throw(v)) = &result {
                    let _ = v;
                }
                if let Ok(None) = result { result = Ok(None); }
                result
            }
            Stmt::Switch(disc, cases) => {
                let d = self.eval(disc, env)?;
                let mut start = None;
                for (i, (test, _)) in cases.iter().enumerate() {
                    match test {
                        Some(t) => {
                            let tv = self.eval(t, env)?;
                            if tv.strict_eq(&d) { start = Some(i); break; }
                        }
                        None => {}
                    }
                }
                if start.is_none() {
                    for (i, (test, _)) in cases.iter().enumerate() {
                        if test.is_none() { start = Some(i); break; }
                    }
                }
                if let Some(mut i) = start {
                    let scope = Env::new(Some(env.clone()));
                    while i < cases.len() {
                        for st in &cases[i].1 {
                            match self.exec(st, &scope) {
                                Ok(_) => {}
                                Err(Control::Break) => return Ok(None),
                                Err(e) => return Err(e),
                            }
                        }
                        i += 1;
                    }
                }
                Ok(None)
            }
        }
    }

    fn iter_values(&mut self, it: &Value) -> Result<Vec<Value>, Control> {
        match it {
            Value::Object(o) => match &o.borrow().kind {
                ObjKind::Array(a) => Ok(a.clone()),
                _ => Ok(Vec::new()),
            },
            Value::Str(s) => Ok(s.chars().map(|c| Value::string(c.to_string())).collect()),
            _ => Ok(Vec::new()),
        }
    }

    fn iter_keys(&mut self, it: &Value) -> Vec<String> {
        match it {
            Value::Object(o) => {
                let b = o.borrow();
                match &b.kind {
                    ObjKind::Array(_) => Vec::new(),
                    _ => b.props.iter().map(|p| p.0.clone()).collect(),
                }
            }
            _ => Vec::new(),
        }
    }

    pub fn eval(&mut self, e: &Expr, env: &Rc<Env>) -> Result<Value, Control> {
        self.step()?;
        match e {
            Expr::Num(n) => Ok(Value::Number(*n)),
            Expr::Str(s) => Ok(Value::str(s)),
            Expr::Bool(b) => Ok(Value::Bool(*b)),
            Expr::Null => Ok(Value::Null),
            Expr::Undefined => Ok(Value::Undefined),
            Expr::This => Ok(env.get("this").unwrap_or(Value::Undefined)),
            Expr::Ident(name) => Ok(env.get(name).unwrap_or(Value::Undefined)),
            Expr::Seq(list) => {
                let mut last = Value::Undefined;
                for x in list { last = self.eval(x, env)?; }
                Ok(last)
            }
            Expr::Array(items) => {
                let mut out = Vec::new();
                for it in items {
                    match it {
                        Expr::Spread(inner) => {
                            let v = self.eval(inner, env)?;
                            if let Value::Object(o) = &v {
                                if let ObjKind::Array(a) = &o.borrow().kind {
                                    out.extend(a.iter().cloned());
                                    continue;
                                }
                            }
                            if let Value::Str(s) = &v {
                                for c in s.chars() { out.push(Value::string(c.to_string())); }
                            }
                        }
                        _ => out.push(self.eval(it, env)?),
                    }
                }
                Ok(new_array(out))
            }
            Expr::Object(props) => {
                let obj = new_object(ObjKind::Plain);
                for (k, v) in props {
                    let key = match k {
                        PropKey::Ident(s) => s.clone(),
                        PropKey::Computed(e) => self.eval(e, env)?.to_display(),
                    };
                    if key == "__spread" {
                        let val = self.eval(v, env)?;
                        if let Value::Object(src) = &val {
                            let items: Vec<(String, Value)> = src.borrow().props.clone();
                            for (pk, pv) in items { obj.set_prop(&pk, pv); }
                        }
                        continue;
                    }
                    let val = self.eval(v, env)?;
                    obj.set_prop(&key, val);
                }
                Ok(obj)
            }
            Expr::Func(fd) => Ok(self.make_func(fd, env)),
            Expr::Unary(op, inner) => self.eval_unary(op, inner, env),
            Expr::Update(op, target, prefix) => {
                let old = self.eval(target, env)?;
                let delta = if *op == UpdOp::Inc { 1.0 } else { -1.0 };
                let newv = Value::Number(old.to_number() + delta);
                self.assign_to(target, newv.clone(), env)?;
                Ok(if *prefix { newv } else { old })
            }
            Expr::Binary(op, l, r) => {
                let lv = self.eval(l, env)?;
                let rv = self.eval(r, env)?;
                self.binop(op, &lv, &rv)
            }
            Expr::Logical(op, l, r) => {
                let lv = self.eval(l, env)?;
                match op {
                    LogOp::And => { if lv.is_truthy() { self.eval(r, env) } else { Ok(lv) } }
                    LogOp::Or => { if lv.is_truthy() { Ok(lv) } else { self.eval(r, env) } }
                    LogOp::Nullish => match lv { Value::Null | Value::Undefined => self.eval(r, env), _ => Ok(lv) },
                }
            }
            Expr::Cond(c, t, f) => {
                if self.eval(c, env)?.is_truthy() { self.eval(t, env) } else { self.eval(f, env) }
            }
            Expr::Assign(op, target, rhs) => {
                let v = match op {
                    None => self.eval(rhs, env)?,
                    Some(b) => {
                        let cur = self.eval(target, env)?;
                        let rv = self.eval(rhs, env)?;
                        self.binop(b, &cur, &rv)?
                    }
                };
                self.assign_to(target, v.clone(), env)?;
                Ok(v)
            }
            Expr::Member(obj, key, _) => {
                let o = self.eval(obj, env)?;
                let k = match key {
                    PropKey::Ident(s) => s.clone(),
                    PropKey::Computed(e) => self.eval(e, env)?.to_display(),
                };
                self.get_member(&o, &k)
            }
            Expr::Call(callee, args) => {
                let (this, f) = match &**callee {
                    Expr::Member(obj, key, _) => {
                        let o = self.eval(obj, env)?;
                        let k = match key {
                            PropKey::Ident(s) => s.clone(),
                            PropKey::Computed(e) => self.eval(e, env)?.to_display(),
                        };
                        let f = self.get_member(&o, &k)?;
                        (o, f)
                    }
                    _ => (Value::Undefined, self.eval(callee, env)?),
                };
                let evaled = self.eval_args(args, env)?;
                self.call(&f, this, &evaled)
            }
            Expr::New(callee, args) => {
                let f = self.eval(callee, env)?;
                let evaled = self.eval_args(args, env)?;
                let this = new_object(ObjKind::Plain);
                let r = self.call(&f, this.clone(), &evaled)?;
                Ok(match r { Value::Object(_) | Value::Function(_) => r, _ => this })
            }
            Expr::Template(parts) => {
                let mut out = String::new();
                for p in parts {
                    match p {
                        TplPart::Lit(s) => out.push_str(s),
                        TplPart::Expr(e) => out.push_str(&self.eval(e, env)?.to_display()),
                    }
                }
                Ok(Value::string(out))
            }
            Expr::Spread(inner) => self.eval(inner, env),
        }
    }

    fn eval_args(&mut self, args: &[Expr], env: &Rc<Env>) -> Result<Vec<Value>, Control> {
        let mut out = Vec::new();
        for a in args {
            match a {
                Expr::Spread(inner) => {
                    let v = self.eval(inner, env)?;
                    if let Value::Object(o) = &v {
                        if let ObjKind::Array(arr) = &o.borrow().kind {
                            out.extend(arr.iter().cloned());
                            continue;
                        }
                    }
                }
                _ => out.push(self.eval(a, env)?),
            }
        }
        Ok(out)
    }

    fn eval_unary(&mut self, op: &UnOp, inner: &Expr, env: &Rc<Env>) -> Result<Value, Control> {
        if *op == UnOp::Typeof {
            if let Expr::Ident(name) = inner {
                return Ok(Value::str(match env.get(name) {
                    Some(v) => v.type_of(),
                    None => "undefined",
                }));
            }
            let v = self.eval(inner, env)?;
            return Ok(Value::str(v.type_of()));
        }
        if *op == UnOp::Delete {
            if let Expr::Member(obj, key, _) = inner {
                let o = self.eval(obj, env)?;
                let k = match key {
                    PropKey::Ident(s) => s.clone(),
                    PropKey::Computed(e) => self.eval(e, env)?.to_display(),
                };
                if let Value::Object(ob) = &o {
                    ob.borrow_mut().props.retain(|p| p.0 != k);
                }
                return Ok(Value::Bool(true));
            }
            let _ = self.eval(inner, env)?;
            return Ok(Value::Bool(true));
        }
        let v = self.eval(inner, env)?;
        Ok(match op {
            UnOp::Neg => Value::Number(-v.to_number()),
            UnOp::Pos => Value::Number(v.to_number()),
            UnOp::Not => Value::Bool(!v.is_truthy()),
            UnOp::BitNot => Value::Number(!(to_int32(&v)) as f64),
            UnOp::Void => Value::Undefined,
            UnOp::Typeof => Value::str(v.type_of()),
            UnOp::Delete => Value::Bool(false),
        })
    }

    fn assign_to(&mut self, target: &Expr, v: Value, env: &Rc<Env>) -> Result<(), Control> {
        match target {
            Expr::Ident(name) => {
                if !env.assign(name, v.clone()) { env.declare(name, v); }
                Ok(())
            }
            Expr::Member(obj, key, _) => {
                let o = self.eval(obj, env)?;
                let k = match key {
                    PropKey::Ident(s) => s.clone(),
                    PropKey::Computed(e) => self.eval(e, env)?.to_display(),
                };
                self.set_member(&o, &k, v)
            }
            _ => Err(Control::Error(String::from("invalid assignment target"))),
        }
    }

    pub fn get_member(&mut self, obj: &Value, key: &str) -> Result<Value, Control> {
        match obj {
            Value::Object(o) => {
                {
                    let b = o.borrow();
                    for p in b.props.iter() {
                        if p.0 == key { return Ok(p.1.clone()); }
                    }
                    if let ObjKind::Array(a) = &b.kind {
                        if key == "length" { return Ok(Value::Number(a.len() as f64)); }
                        if let Some(i) = index_key(key) {
                            return Ok(a.get(i).cloned().unwrap_or(Value::Undefined));
                        }
                    }
                }
                if let Some(v) = super::builtins::get_method(self, obj, key) { return Ok(v); }
                if let Some(v) = super::host::host_get_prop(self, obj, key) { return Ok(v); }
                Ok(Value::Undefined)
            }
            Value::Str(s) => {
                if key == "length" { return Ok(Value::Number(s.chars().count() as f64)); }
                if let Some(i) = index_key(key) {
                    return Ok(s.chars().nth(i).map(|c| Value::string(c.to_string())).unwrap_or(Value::Undefined));
                }
                if let Some(v) = super::builtins::get_method(self, obj, key) { return Ok(v); }
                Ok(Value::Undefined)
            }
            Value::Number(_) | Value::Bool(_) => {
                if let Some(v) = super::builtins::get_method(self, obj, key) { return Ok(v); }
                Ok(Value::Undefined)
            }
            Value::Function(f) => {
                for p in f.props.borrow().iter() {
                    if p.0 == key { return Ok(p.1.clone()); }
                }
                match key {
                    "name" => Ok(Value::str(&f.name)),
                    "length" => Ok(Value::Number(f.params.len() as f64)),
                    _ => super::builtins::get_method(self, obj, key).ok_or_else(|| Control::Error(String::from("not a function"))),
                }
            }
            _ => Err(Control::Error(alloc::format!("cannot read property '{}' of {}", key, obj.type_of()))),
        }
    }

    pub fn set_member(&mut self, obj: &Value, key: &str, v: Value) -> Result<(), Control> {
        match obj {
            Value::Object(o) => {
                if matches!(&o.borrow().kind, ObjKind::Array(_)) {
                    if let Some(i) = index_key(key) {
                        let mut b = o.borrow_mut();
                        if let ObjKind::Array(a) = &mut b.kind {
                            while a.len() <= i { a.push(Value::Undefined); }
                            a[i] = v;
                        }
                        return Ok(());
                    }
                    if key == "length" {
                        let n = v.to_number() as usize;
                        let mut b = o.borrow_mut();
                        if let ObjKind::Array(a) = &mut b.kind {
                            a.resize(n, Value::Undefined);
                        }
                        return Ok(());
                    }
                }
                if super::host::host_set_prop(self, obj, key, v.clone()) {
                    return Ok(());
                }
                let mut b = o.borrow_mut();
                for p in b.props.iter_mut() {
                    if p.0 == key { p.1 = v; return Ok(()); }
                }
                b.props.push((String::from(key), v));
                Ok(())
            }
            _ => Err(Control::Error(alloc::format!("cannot set property '{}'", key))),
        }
    }

    pub fn binop(&mut self, op: &BinOp, l: &Value, r: &Value) -> Result<Value, Control> {
        Ok(match op {
            BinOp::Add => {
                if matches!(l, Value::Str(_)) || matches!(r, Value::Str(_)) {
                    Value::string(alloc::format!("{}{}", l.to_display(), r.to_display()))
                } else {
                    Value::Number(l.to_number() + r.to_number())
                }
            }
            BinOp::Sub => Value::Number(l.to_number() - r.to_number()),
            BinOp::Mul => Value::Number(l.to_number() * r.to_number()),
            BinOp::Div => Value::Number(l.to_number() / r.to_number()),
            BinOp::Mod => Value::Number(l.to_number() % r.to_number()),
            BinOp::Pow => Value::Number(powf(l.to_number(), r.to_number())),
            BinOp::Eq => Value::Bool(l.loose_eq(r)),
            BinOp::Ne => Value::Bool(!l.loose_eq(r)),
            BinOp::StrictEq => Value::Bool(l.strict_eq(r)),
            BinOp::StrictNe => Value::Bool(!l.strict_eq(r)),
            BinOp::Lt => Value::Bool(cmp_vals(l, r).map(|o| o < 0).unwrap_or(false)),
            BinOp::Le => Value::Bool(cmp_vals(l, r).map(|o| o <= 0).unwrap_or(false)),
            BinOp::Gt => Value::Bool(cmp_vals(l, r).map(|o| o > 0).unwrap_or(false)),
            BinOp::Ge => Value::Bool(cmp_vals(l, r).map(|o| o >= 0).unwrap_or(false)),
            BinOp::BitAnd => Value::Number((to_int32(l) & to_int32(r)) as f64),
            BinOp::BitOr => Value::Number((to_int32(l) | to_int32(r)) as f64),
            BinOp::BitXor => Value::Number((to_int32(l) ^ to_int32(r)) as f64),
            BinOp::Shl => Value::Number(((to_int32(l) as i32) << (to_uint32(r) & 31)) as f64),
            BinOp::Shr => Value::Number(((to_int32(l) as i32) >> (to_uint32(r) & 31)) as f64),
            BinOp::Ushr => Value::Number(((to_uint32(l)) >> (to_uint32(r) & 31)) as f64),
            BinOp::InstanceOf => Value::Bool(false),
            BinOp::In => {
                let k = l.to_display();
                Value::Bool(match r {
                    Value::Object(o) => {
                        let b = o.borrow();
                        if b.props.iter().any(|p| p.0 == k) { true }
                        else if let ObjKind::Array(a) = &b.kind {
                            index_key(&k).map(|i| i < a.len()).unwrap_or(false)
                        } else { false }
                    }
                    _ => false,
                })
            }
        })
    }

    pub fn call(&mut self, f: &Value, this: Value, args: &[Value]) -> Result<Value, Control> {
        let func = match f {
            Value::Function(func) => func.clone(),
            _ => return Err(Control::Error(alloc::format!("{} is not a function", f.to_display()))),
        };
        if let Some(nat) = func.native {
            return nat(self, &this, args);
        }
        self.depth += 1;
        if self.depth > self.max_depth {
            self.depth -= 1;
            return Err(Control::Error(String::from("maximum call depth exceeded")));
        }
        let call_env = Env::new(Some(func.env.clone()));
        for (i, p) in func.params.iter().enumerate() {
            call_env.declare(p, args.get(i).cloned().unwrap_or(Value::Undefined));
        }
        call_env.declare("arguments", new_array(args.to_vec()));
        if !func.is_arrow {
            call_env.declare("this", this);
        }
        self.hoist(&func.body, &call_env);
        let mut result = Ok(Value::Undefined);
        for st in func.body.iter() {
            match self.exec(st, &call_env) {
                Ok(_) => {}
                Err(Control::Return(v)) => { result = Ok(v); break; }
                Err(e) => { result = Err(e); break; }
            }
        }
        self.depth -= 1;
        result
    }

    pub fn call_method(&mut self, f: &Value, this: &Value, args: &[Value]) -> Result<Value, Control> {
        self.call(f, this.clone(), args)
    }

    pub fn make_native(&self, name: &str, n: NativeFn) -> Value {
        Value::Function(Rc::new(Func {
            name: String::from(name),
            params: Vec::new(),
            body: Rc::new(Vec::new()),
            env: self.globals.clone(),
            native: Some(n),
            is_arrow: false,
            is_ctor: false,
            props: core::cell::RefCell::new(Vec::new()),
        }))
    }
}

pub fn index_key(k: &str) -> Option<usize> {
    if k.is_empty() { return None; }
    let mut n: usize = 0;
    for c in k.bytes() {
        if c < b'0' || c > b'9' { return None; }
        n = n * 10 + (c - b'0') as usize;
    }
    Some(n)
}

pub fn to_int32(v: &Value) -> i32 {
    let n = v.to_number();
    f64_to_i32(n)
}

pub fn to_uint32(v: &Value) -> u32 {
    to_int32(v) as u32
}

pub fn itrunc(x: f64) -> f64 {
    if x.is_nan() || x.is_infinite() || x == 0.0 { x } else { x as i64 as f64 }
}
pub fn iround(x: f64) -> f64 {
    if x.is_nan() || x.is_infinite() || x == 0.0 { return x; }
    let f = itrunc(x);
    if x - f >= 0.5 { f + 1.0 } else { f }
}

fn f64_to_i32(n: f64) -> i32 {
    if n.is_nan() || n.is_infinite() { return 0; }
    let m = itrunc(n);
    let m = m % 4294967296.0;
    let m = if m < 0.0 { m + 4294967296.0 } else { m };
    if m >= 2147483648.0 { (m - 4294967296.0) as i32 } else { m as i32 }
}

fn cmp_vals(l: &Value, r: &Value) -> Option<i32> {
    if matches!(l, Value::Str(_)) && matches!(r, Value::Str(_)) {
        let a = l.to_display();
        let b = r.to_display();
        return Some(match a.cmp(&b) { core::cmp::Ordering::Less => -1, core::cmp::Ordering::Equal => 0, core::cmp::Ordering::Greater => 1 });
    }
    let a = l.to_number();
    let b = r.to_number();
    if a.is_nan() || b.is_nan() { None }
    else { Some(if a < b { -1 } else if a > b { 1 } else { 0 }) }
}

pub fn powf(a: f64, b: f64) -> f64 {
    if b == 0.0 { return 1.0; }
    if a == 0.0 { return 0.0; }
    if b == (b as i64) as f64 && b.abs() < 1024.0 {
        let mut n = b as i64;
        let mut base = a;
        let mut neg = false;
        if n < 0 { neg = true; n = -n; }
        let mut result = 1.0f64;
        while n > 0 {
            if n & 1 == 1 { result *= base; }
            base *= base;
            n >>= 1;
        }
        return if neg { 1.0 / result } else { result };
    }
    expf(b * lnf(a))
}

pub fn lnf(x: f64) -> f64 {
    if x <= 0.0 { return if x == 0.0 { f64::NEG_INFINITY } else { f64::NAN }; }
    let bits = x.to_bits();
    let mut e = ((bits >> 52) & 0x7ff) as i64 - 1023;
    let mant = (bits & 0x000f_ffff_ffff_ffff) | 0x3ff0_0000_0000_0000;
    let mut m = f64::from_bits(mant);
    if m > 1.4142135623730951 { m /= 2.0; e += 1; }
    let z = (m - 1.0) / (m + 1.0);
    let z2 = z * z;
    let mut sum = 0.0;
    let terms = [1.0f64, 1.0/3.0, 1.0/5.0, 1.0/7.0, 1.0/9.0, 1.0/11.0, 1.0/13.0, 1.0/15.0, 1.0/17.0, 1.0/19.0, 1.0/21.0, 1.0/23.0];
    let mut p = z;
    for t in terms {
        sum += t * p;
        p *= z2;
    }
    2.0 * sum + (e as f64) * core::f64::consts::LN_2
}

pub fn expf(x: f64) -> f64 {
    if x.is_nan() { return x; }
    if x > 709.78 { return f64::INFINITY; }
    if x < -745.0 { return 0.0; }
    let k = iround(x / core::f64::consts::LN_2);
    let r = x - k * core::f64::consts::LN_2;
    let mut term = 1.0f64;
    let mut sum = 1.0f64;
    for i in 1..20 {
        term *= r / (i as f64);
        sum += term;
    }
    scale_pow2(sum, k as i32)
}

pub fn scale_pow2(mut x: f64, mut e: i32) -> f64 {
    while e > 0 {
        let step = if e > 1023 { 1023 } else { e };
        x *= f64::from_bits(((1023 + step) as u64) << 52);
        e -= step;
    }
    while e < 0 {
        let step = if e < -1023 { 1023 } else { -e };
        x *= f64::from_bits(((1023 - step) as u64) << 52);
        e += step;
    }
    x
}
