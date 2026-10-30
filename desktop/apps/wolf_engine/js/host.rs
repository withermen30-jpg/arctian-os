use alloc::string::String;
use alloc::vec::Vec;

use super::interp::*;
use super::value::*;

pub type CbGet = extern "C" fn(*const u8, u32) -> i64;
pub type CbGetIdx = extern "C" fn(*const u8, u32, i32) -> i64;
pub type CbHandle = extern "C" fn(i64) -> i64;

#[repr(C)]
#[derive(Default)]
pub struct HostTable {
    pub get_by_id: Option<CbGet>,
    pub get_by_tag: Option<CbGetIdx>,
    pub get_by_class: Option<CbGetIdx>,
    pub query: Option<CbGet>,
    pub query_idx: Option<CbGetIdx>,
    pub create: Option<CbGet>,
    pub tag_of: Option<extern "C" fn(i64, *mut u8, u32) -> i32>,
    pub get_text: Option<extern "C" fn(i64, *mut u8, u32) -> i32>,
    pub set_text: Option<extern "C" fn(i64, *const u8, u32)>,
    pub get_attr: Option<extern "C" fn(i64, *const u8, u32, *mut u8, u32) -> i32>,
    pub set_attr: Option<extern "C" fn(i64, *const u8, u32, *const u8, u32) -> i32>,
    pub append: Option<extern "C" fn(i64, i64)>,
    pub remove: Option<extern "C" fn(i64)>,
    pub set_style: Option<extern "C" fn(i64, *const u8, u32, *const u8, u32)>,
    pub navigate: Option<extern "C" fn(*const u8, u32)>,
    pub body: Option<extern "C" fn() -> i64>,
    pub parent: Option<CbHandle>,
    pub child_count: Option<CbHandle>,
    pub child_at: Option<extern "C" fn(i64, i32) -> i64>,
}

fn arg(args: &[Value], i: usize) -> Value {
    args.get(i).cloned().unwrap_or(Value::Undefined)
}

fn as_bytes(s: &str) -> (*const u8, u32) {
    (s.as_ptr(), s.len() as u32)
}

fn handle_of(v: &Value) -> i64 {
    if let Value::Object(o) = v {
        if let ObjKind::Host { handle, .. } = &o.borrow().kind { return *handle; }
        if let ObjKind::Style { handle } = &o.borrow().kind { return *handle; }
        if let ObjKind::ClassList { handle } = &o.borrow().kind { return *handle; }
    }
    -1
}

fn make_element(handle: i64) -> Value {
    new_object(ObjKind::Host { handle, tag: String::new() })
}

pub fn setup_dom(interp: &mut Interp) {
    let doc = new_object(ObjKind::Plain);
    doc.set_prop("getElementById", interp.make_native("getElementById", d_get_by_id));
    doc.set_prop("getElementsByTagName", interp.make_native("getElementsByTagName", d_get_by_tag));
    doc.set_prop("getElementsByClassName", interp.make_native("getElementsByClassName", d_get_by_class));
    doc.set_prop("querySelector", interp.make_native("querySelector", d_query));
    doc.set_prop("querySelectorAll", interp.make_native("querySelectorAll", d_query_all));
    doc.set_prop("createElement", interp.make_native("createElement", d_create));
    doc.set_prop("addEventListener", interp.make_native("addEventListener", d_noop));
    doc.set_prop("readyState", Value::str("complete"));

    if let Some(body) = interp.host.body {
        let h = body();
        if h > 0 { doc.set_prop("body", make_element(h)); }
    }
    interp.globals.declare("document", doc);

    let win = new_object(ObjKind::Plain);
    let loc = new_object(ObjKind::Plain);
    loc.set_prop("href", Value::str(""));
    loc.set_prop("reload", interp.make_native("reload", d_noop));
    loc.set_prop("assign", interp.make_native("assign", d_navigate));
    loc.set_prop("replace", interp.make_native("replace", d_navigate));
    win.set_prop("location", loc.clone());
    win.set_prop("alert", interp.make_native("alert", d_alert));
    for t in ["setTimeout", "setInterval", "setImmediate", "queueMicrotask",
              "requestAnimationFrame", "clearTimeout", "clearInterval", "clearImmediate",
              "cancelAnimationFrame"] {
        win.set_prop(t, interp.globals.get(t).unwrap_or(Value::Undefined));
    }
    interp.globals.declare("window", win.clone());
    interp.globals.declare("self", win);
    interp.globals.declare("location", loc);
    interp.globals.declare("addEventListener", interp.make_native("addEventListener", d_noop));
}

