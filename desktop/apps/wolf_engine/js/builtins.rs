use alloc::string::{String, ToString};
use alloc::vec::Vec;

use super::interp::*;
use super::value::*;

type NF = NativeFn;
type R = Result<Value, Control>;

fn arg(args: &[Value], i: usize) -> Value {
    args.get(i).cloned().unwrap_or(Value::Undefined)
}

fn sarg(args: &[Value], i: usize) -> String {
    arg(args, i).to_display()
}

pub fn setup(interp: &mut Interp) {
    let g = interp.globals.clone();

    let console = new_object(ObjKind::Plain);
    for n in ["log", "info", "warn", "error", "debug"] {
        console.set_prop(n, interp.make_native(n, console_log));
    }
    g.declare("console", console);

    let math = new_object(ObjKind::Plain);
    math.set_prop("PI", Value::Number(core::f64::consts::PI));
    math.set_prop("E", Value::Number(core::f64::consts::E));
    math.set_prop("LN2", Value::Number(core::f64::consts::LN_2));
    math.set_prop("LN10", Value::Number(core::f64::consts::LN_10));
    math.set_prop("SQRT2", Value::Number(core::f64::consts::SQRT_2));
    math.set_prop("MAX_SAFE_INTEGER", Value::Number(9007199254740991.0));
    for n in ["abs", "floor", "ceil", "round", "trunc", "sign", "sqrt", "cbrt", "exp", "log", "log2", "log10", "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "max", "min", "pow", "hypot", "random"] {
        let f: NF = match n {
            "abs" => math_abs, "floor" => math_floor, "ceil" => math_ceil,
            "round" => math_round, "trunc" => math_trunc, "sign" => math_sign,
            "sqrt" => math_sqrt, "cbrt" => math_cbrt, "exp" => math_exp,
            "log" => math_log, "log2" => math_log2, "log10" => math_log10,
            "sin" => math_sin, "cos" => math_cos, "tan" => math_tan,
            "asin" => math_asin, "acos" => math_acos, "atan" => math_atan,
            "atan2" => math_atan2, "max" => math_max, "min" => math_min,
            "pow" => math_pow, "hypot" => math_hypot, "random" => math_random,
            _ => math_abs,
        };
        math.set_prop(n, interp.make_native(n, f));
    }
    g.declare("Math", math);

    let json = new_object(ObjKind::Plain);
    json.set_prop("parse", interp.make_native("parse", json_parse));
    json.set_prop("stringify", interp.make_native("stringify", json_stringify));
    g.declare("JSON", json);

    g.declare("parseInt", interp.make_native("parseInt", g_parse_int));
    g.declare("parseFloat", interp.make_native("parseFloat", g_parse_float));
    g.declare("isNaN", interp.make_native("isNaN", g_is_nan));
    g.declare("isFinite", interp.make_native("isFinite", g_is_finite));
    g.declare("String", interp.make_native("String", ctor_string));
    g.declare("Number", interp.make_native("Number", ctor_number));
    g.declare("Boolean", interp.make_native("Boolean", ctor_boolean));
    g.declare("Array", interp.make_native("Array", ctor_array));
    g.declare("Object", interp.make_native("Object", ctor_object));
    g.declare("print", interp.make_native("print", console_log));
    g.declare("alert", interp.make_native("alert", console_log));

    let object_ctor = g.get("Object").unwrap_or(Value::Undefined);
    object_ctor.set_prop("keys", interp.make_native("keys", obj_keys));
    object_ctor.set_prop("values", interp.make_native("values", obj_values));
    object_ctor.set_prop("entries", interp.make_native("entries", obj_entries));
    object_ctor.set_prop("assign", interp.make_native("assign", obj_assign));
    object_ctor.set_prop("create", interp.make_native("create", obj_create));
    object_ctor.set_prop("freeze", interp.make_native("freeze", obj_identity));

    let array_ctor = g.get("Array").unwrap_or(Value::Undefined);
    array_ctor.set_prop("isArray", interp.make_native("isArray", arr_is_array));
    array_ctor.set_prop("from", interp.make_native("from", arr_from));

    let string_ctor = g.get("String").unwrap_or(Value::Undefined);
    string_ctor.set_prop("fromCharCode", interp.make_native("fromCharCode", str_from_char_code));

    let number_ctor = g.get("Number").unwrap_or(Value::Undefined);
    number_ctor.set_prop("isInteger", interp.make_native("isInteger", num_is_integer));
    number_ctor.set_prop("isNaN", interp.make_native("isNaN", num_is_nan));
    number_ctor.set_prop("parseInt", interp.make_native("parseInt", g_parse_int));
    number_ctor.set_prop("parseFloat", interp.make_native("parseFloat", g_parse_float));
    number_ctor.set_prop("MAX_SAFE_INTEGER", Value::Number(9007199254740991.0));
    number_ctor.set_prop("MIN_SAFE_INTEGER", Value::Number(-9007199254740991.0));

    g.declare("NaN", Value::Number(f64::NAN));
    g.declare("Infinity", Value::Number(f64::INFINITY));

    g.declare("setTimeout", interp.make_native("setTimeout", g_set_timeout));
    g.declare("setInterval", interp.make_native("setInterval", g_set_interval));
    g.declare("setImmediate", interp.make_native("setImmediate", g_set_timeout));
    g.declare("queueMicrotask", interp.make_native("queueMicrotask", g_set_timeout));
    g.declare("requestAnimationFrame", interp.make_native("requestAnimationFrame", g_set_timeout));
    g.declare("clearTimeout", interp.make_native("clearTimeout", g_clear_timer));
    g.declare("clearInterval", interp.make_native("clearInterval", g_clear_timer));
    g.declare("clearImmediate", interp.make_native("clearImmediate", g_clear_timer));
    g.declare("cancelAnimationFrame", interp.make_native("cancelAnimationFrame", g_clear_timer));
}

static TIMER_ID: core::sync::atomic::AtomicI32 = core::sync::atomic::AtomicI32::new(1);

fn next_timer_id() -> Value {
    Value::Number(TIMER_ID.fetch_add(1, core::sync::atomic::Ordering::Relaxed) as f64)
}