pub fn host_get_prop(interp: &mut Interp, obj: &Value, key: &str) -> Option<Value> {
    let kind = match obj {
        Value::Object(o) => match &o.borrow().kind {
            ObjKind::Host { handle, .. } => (0i32, *handle),
            ObjKind::Style { handle } => (1, *handle),
            ObjKind::ClassList { handle } => (2, *handle),
            _ => return None,
        },
        _ => return None,
    };
    let (kind_id, handle) = kind;

    if kind_id == 1 {
        return match key {
            "setProperty" => Some(interp.make_native("setProperty", style_set)),
            "removeProperty" => Some(interp.make_native("removeProperty", d_noop)),
            "cssText" => get_attr(interp, handle, "style"),
            _ => None,
        };
    }
    if kind_id == 2 {
        return match key {
            "add" => Some(interp.make_native("add", cl_add)),
            "remove" => Some(interp.make_native("remove", cl_remove)),
            "toggle" => Some(interp.make_native("toggle", cl_toggle)),
            "contains" => Some(interp.make_native("contains", cl_contains)),
            _ => None,
        };
    }

    if let Some(n) = element_method(key) {
        return Some(interp.make_native(key, n));
    }
    match key {
        "id" => get_attr(interp, handle, "id"),
        "className" => get_attr(interp, handle, "class"),
        "tagName" | "nodeName" => {
            let mut buf = [0u8; 32];
            let n = if let Some(f) = interp.host.tag_of { f(handle, buf.as_mut_ptr(), buf.len() as u32) } else { 0 };
            if n > 0 {
                if let Ok(t) = core::str::from_utf8(&buf[..n as usize]) {
                    return Some(Value::str(&upper(t)));
                }
            }
            Some(Value::str(""))
        }
        "textContent" | "innerText" | "innerHTML" => get_text(interp, handle),
        "value" => get_attr(interp, handle, "value"),
        "href" => get_attr(interp, handle, "href"),
        "style" => Some(new_object(ObjKind::Style { handle })),
        "classList" => Some(new_object(ObjKind::ClassList { handle })),
        "parentNode" | "parentElement" => {
            let p = if let Some(f) = interp.host.parent { f(handle) } else { -1 };
            if p >= 0 { Some(make_element(p)) } else { Some(Value::Null) }
        }
        "firstChild" | "lastChild" => Some(Value::Null),
        "children" | "childNodes" => {
            let n = if let Some(f) = interp.host.child_count { f(handle) } else { -1 };
            let mut out = Vec::new();
            if n > 0 {
                for i in 0..n {
                    let c = if let Some(f) = interp.host.child_at { f(handle, i as i32) } else { -1 };
                    if c >= 0 { out.push(make_element(c)); }
                }
            }
            Some(new_array(out))
        }
        "offsetWidth" | "offsetHeight" | "clientWidth" | "clientHeight" => Some(Value::Number(0.0)),
        _ => None,
    }
}