fn g_set_timeout(i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let f = arg(a, 0);
    if matches!(f, Value::Function(_)) {
        let _ = i.call(&f, Value::Undefined, &[]);
    }
    Ok(next_timer_id())
}

fn g_set_interval(i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let f = arg(a, 0);
    if matches!(f, Value::Function(_)) {
        let _ = i.call(&f, Value::Undefined, &[]);
    }
    Ok(next_timer_id())
}

fn g_clear_timer(_i: &mut Interp, _t: &Value, _a: &[Value]) -> R {
    Ok(Value::Undefined)
}

pub fn get_method(interp: &mut Interp, obj: &Value, name: &str) -> Option<Value> {
    let f: NF = match obj {
        Value::Str(_) => str_method(name)?,
        Value::Object(o) => {
            let is_arr = matches!(&o.borrow().kind, ObjKind::Array(_));
            if is_arr { arr_method(name).or_else(|| plain_method(name))? } else { plain_method(name)? }
        }
        Value::Number(_) => num_method(name)?,
        Value::Bool(_) => bool_method(name)?,
        Value::Function(_) => fn_method(name)?,
        _ => return None,
    };
    Some(interp.make_native(name, f))
}

fn plain_method(name: &str) -> Option<NF> {
    Some(match name {
        "hasOwnProperty" => obj_has_own,
        "toString" => val_to_string,
        "valueOf" => val_value_of,
        _ => return None,
    })
}

fn fn_method(name: &str) -> Option<NF> {
    Some(match name {
        "call" => fn_call,
        "apply" => fn_apply,
        "bind" => fn_bind,
        "toString" => fn_to_string,
        _ => return None,
    })
}

fn str_method(name: &str) -> Option<NF> {
    Some(match name {
        "charAt" => s_char_at,
        "charCodeAt" | "codePointAt" => s_char_code_at,
        "indexOf" => s_index_of,
        "lastIndexOf" => s_last_index_of,
        "includes" => s_includes,
        "startsWith" => s_starts_with,
        "endsWith" => s_ends_with,
        "slice" => s_slice,
        "substring" => s_substring,
        "substr" => s_substr,
        "toUpperCase" => s_upper,
        "toLowerCase" => s_lower,
        "trim" => s_trim,
        "trimStart" => s_trim_start,
        "trimEnd" => s_trim_end,
        "split" => s_split,
        "replace" => s_replace,
        "replaceAll" => s_replace_all,
        "repeat" => s_repeat,
        "concat" => s_concat,
        "padStart" => s_pad_start,
        "padEnd" => s_pad_end,
        "at" => s_at,
        "toString" => val_to_string,
        "valueOf" => val_value_of,
        _ => return None,
    })
}

fn num_method(name: &str) -> Option<NF> {
    Some(match name {
        "toString" => n_to_string,
        "toFixed" => n_to_fixed,
        "toPrecision" => n_to_string,
        "valueOf" => val_value_of,
        _ => return None,
    })
}

fn bool_method(name: &str) -> Option<NF> {
    Some(match name {
        "toString" => val_to_string,
        "valueOf" => val_value_of,
        _ => return None,
    })
}

fn arr_method(name: &str) -> Option<NF> {
    Some(match name {
        "push" => a_push,
        "pop" => a_pop,
        "shift" => a_shift,
        "unshift" => a_unshift,
        "slice" => a_slice,
        "splice" => a_splice,
        "join" => a_join,
        "indexOf" => a_index_of,
        "lastIndexOf" => a_last_index_of,
        "includes" => a_includes,
        "map" => a_map,
        "filter" => a_filter,
        "forEach" => a_for_each,
        "reduce" => a_reduce,
        "find" => a_find,
        "findIndex" => a_find_index,
        "some" => a_some,
        "every" => a_every,
        "sort" => a_sort,
        "reverse" => a_reverse,
        "concat" => a_concat,
        "flat" => a_flat,
        "at" => a_at,
        "toString" => val_to_string,
        _ => return None,
    })
}

fn console_log(interp: &mut Interp, _this: &Value, args: &[Value]) -> R {
    let mut out = String::new();
    for (i, a) in args.iter().enumerate() {
        if i > 0 { out.push(' '); }
        out.push_str(&a.to_display());
    }
    out.push('\n');
    if let Some(log) = interp.log {
        log(out.as_ptr(), out.len() as u32);
    }
    Ok(Value::Undefined)
}

fn ctor_string(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::string(arg(a, 0).to_display())) }
fn ctor_number(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    if a.is_empty() { return Ok(Value::Number(0.0)); }
    Ok(Value::Number(arg(a, 0).to_number()))
}
fn ctor_boolean(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Bool(arg(a, 0).is_truthy())) }
fn ctor_array(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(new_array(a.to_vec())) }
fn ctor_object(_i: &mut Interp, _t: &Value, _a: &[Value]) -> R { Ok(new_object(ObjKind::Plain)) }

fn g_parse_int(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let s = arg(a, 0).to_display();
    let radix0 = if a.len() > 1 { arg(a, 1).to_number() as u32 } else { 0 };
    let s = s.trim_start();
    let (neg, body) = if let Some(r) = s.strip_prefix('-') { (true, r) } else if let Some(r) = s.strip_prefix('+') { (false, r) } else { (false, s) };
    let (radix, body) = if body.starts_with("0x") || body.starts_with("0X") { (16u32, &body[2..]) } else if radix0 == 0 { (10u32, body) } else { (radix0, body) };
    let mut n: f64 = 0.0;
    let mut any = false;
    for c in body.chars() {
        let d = match c.to_digit(radix) { Some(d) => d, None => break };
        n = n * radix as f64 + d as f64;
        any = true;
    }
    if !any { return Ok(Value::Number(f64::NAN)); }
    Ok(Value::Number(if neg { -n } else { n }))
}

fn g_parse_float(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let s = arg(a, 0).to_display();
    let t = s.trim_start();
    let b = t.as_bytes();
    let mut end = 0;
    let mut seen_dot = false;
    let mut seen_e = false;
    while end < b.len() {
        let c = b[end];
        if c == b'+' || c == b'-' {
            if end != 0 && !(b[end - 1] == b'e' || b[end - 1] == b'E') { break; }
        } else if c.is_ascii_digit() {
        } else if c == b'.' && !seen_dot && !seen_e { seen_dot = true; }
        else if (c == b'e' || c == b'E') && !seen_e { seen_e = true; }
        else { break; }
        end += 1;
    }
    Ok(Value::Number(t[..end].parse::<f64>().unwrap_or(f64::NAN)))
}

fn g_is_nan(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Bool(arg(a, 0).to_number().is_nan())) }
fn g_is_finite(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Bool(arg(a, 0).to_number().is_finite())) }

fn str_from_char_code(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let mut s = String::new();
    for v in a {
        if let Some(ch) = char::from_u32(v.to_number() as u32) { s.push(ch); }
    }
    Ok(Value::string(s))
}

/* ================= Math ================= */

fn floorf(x: f64) -> f64 {
    if x.is_nan() || x.is_infinite() || x == 0.0 { return x; }
    let t = x as i64 as f64;
    if t > x { t - 1.0 } else { t }
}
fn ceilf(x: f64) -> f64 {
    if x.is_nan() || x.is_infinite() || x == 0.0 { return x; }
    let t = x as i64 as f64;
    if t < x { t + 1.0 } else { t }
}
fn truncf(x: f64) -> f64 {
    if x.is_nan() || x.is_infinite() || x == 0.0 { return x; }
    x as i64 as f64
}
fn roundf(x: f64) -> f64 {
    if x.is_nan() || x.is_infinite() || x == 0.0 { return x; }
    let f = floorf(x);
    if x - f >= 0.5 { f + 1.0 } else { f }
}

fn math_abs(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(arg(a, 0).to_number().abs())) }
fn math_floor(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(floorf(arg(a, 0).to_number()))) }
fn math_ceil(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(ceilf(arg(a, 0).to_number()))) }
fn math_round(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(roundf(arg(a, 0).to_number()))) }
fn math_trunc(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(truncf(arg(a, 0).to_number()))) }
fn math_sign(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let n = arg(a, 0).to_number();
    Ok(Value::Number(if n.is_nan() { f64::NAN } else if n > 0.0 { 1.0 } else if n < 0.0 { -1.0 } else { 0.0 }))
}
fn math_sqrt(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(sqrtf(arg(a, 0).to_number()))) }
fn math_cbrt(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let n = arg(a, 0).to_number();
    Ok(Value::Number(if n < 0.0 { -expf(lnf(-n) / 3.0) } else { expf(lnf(n) / 3.0) }))
}
fn math_exp(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(expf(arg(a, 0).to_number()))) }
fn math_log(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(lnf(arg(a, 0).to_number()))) }
fn math_log2(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(lnf(arg(a, 0).to_number()) / core::f64::consts::LN_2)) }
fn math_log10(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(lnf(arg(a, 0).to_number()) / core::f64::consts::LN_10)) }
fn math_sin(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(sinf(arg(a, 0).to_number()))) }
fn math_cos(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(cosf(arg(a, 0).to_number()))) }
fn math_tan(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { let x = arg(a, 0).to_number(); Ok(Value::Number(sinf(x) / cosf(x))) }
fn math_asin(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { let x = arg(a, 0).to_number(); Ok(Value::Number(atan2f(x, sqrtf((1.0 - x * x).max(0.0))))) }
fn math_acos(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { let x = arg(a, 0).to_number(); Ok(Value::Number(core::f64::consts::FRAC_PI_2 - atan2f(x, sqrtf((1.0 - x * x).max(0.0))))) }
fn math_atan(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(atan2f(arg(a, 0).to_number(), 1.0))) }
fn math_atan2(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(atan2f(arg(a, 0).to_number(), arg(a, 1).to_number()))) }
fn math_max(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let mut m = f64::NEG_INFINITY;
    for v in a { let n = v.to_number(); if n.is_nan() { return Ok(Value::Number(f64::NAN)); } if n > m { m = n; } }
    Ok(Value::Number(m))
}
fn math_min(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let mut m = f64::INFINITY;
    for v in a { let n = v.to_number(); if n.is_nan() { return Ok(Value::Number(f64::NAN)); } if n < m { m = n; } }
    Ok(Value::Number(m))
}
fn math_pow(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Number(powf(arg(a, 0).to_number(), arg(a, 1).to_number()))) }
fn math_hypot(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let mut s = 0.0;
    for v in a { let n = v.to_number(); s += n * n; }
    Ok(Value::Number(sqrtf(s)))
}
fn math_random(i: &mut Interp, _t: &Value, _a: &[Value]) -> R { Ok(Value::Number(i.random())) }

pub fn sqrtf(x: f64) -> f64 {
    if x < 0.0 { return f64::NAN; }
    if x == 0.0 { return 0.0; }
    let bits = x.to_bits();
    let mut e = ((bits >> 52) & 0x7ff) as i32 - 1023;
    let mut m = f64::from_bits((bits & 0x000f_ffff_ffff_ffff) | 0x3ff0_0000_0000_0000);
    if e & 1 != 0 { m *= 2.0; e -= 1; }
    let mut q = m;
    for _ in 0..40 { q = 0.5 * (q + m / q); }
    scale_pow2(q, e / 2)
}