pub fn host_set_prop(interp: &mut Interp, obj: &Value, key: &str, val: Value) -> bool {
    let kind = match obj {
        Value::Object(o) => match &o.borrow().kind {
            ObjKind::Host { handle, .. } => (0i32, *handle),
            ObjKind::Style { handle } => (1, *handle),
            _ => return false,
        },
        _ => return false,
    };
    let (kind_id, handle) = kind;
    let text = val.to_display();

    if kind_id == 1 {
        if let Some(f) = interp.host.set_style {
            let (p, pl) = as_bytes(key);
            let (v, vl) = as_bytes(&text);
            f(handle, p, pl, v, vl);
        }
        return true;
    }

    match key {
        "textContent" | "innerText" | "innerHTML" => {
            if let Some(f) = interp.host.set_text { f(handle, text.as_ptr(), text.len() as u32); }
            true
        }
        "id" => { set_attr(interp, handle, "id", &text); true }
        "className" => { set_attr(interp, handle, "class", &text); true }
        "value" => { set_attr(interp, handle, "value", &text); true }
        "href" => { set_attr(interp, handle, "href", &text); true }
        "style" => {
            if let Some(f) = interp.host.set_style {
                let (v, vl) = as_bytes(&text);
                f(handle, core::ptr::null(), 0, v, vl);
            }
            true
        }
        _ => false,
    }
}

fn get_text(interp: &mut Interp, handle: i64) -> Option<Value> {
    let f = interp.host.get_text?;
    let mut buf = [0u8; 4096];
    let n = f(handle, buf.as_mut_ptr(), buf.len() as u32);
    if n < 0 { return Some(Value::str("")); }
    let n = (n as usize).min(buf.len());
    Some(Value::string(String::from_utf8_lossy(&buf[..n]).into_owned()))
}

fn get_attr(interp: &mut Interp, handle: i64, name: &str) -> Option<Value> {
    let f = interp.host.get_attr?;
    let (p, pl) = as_bytes(name);
    let mut buf = [0u8; 512];
    let n = f(handle, p, pl, buf.as_mut_ptr(), buf.len() as u32);
    if n < 0 { return Some(Value::str("")); }
    let n = (n as usize).min(buf.len());
    Some(Value::string(String::from_utf8_lossy(&buf[..n]).into_owned()))
}

fn set_attr(interp: &mut Interp, handle: i64, name: &str, val: &str) {
    if let Some(f) = interp.host.set_attr {
        let (p, pl) = as_bytes(name);
        let (v, vl) = as_bytes(val);
        f(handle, p, pl, v, vl);
    }
}

fn element_method(name: &str) -> Option<NativeFn> {
    Some(match name {
        "getAttribute" => e_get_attr,
        "setAttribute" => e_set_attr,
        "hasAttribute" => e_has_attr,
        "removeAttribute" => e_remove_attr,
        "appendChild" => e_append_child,
        "removeChild" => e_remove_child,
        "remove" => e_remove,
        "addEventListener" => e_add_listener,
        "removeEventListener" => d_noop,
        "click" => e_click,
        "focus" => d_noop,
        "blur" => d_noop,
        "getElementsByTagName" => d_get_by_tag,
        "getElementsByClassName" => d_get_by_class,
        "querySelector" => d_query,
        "querySelectorAll" => d_query_all,
        "contains" => e_contains,
        "insertBefore" => e_insert_before,
        "replaceChildren" => d_noop,
        "normalize" => d_noop,
        "closest" => d_noop,
        _ => return None,
    })
}

/* document natives */