fn sinf(x: f64) -> f64 {
    let x = reduce_angle(x);
    let mut term = x;
    let mut sum = x;
    for n in 1..14 {
        term *= -x * x / (((2 * n) * (2 * n + 1)) as f64);
        sum += term;
    }
    sum
}
fn cosf(x: f64) -> f64 {
    let x = reduce_angle(x);
    let mut term = 1.0;
    let mut sum = 1.0;
    for n in 1..14 {
        term *= -x * x / (((2 * n - 1) * (2 * n)) as f64);
        sum += term;
    }
    sum
}
fn reduce_angle(mut x: f64) -> f64 {
    let twopi = 2.0 * core::f64::consts::PI;
    x = x % twopi;
    if x > core::f64::consts::PI { x -= twopi; }
    else if x < -core::f64::consts::PI { x += twopi; }
    x
}
fn atan2f(y: f64, x: f64) -> f64 {
    if x > 0.0 { return atanf(y / x); }
    if x < 0.0 { return if y >= 0.0 { atanf(y / x) + core::f64::consts::PI } else { atanf(y / x) - core::f64::consts::PI }; }
    if y > 0.0 { core::f64::consts::FRAC_PI_2 }
    else if y < 0.0 { -core::f64::consts::FRAC_PI_2 }
    else { 0.0 }
}
fn atanf(x: f64) -> f64 {
    if x.abs() > 1.0 { let s = if x > 0.0 { core::f64::consts::FRAC_PI_2 } else { -core::f64::consts::FRAC_PI_2 }; return s - atanf(1.0 / x); }
    let mut term = x;
    let mut sum = x;
    let x2 = x * x;
    for n in 1..40 {
        term *= -x2;
        sum += term / ((2 * n + 1) as f64);
    }
    sum
}

/* ================= Object statikleri ================= */

fn own_keys(v: &Value) -> Vec<String> {
    match v {
        Value::Object(o) => {
            let b = o.borrow();
            match &b.kind {
                ObjKind::Array(a) => (0..a.len()).map(|i| alloc::format!("{}", i)).collect(),
                _ => b.props.iter().map(|p| p.0.clone()).collect(),
            }
        }
        Value::Function(f) => f.props.borrow().iter().map(|p| p.0.clone()).collect(),
        _ => Vec::new(),
    }
}

fn obj_keys(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let out = own_keys(&arg(a, 0));
    Ok(new_array(out.into_iter().map(Value::string).collect()))
}
fn obj_values(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let v = arg(a, 0);
    let mut out = Vec::new();
    if let Value::Object(o) = &v {
        let b = o.borrow();
        if let ObjKind::Array(arr) = &b.kind { out.extend(arr.iter().cloned()); }
        else { out.extend(b.props.iter().map(|p| p.1.clone())); }
    }
    Ok(new_array(out))
}
fn obj_entries(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let v = arg(a, 0);
    let mut out = Vec::new();
    if let Value::Object(o) = &v {
        let b = o.borrow();
        if let ObjKind::Array(arr) = &b.kind {
            for (i, x) in arr.iter().enumerate() {
                out.push(new_array(alloc::vec![Value::string(alloc::format!("{}", i)), x.clone()]));
            }
        } else {
            for p in b.props.iter() {
                out.push(new_array(alloc::vec![Value::string(p.0.clone()), p.1.clone()]));
            }
        }
    }
    Ok(new_array(out))
}
fn obj_assign(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let target = arg(a, 0);
    for src in a.iter().skip(1) {
        if let Value::Object(o) = src {
            let props: Vec<(String, Value)> = o.borrow().props.clone();
            for (k, v) in props { target.set_prop(&k, v); }
        }
    }
    Ok(target)
}
fn obj_create(_i: &mut Interp, _t: &Value, _a: &[Value]) -> R { Ok(new_object(ObjKind::Plain)) }
fn obj_identity(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(arg(a, 0)) }
fn obj_has_own(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let k = sarg(a, 0);
    Ok(Value::Bool(own_keys(this).iter().any(|x| *x == k)))
}

/* ================= Array statikleri ================= */

fn arr_is_array(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let v = arg(a, 0);
    Ok(Value::Bool(match &v { Value::Object(o) => matches!(&o.borrow().kind, ObjKind::Array(_)), _ => false }))
}
fn arr_from(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let v = arg(a, 0);
    if let Value::Object(o) = &v {
        if let ObjKind::Array(arr) = &o.borrow().kind { return Ok(new_array(arr.clone())); }
    }
    if let Value::Str(s) = &v {
        return Ok(new_array(s.chars().map(|c| Value::string(c.to_string())).collect()));
    }
    Ok(new_array(Vec::new()))
}

/* ================= ortak metotlar ================= */

fn val_to_string(_i: &mut Interp, this: &Value, _a: &[Value]) -> R { Ok(Value::string(this.to_display())) }
fn val_value_of(_i: &mut Interp, this: &Value, _a: &[Value]) -> R { Ok(this.clone()) }

fn n_to_string(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let n = this.to_number();
    let radix = if a.is_empty() { 10 } else { arg(a, 0).to_number() as u32 };
    if radix == 10 { return Ok(Value::string(num_to_string(n))); }
    if radix < 2 || radix > 36 { return Err(Control::Error(String::from("invalid radix"))); }
    let neg = n < 0.0;
    let mut v = truncf(n.abs()) as u64;
    if v == 0 { return Ok(Value::str("0")); }
    let digits = b"0123456789abcdefghijklmnopqrstuvwxyz";
    let mut buf = Vec::new();
    while v > 0 { buf.push(digits[(v % radix as u64) as usize]); v /= radix as u64; }
    if neg { buf.push(b'-'); }
    buf.reverse();
    Ok(Value::string(String::from_utf8(buf).unwrap_or_default()))
}
fn n_to_fixed(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let n = this.to_number();
    let d = arg(a, 0).to_number() as usize;
    Ok(Value::string(fixed_str(n, d)))
}
pub fn fixed_str(n: f64, d: usize) -> String {
    if !n.is_finite() { return num_to_string(n); }
    let neg = n < 0.0;
    let p = powf(10f64, d as i32 as f64);
    let r = roundf(n.abs() * p) / p;
    let mut s = if d == 0 { alloc::format!("{}", r as i64) } else { alloc::format!("{:.1$}", r, d) };
    if neg && r > 0.0 { s.insert(0, '-'); }
    s
}

/* ================= Function metotları ================= */

fn fn_call(i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let t = arg(a, 0);
    let rest = if a.len() > 1 { &a[1..] } else { &[][..] };
    i.call(this, t, rest)
}
fn fn_apply(i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let t = arg(a, 0);
    let mut rest = Vec::new();
    if let Some(Value::Object(o)) = a.get(1) {
        if let ObjKind::Array(arr) = &o.borrow().kind { rest = arr.clone(); }
    }
    i.call(this, t, &rest)
}
fn fn_bind(_i: &mut Interp, this: &Value, _a: &[Value]) -> R { Ok(this.clone()) }
fn fn_to_string(_i: &mut Interp, this: &Value, _a: &[Value]) -> R { Ok(Value::string(this.to_display())) }

/* ================= String metotları ================= */

fn s_char_at(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let s = this.to_display();
    let i = arg(a, 0).to_number() as i64;
    if i < 0 { return Ok(Value::str("")); }
    Ok(Value::string(s.chars().nth(i as usize).map(|c| c.to_string()).unwrap_or_default()))
}
fn s_char_code_at(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let s = this.to_display();
    let i = arg(a, 0).to_number() as i64;
    if i < 0 { return Ok(Value::Number(f64::NAN)); }
    Ok(Value::Number(s.chars().nth(i as usize).map(|c| c as u32 as f64).unwrap_or(f64::NAN)))
}
fn s_index_of(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let s = this.to_display();
    let needle = sarg(a, 0);
    let from = if a.len() > 1 { arg(a, 1).to_number() as usize } else { 0 };
    if from > s.len() { return Ok(Value::Number(-1.0)); }
    match s[from..].find(&needle) { Some(p) => Ok(Value::Number((p + from) as f64)), None => Ok(Value::Number(-1.0)) }
}
fn s_last_index_of(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let s = this.to_display();
    let needle = sarg(a, 0);
    match s.rfind(&needle) { Some(p) => Ok(Value::Number(p as f64)), None => Ok(Value::Number(-1.0)) }
}
fn s_includes(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    Ok(Value::Bool(this.to_display().contains(&sarg(a, 0))))
}
fn s_starts_with(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    Ok(Value::Bool(this.to_display().starts_with(&sarg(a, 0))))
}
fn s_ends_with(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    Ok(Value::Bool(this.to_display().ends_with(&sarg(a, 0))))
}
fn clamp_index(s: &str, v: f64) -> usize {
    let l = s.chars().count();
    if v < 0.0 { let n = l as i64 + v as i64; if n < 0 { 0 } else { n as usize } }
    else if v as usize > l { l } else { v as usize }
}
fn byte_of_char(s: &str, ci: usize) -> usize { s.char_indices().nth(ci).map(|(b, _)| b).unwrap_or(s.len()) }
fn s_slice(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let s = this.to_display();
    let start = clamp_index(&s, arg(a, 0).to_number());
    let end = if a.len() > 1 && !matches!(arg(a, 1), Value::Undefined) { clamp_index(&s, arg(a, 1).to_number()) } else { s.chars().count() };
    if start >= end { return Ok(Value::str("")); }
    Ok(Value::string(s[byte_of_char(&s, start)..byte_of_char(&s, end)].to_string()))
}
fn s_substring(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let s = this.to_display();
    let l = s.chars().count();
    let mut start = arg(a, 0).to_number() as i64;
    let mut end = if a.len() > 1 { arg(a, 1).to_number() as i64 } else { l as i64 };
    start = start.clamp(0, l as i64);
    end = end.clamp(0, l as i64);
    if start > end { core::mem::swap(&mut start, &mut end); }
    Ok(Value::string(s[byte_of_char(&s, start as usize)..byte_of_char(&s, end as usize)].to_string()))
}
fn s_substr(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let s = this.to_display();
    let l = s.chars().count();
    let mut start = arg(a, 0).to_number() as i64;
    if start < 0 { start = (l as i64 + start).max(0); }
    if start > l as i64 { return Ok(Value::str("")); }
    let len = if a.len() > 1 { arg(a, 1).to_number() as i64 } else { l as i64 - start };
    let end = (start + len).min(l as i64).max(start);
    Ok(Value::string(s[byte_of_char(&s, start as usize)..byte_of_char(&s, end as usize)].to_string()))
}
fn s_upper(_i: &mut Interp, this: &Value, _a: &[Value]) -> R {
    let mut o = String::new();
    for c in this.to_display().chars() { o.extend(c.to_uppercase()); }
    Ok(Value::string(o))
}
fn s_lower(_i: &mut Interp, this: &Value, _a: &[Value]) -> R {
    let mut o = String::new();
    for c in this.to_display().chars() { o.extend(c.to_lowercase()); }
    Ok(Value::string(o))
}
fn s_trim(_i: &mut Interp, this: &Value, _a: &[Value]) -> R { Ok(Value::string(this.to_display().trim().to_string())) }
fn s_trim_start(_i: &mut Interp, this: &Value, _a: &[Value]) -> R { Ok(Value::string(this.to_display().trim_start().to_string())) }
fn s_trim_end(_i: &mut Interp, this: &Value, _a: &[Value]) -> R { Ok(Value::string(this.to_display().trim_end().to_string())) }
fn s_split(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let s = this.to_display();
    if a.is_empty() || matches!(arg(a, 0), Value::Undefined) {
        return Ok(new_array(alloc::vec![Value::string(s)]));
    }
    let sep = sarg(a, 0);
    let mut parts: Vec<Value> = Vec::new();
    if sep.is_empty() {
        for c in s.chars() { parts.push(Value::string(c.to_string())); }
    } else {
        for p in s.split(sep.as_str()) { parts.push(Value::string(p.to_string())); }
    }
    let limit = if a.len() > 1 { arg(a, 1).to_number() as usize } else { 0 };
    if limit > 0 && parts.len() > limit { parts.truncate(limit); }
    Ok(new_array(parts))
}
fn s_replace(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let s = this.to_display();
    let from = sarg(a, 0);
    let to = sarg(a, 1);
    match s.find(&from) {
        Some(p) => Ok(Value::string(alloc::format!("{}{}{}", &s[..p], to, &s[p + from.len()..]))),
        None => Ok(Value::string(s)),
    }
}
fn s_replace_all(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let s = this.to_display();
    let from = sarg(a, 0);
    let to = sarg(a, 1);
    if from.is_empty() { return Ok(Value::string(s)); }
    Ok(Value::string(s.replace(from.as_str(), to.as_str())))
}
fn s_repeat(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let s = this.to_display();
    let n = arg(a, 0).to_number().max(0.0) as usize;
    let mut o = String::new();
    for _ in 0..n { o.push_str(&s); }
    Ok(Value::string(o))
}
fn s_concat(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let mut s = this.to_display();
    for v in a { s.push_str(&v.to_display()); }
    Ok(Value::string(s))
}
fn s_pad_start(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    Ok(Value::string(pad(this.to_display(), arg(a, 0).to_number() as usize, sarg_or(a, 1, " "), true)))
}
fn s_pad_end(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    Ok(Value::string(pad(this.to_display(), arg(a, 0).to_number() as usize, sarg_or(a, 1, " "), false)))
}
fn s_at(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let s = this.to_display();
    let l = s.chars().count();
    let mut i = arg(a, 0).to_number() as i64;
    if i < 0 { i += l as i64; }
    if i < 0 || i >= l as i64 { return Ok(Value::Undefined); }
    Ok(Value::string(s.chars().nth(i as usize).map(|c| c.to_string()).unwrap_or_default()))
}
fn sarg_or(a: &[Value], i: usize, d: &str) -> String {
    match a.get(i) {
        Some(Value::Undefined) | None => String::from(d),
        Some(v) => v.to_display(),
    }
}
fn pad(s: String, target: usize, pad: String, start: bool) -> String {
    let len = s.chars().count();
    if len >= target || pad.is_empty() { return s; }
    let mut p = String::new();
    while p.chars().count() < target - len { p.push_str(&pad); }
    p.truncate(byte_of_char(&p, target - len));
    if start { alloc::format!("{}{}", p, s) } else { alloc::format!("{}{}", s, p) }
}