fn d_get_by_id(i: &mut Interp, _t: &Value, a: &[Value]) -> Result<Value, Control> {
    let id = arg(a, 0).to_display();
    let f = i.host.get_by_id.ok_or_else(|| Control::Error(String::from("no host")))?;
    let (p, pl) = as_bytes(&id);
    let h = f(p, pl);
    Ok(if h >= 0 { make_element(h) } else { Value::Null })
}
fn collect_idx(i: &mut Interp, f: Option<CbGetIdx>, key: &str) -> Value {
    let f = match f { Some(f) => f, None => return new_array(Vec::new()) };
    let (p, pl) = as_bytes(key);
    let mut out = Vec::new();
    let mut idx = 0;
    loop {
        let h = f(p, pl, idx);
        if h < 0 { break; }
        out.push(make_element(h));
        idx += 1;
        if idx > 5000 { break; }
    }
    new_array(out)
}
fn d_get_by_tag(i: &mut Interp, _t: &Value, a: &[Value]) -> Result<Value, Control> {
    let key = arg(a, 0).to_display();
    let f = i.host.get_by_tag;
    Ok(collect_idx(i, f, &key))
}
fn d_get_by_class(i: &mut Interp, _t: &Value, a: &[Value]) -> Result<Value, Control> {
    let key = arg(a, 0).to_display();
    let f = i.host.get_by_class;
    Ok(collect_idx(i, f, &key))
}
fn d_query(i: &mut Interp, _t: &Value, a: &[Value]) -> Result<Value, Control> {
    let sel = arg(a, 0).to_display();
    let f = i.host.query.ok_or_else(|| Control::Error(String::from("no host")))?;
    let (p, pl) = as_bytes(&sel);
    let h = f(p, pl);
    Ok(if h >= 0 { make_element(h) } else { Value::Null })
}
fn d_query_all(i: &mut Interp, _t: &Value, a: &[Value]) -> Result<Value, Control> {
    let sel = arg(a, 0).to_display();
    let f = i.host.query_idx;
    Ok(collect_idx(i, f, &sel))
}
fn d_create(i: &mut Interp, _t: &Value, a: &[Value]) -> Result<Value, Control> {
    let tag = arg(a, 0).to_display();
    let f = i.host.create.ok_or_else(|| Control::Error(String::from("no host")))?;
    let (p, pl) = as_bytes(&tag);
    let h = f(p, pl);
    Ok(if h >= 0 { make_element(h) } else { Value::Null })
}
fn d_navigate(i: &mut Interp, _t: &Value, a: &[Value]) -> Result<Value, Control> {
    let url = arg(a, 0).to_display();
    if let Some(f) = i.host.navigate {
        let (p, pl) = as_bytes(&url);
        f(p, pl);
    }
    Ok(Value::Undefined)
}
fn d_alert(i: &mut Interp, _t: &Value, a: &[Value]) -> Result<Value, Control> {
    let msg = arg(a, 0).to_display();
    if let Some(log) = i.log {
        let s = alloc::format!("{}\n", msg);
        log(s.as_ptr(), s.len() as u32);
    }
    Ok(Value::Undefined)
}
fn d_noop(_i: &mut Interp, _t: &Value, _a: &[Value]) -> Result<Value, Control> { Ok(Value::Undefined) }

/* element natives */

fn e_get_attr(i: &mut Interp, this: &Value, a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    let name = arg(a, 0).to_display();
    Ok(get_attr(i, h, &name).unwrap_or(Value::str("")))
}
fn e_set_attr(i: &mut Interp, this: &Value, a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    let name = arg(a, 0).to_display();
    let val = arg(a, 1).to_display();
    set_attr(i, h, &name, &val);
    Ok(Value::Undefined)
}
fn e_has_attr(i: &mut Interp, this: &Value, a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    let name = arg(a, 0).to_display();
    let v = get_attr(i, h, &name).map(|v| v.to_display()).unwrap_or_default();
    Ok(Value::Bool(!v.is_empty()))
}
fn e_remove_attr(i: &mut Interp, this: &Value, a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    let name = arg(a, 0).to_display();
    set_attr(i, h, &name, "");
    Ok(Value::Undefined)
}
fn e_append_child(i: &mut Interp, this: &Value, a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    let c = handle_of(&arg(a, 0));
    if let Some(f) = i.host.append { if h >= 0 && c >= 0 { f(h, c); } }
    Ok(arg(a, 0))
}
fn e_remove_child(i: &mut Interp, _this: &Value, a: &[Value]) -> Result<Value, Control> {
    let c = handle_of(&arg(a, 0));
    if let Some(f) = i.host.remove { if c >= 0 { f(c); } }
    Ok(arg(a, 0))
}
fn e_remove(i: &mut Interp, this: &Value, _a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    if let Some(f) = i.host.remove { if h >= 0 { f(h); } }
    Ok(Value::Undefined)
}
fn e_insert_before(i: &mut Interp, this: &Value, a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    let c = handle_of(&arg(a, 0));
    if let Some(f) = i.host.append { if h >= 0 && c >= 0 { f(h, c); } }
    Ok(arg(a, 0))
}
fn e_contains(_i: &mut Interp, _this: &Value, _a: &[Value]) -> Result<Value, Control> { Ok(Value::Bool(false)) }
fn e_add_listener(i: &mut Interp, this: &Value, a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    let ev = arg(a, 0).to_display();
    let f = arg(a, 1);
    if h >= 0 {
        if let Value::Function(_) = f {
            i.listeners.push((h, ev, f));
        }
    }
    Ok(Value::Undefined)
}
fn e_click(i: &mut Interp, this: &Value, _a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    dispatch(i, h, "click");
    Ok(Value::Undefined)
}