/* ================= Array metotları ================= */

fn as_arr(this: &Value) -> alloc::rc::Rc<core::cell::RefCell<Obj>> {
    match this {
        Value::Object(o) => o.clone(),
        _ => alloc::rc::Rc::new(core::cell::RefCell::new(Obj { kind: ObjKind::Array(Vec::new()), props: Vec::new() })),
    }
}
fn arr_items(this: &Value) -> Vec<Value> {
    if let Value::Object(o) = this {
        if let ObjKind::Array(a) = &o.borrow().kind { return a.clone(); }
    }
    Vec::new()
}
fn a_push(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let o = as_arr(this);
    if let ObjKind::Array(arr) = &mut o.borrow_mut().kind {
        for v in a { arr.push(v.clone()); }
        return Ok(Value::Number(arr.len() as f64));
    }
    Ok(Value::Number(0.0))
}
fn a_pop(_i: &mut Interp, this: &Value, _a: &[Value]) -> R {
    let o = as_arr(this);
    if let ObjKind::Array(arr) = &mut o.borrow_mut().kind { return Ok(arr.pop().unwrap_or(Value::Undefined)); }
    Ok(Value::Undefined)
}
fn a_shift(_i: &mut Interp, this: &Value, _a: &[Value]) -> R {
    let o = as_arr(this);
    if let ObjKind::Array(arr) = &mut o.borrow_mut().kind { if arr.is_empty() { return Ok(Value::Undefined); } return Ok(arr.remove(0)); }
    Ok(Value::Undefined)
}
fn a_unshift(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let o = as_arr(this);
    if let ObjKind::Array(arr) = &mut o.borrow_mut().kind {
        for (i, v) in a.iter().enumerate() { arr.insert(i, v.clone()); }
        return Ok(Value::Number(arr.len() as f64));
    }
    Ok(Value::Number(0.0))
}
fn a_slice(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let l = arr.len() as i64;
    let mut start = arg(a, 0).to_number() as i64;
    if start < 0 { start += l; }
    start = start.clamp(0, l);
    let mut end = if a.len() > 1 && !matches!(arg(a, 1), Value::Undefined) { arg(a, 1).to_number() as i64 } else { l };
    if end < 0 { end += l; }
    end = end.clamp(0, l);
    if start >= end { return Ok(new_array(Vec::new())); }
    Ok(new_array(arr[start as usize..end as usize].to_vec()))
}
fn a_splice(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let o = as_arr(this);
    if let ObjKind::Array(arr) = &mut o.borrow_mut().kind {
        let l = arr.len() as i64;
        let mut start = arg(a, 0).to_number() as i64;
        if start < 0 { start += l; }
        start = start.clamp(0, l);
        let del = if a.len() > 1 { (arg(a, 1).to_number() as i64).max(0) } else { l - start };
        let del = del.min(l - start);
        let removed: Vec<Value> = arr.drain(start as usize..(start + del) as usize).collect();
        for (i, v) in a.iter().skip(2).enumerate() { arr.insert(start as usize + i, v.clone()); }
        return Ok(new_array(removed));
    }
    Ok(new_array(Vec::new()))
}
fn a_join(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let sep = if a.is_empty() { String::from(",") } else { arg(a, 0).to_display() };
    let mut out = String::new();
    for (i, v) in arr.iter().enumerate() {
        if i > 0 { out.push_str(&sep); }
        if !matches!(v, Value::Undefined | Value::Null) { out.push_str(&v.to_display()); }
    }
    Ok(Value::string(out))
}
fn a_index_of(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let target = arg(a, 0);
    let from = if a.len() > 1 { arg(a, 1).to_number() as usize } else { 0 };
    for i in from..arr.len() { if arr[i].strict_eq(&target) { return Ok(Value::Number(i as f64)); } }
    Ok(Value::Number(-1.0))
}
fn a_last_index_of(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let target = arg(a, 0);
    for i in (0..arr.len()).rev() { if arr[i].strict_eq(&target) { return Ok(Value::Number(i as f64)); } }
    Ok(Value::Number(-1.0))
}
fn a_includes(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let target = arg(a, 0);
    Ok(Value::Bool(arr.iter().any(|v| v.strict_eq(&target) || (v.to_number().is_nan() && target.to_number().is_nan()))))
}
fn a_map(i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let f = arg(a, 0);
    let mut out = Vec::new();
    for (idx, v) in arr.iter().enumerate() {
        let args = alloc::vec![v.clone(), Value::Number(idx as f64), this.clone()];
        out.push(i.call(&f, Value::Undefined, &args)?);
    }
    Ok(new_array(out))
}
fn a_filter(i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let f = arg(a, 0);
    let mut out = Vec::new();
    for (idx, v) in arr.iter().enumerate() {
        let args = alloc::vec![v.clone(), Value::Number(idx as f64), this.clone()];
        if i.call(&f, Value::Undefined, &args)?.is_truthy() { out.push(v.clone()); }
    }
    Ok(new_array(out))
}
fn a_for_each(i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let f = arg(a, 0);
    for (idx, v) in arr.iter().enumerate() {
        let args = alloc::vec![v.clone(), Value::Number(idx as f64), this.clone()];
        i.call(&f, Value::Undefined, &args)?;
    }
    Ok(Value::Undefined)
}
fn a_reduce(i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let f = arg(a, 0);
    let (mut acc, start) = if a.len() > 1 { (arg(a, 1), 0) } else if !arr.is_empty() { (arr[0].clone(), 1) } else {
        return Err(Control::Error(String::from("reduce of empty array with no initial value")));
    };
    for idx in start..arr.len() {
        let args = alloc::vec![acc, arr[idx].clone(), Value::Number(idx as f64), this.clone()];
        acc = i.call(&f, Value::Undefined, &args)?;
    }
    Ok(acc)
}
fn a_find(i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let f = arg(a, 0);
    for (idx, v) in arr.iter().enumerate() {
        let args = alloc::vec![v.clone(), Value::Number(idx as f64), this.clone()];
        if i.call(&f, Value::Undefined, &args)?.is_truthy() { return Ok(v.clone()); }
    }
    Ok(Value::Undefined)
}
fn a_find_index(i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let f = arg(a, 0);
    for (idx, v) in arr.iter().enumerate() {
        let args = alloc::vec![v.clone(), Value::Number(idx as f64), this.clone()];
        if i.call(&f, Value::Undefined, &args)?.is_truthy() { return Ok(Value::Number(idx as f64)); }
    }
    Ok(Value::Number(-1.0))
}
fn a_some(i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let f = arg(a, 0);
    for (idx, v) in arr.iter().enumerate() {
        let args = alloc::vec![v.clone(), Value::Number(idx as f64), this.clone()];
        if i.call(&f, Value::Undefined, &args)?.is_truthy() { return Ok(Value::Bool(true)); }
    }
    Ok(Value::Bool(false))
}
fn a_every(i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let f = arg(a, 0);
    for (idx, v) in arr.iter().enumerate() {
        let args = alloc::vec![v.clone(), Value::Number(idx as f64), this.clone()];
        if !i.call(&f, Value::Undefined, &args)?.is_truthy() { return Ok(Value::Bool(false)); }
    }
    Ok(Value::Bool(true))
}
fn a_reverse(_i: &mut Interp, this: &Value, _a: &[Value]) -> R {
    let o = as_arr(this);
    if let ObjKind::Array(arr) = &mut o.borrow_mut().kind { arr.reverse(); }
    Ok(this.clone())
}
fn a_sort(i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let mut arr = arr_items(this);
    let cmp = a.first().cloned().unwrap_or(Value::Undefined);
    if let Value::Function(_) = cmp {
        let n = arr.len();
        for x in 0..n {
            for y in 0..n - 1 - x {
                let args = alloc::vec![arr[y].clone(), arr[y + 1].clone()];
                if i.call(&cmp, Value::Undefined, &args)?.to_number() > 0.0 { arr.swap(y, y + 1); }
            }
        }
    } else {
        arr.sort_by(|x, y| x.to_display().cmp(&y.to_display()));
    }
    let o = as_arr(this);
    if let ObjKind::Array(dst) = &mut o.borrow_mut().kind { *dst = arr; }
    Ok(this.clone())
}
fn a_concat(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let mut out = arr_items(this);
    for v in a {
        if let Value::Object(o) = v {
            if let ObjKind::Array(arr) = &o.borrow().kind { out.extend(arr.iter().cloned()); continue; }
        }
        out.push(v.clone());
    }
    Ok(new_array(out))
}
fn a_flat(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let depth = if a.is_empty() { 1 } else { arg(a, 0).to_number() as i32 };
    let mut out = Vec::new();
    flatten(&arr, depth, &mut out);
    Ok(new_array(out))
}
fn flatten(arr: &[Value], depth: i32, out: &mut Vec<Value>) {
    for v in arr {
        if depth > 0 {
            if let Value::Object(o) = v {
                if let ObjKind::Array(inner) = &o.borrow().kind { flatten(inner, depth - 1, out); continue; }
            }
        }
        out.push(v.clone());
    }
}
fn a_at(_i: &mut Interp, this: &Value, a: &[Value]) -> R {
    let arr = arr_items(this);
    let l = arr.len() as i64;
    let mut idx = arg(a, 0).to_number() as i64;
    if idx < 0 { idx += l; }
    if idx < 0 || idx >= l { return Ok(Value::Undefined); }
    Ok(arr[idx as usize].clone())
}

/* ================= Number statikleri ================= */

fn num_is_integer(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let n = arg(a, 0).to_number();
    Ok(Value::Bool(n.is_finite() && n - truncf(n) == 0.0))
}
fn num_is_nan(_i: &mut Interp, _t: &Value, a: &[Value]) -> R { Ok(Value::Bool(arg(a, 0).to_number().is_nan())) }

/* ================= JSON ================= */

fn json_stringify(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let mut out = String::new();
    if json_write(&arg(a, 0), 0, &mut out) { Ok(Value::string(out)) } else { Ok(Value::Undefined) }
}
fn json_write(v: &Value, depth: usize, out: &mut String) -> bool {
    if depth > 64 { return false; }
    match v {
        Value::Null => { out.push_str("null"); true }
        Value::Bool(b) => { out.push_str(if *b { "true" } else { "false" }); true }
        Value::Number(n) => { out.push_str(&num_to_string(*n)); true }
        Value::Str(s) => { json_quote(s, out); true }
        Value::Undefined | Value::Function(_) => false,
        Value::Object(o) => {
            let b = o.borrow();
            match &b.kind {
                ObjKind::Array(arr) => {
                    out.push('[');
                    for (i, x) in arr.iter().enumerate() {
                        if i > 0 { out.push(','); }
                        if !json_write(x, depth + 1, out) { out.push_str("null"); }
                    }
                    out.push(']');
                }
                _ => {
                    out.push('{');
                    let mut first = true;
                    for p in b.props.iter() {
                        if matches!(p.1, Value::Undefined | Value::Function(_)) { continue; }
                        if !first { out.push(','); }
                        first = false;
                        json_quote(&p.0, out);
                        out.push(':');
                        json_write(&p.1, depth + 1, out);
                    }
                    out.push('}');
                }
            }
            true
        }
    }
}
fn json_quote(s: &str, out: &mut String) {
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&alloc::format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
}