/* style / classList natives */

fn style_set(i: &mut Interp, this: &Value, a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    let prop = arg(a, 0).to_display();
    let val = arg(a, 1).to_display();
    if let Some(f) = i.host.set_style {
        let (p, pl) = as_bytes(&prop);
        let (v, vl) = as_bytes(&val);
        f(h, p, pl, v, vl);
    }
    Ok(Value::Undefined)
}
fn cl_add(i: &mut Interp, this: &Value, a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    let cls = arg(a, 0).to_display();
    let cur = get_attr(i, h, "class").map(|v| v.to_display()).unwrap_or_default();
    if !cur.split_whitespace().any(|c| c == cls) {
        let nv = if cur.is_empty() { cls } else { alloc::format!("{} {}", cur, cls) };
        set_attr(i, h, "class", &nv);
    }
    Ok(Value::Undefined)
}
fn cl_remove(i: &mut Interp, this: &Value, a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    let cls = arg(a, 0).to_display();
    let cur = get_attr(i, h, "class").map(|v| v.to_display()).unwrap_or_default();
    let nv: Vec<&str> = cur.split_whitespace().filter(|c| *c != cls).collect();
    set_attr(i, h, "class", &nv.join(" "));
    Ok(Value::Undefined)
}
fn cl_toggle(i: &mut Interp, this: &Value, a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    let cls = arg(a, 0).to_display();
    let cur = get_attr(i, h, "class").map(|v| v.to_display()).unwrap_or_default();
    let has = cur.split_whitespace().any(|c| c == cls);
    if has {
        let nv: Vec<&str> = cur.split_whitespace().filter(|c| *c != cls).collect();
        set_attr(i, h, "class", &nv.join(" "));
    } else {
        let nv = if cur.is_empty() { cls } else { alloc::format!("{} {}", cur, cls) };
        set_attr(i, h, "class", &nv);
    }
    Ok(Value::Bool(!has))
}
fn cl_contains(i: &mut Interp, this: &Value, a: &[Value]) -> Result<Value, Control> {
    let h = handle_of(this);
    let cls = arg(a, 0).to_display();
    let cur = get_attr(i, h, "class").map(|v| v.to_display()).unwrap_or_default();
    Ok(Value::Bool(cur.split_whitespace().any(|c| c == cls)))
}

pub fn dispatch(interp: &mut Interp, handle: i64, ev: &str) {
    let listeners: Vec<Value> = interp
        .listeners
        .iter()
        .filter(|(h, t, _)| *h == handle && t == ev)
        .map(|(_, _, f)| f.clone())
        .collect();
    for f in listeners {
        let event = new_object(ObjKind::Plain);
        event.set_prop("type", Value::str(ev));
        let args = alloc::vec![event];
        let _ = interp.call(&f, Value::Undefined, &args);
    }
}

fn upper(s: &str) -> String {
    let mut o = String::new();
    for c in s.chars() { o.extend(c.to_uppercase()); }
    o
}