struct JP<'a> { s: &'a [u8], p: usize }
fn json_parse(_i: &mut Interp, _t: &Value, a: &[Value]) -> R {
    let src = sarg(a, 0);
    let mut jp = JP { s: src.as_bytes(), p: 0 };
    jp.value()
}
impl<'a> JP<'a> {
    fn ws(&mut self) { while self.p < self.s.len() && (self.s[self.p] as char).is_whitespace() { self.p += 1; } }
    fn value(&mut self) -> R {
        self.ws();
        if self.p >= self.s.len() { return Err(Control::Error(String::from("unexpected end of JSON"))); }
        match self.s[self.p] {
            b'{' => self.object(),
            b'[' => self.array(),
            b'"' => self.string().map(Value::string),
            b't' => { self.lit("true")?; Ok(Value::Bool(true)) }
            b'f' => { self.lit("false")?; Ok(Value::Bool(false)) }
            b'n' => { self.lit("null")?; Ok(Value::Null) }
            _ => self.number(),
        }
    }
    fn lit(&mut self, w: &str) -> Result<(), Control> {
        if self.s[self.p..].starts_with(w.as_bytes()) { self.p += w.len(); Ok(()) }
        else { Err(Control::Error(String::from("invalid JSON literal"))) }
    }
    fn string(&mut self) -> Result<String, Control> {
        self.p += 1;
        let mut out = String::new();
        while self.p < self.s.len() {
            let c = self.s[self.p];
            self.p += 1;
            if c == b'"' { return Ok(out); }
            if c == b'\\' {
                if self.p >= self.s.len() { break; }
                let e = self.s[self.p]; self.p += 1;
                match e {
                    b'n' => out.push('\n'), b't' => out.push('\t'), b'r' => out.push('\r'),
                    b'b' => out.push('\u{8}'), b'f' => out.push('\u{c}'), b'/' => out.push('/'),
                    b'"' => out.push('"'), b'\\' => out.push('\\'),
                    b'u' => {
                        let mut v: u32 = 0;
                        for _ in 0..4 {
                            if self.p >= self.s.len() { break; }
                            let h = hexv(self.s[self.p]); self.p += 1;
                            if h < 0 { break; }
                            v = v * 16 + h as u32;
                        }
                        if let Some(ch) = char::from_u32(v) { out.push(ch); }
                    }
                    other => out.push(other as char),
                }
                continue;
            }
            let start = self.p - 1;
            let extra = if c >= 0xF0 { 3 } else if c >= 0xE0 { 2 } else if c >= 0x80 { 1 } else { 0 };
            self.p += extra;
            if let Ok(t) = core::str::from_utf8(&self.s[start..self.p]) { out.push_str(t); }
        }
        Err(Control::Error(String::from("unterminated JSON string")))
    }
    fn number(&mut self) -> R {
        let start = self.p;
        while self.p < self.s.len() {
            let c = self.s[self.p];
            if c.is_ascii_digit() || c == b'-' || c == b'+' || c == b'.' || c == b'e' || c == b'E' { self.p += 1; }
            else { break; }
        }
        let t = core::str::from_utf8(&self.s[start..self.p]).unwrap_or("");
        t.parse::<f64>().map(Value::Number).map_err(|_| Control::Error(String::from("invalid JSON number")))
    }
    fn array(&mut self) -> R {
        self.p += 1;
        let mut out = Vec::new();
        self.ws();
        if self.p < self.s.len() && self.s[self.p] == b']' { self.p += 1; return Ok(new_array(out)); }
        loop {
            out.push(self.value()?);
            self.ws();
            if self.p >= self.s.len() { break; }
            if self.s[self.p] == b',' { self.p += 1; continue; }
            if self.s[self.p] == b']' { self.p += 1; break; }
            return Err(Control::Error(String::from("invalid JSON array")));
        }
        Ok(new_array(out))
    }
    fn object(&mut self) -> R {
        self.p += 1;
        let obj = new_object(ObjKind::Plain);
        self.ws();
        if self.p < self.s.len() && self.s[self.p] == b'}' { self.p += 1; return Ok(obj); }
        loop {
            self.ws();
            if self.p >= self.s.len() || self.s[self.p] != b'"' { return Err(Control::Error(String::from("invalid JSON object"))); }
            let key = self.string()?;
            self.ws();
            if self.p >= self.s.len() || self.s[self.p] != b':' { return Err(Control::Error(String::from("invalid JSON object"))); }
            self.p += 1;
            let v = self.value()?;
            obj.set_prop(&key, v);
            self.ws();
            if self.p >= self.s.len() { break; }
            if self.s[self.p] == b',' { self.p += 1; continue; }
            if self.s[self.p] == b'}' { self.p += 1; break; }
            return Err(Control::Error(String::from("invalid JSON object")));
        }
        Ok(obj)
    }
}
fn hexv(c: u8) -> i32 {
    if c >= b'0' && c <= b'9' { (c - b'0') as i32 }
    else if c >= b'a' && c <= b'f' { (c - b'a' + 10) as i32 }
    else if c >= b'A' && c <= b'F' { (c - b'A' + 10) as i32 }
    else { -1 }
}
