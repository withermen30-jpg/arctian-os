extern crate alloc;
mod js;

use std::io::Read;
use std::io::Write;

extern "C" fn print(p: *const u8, n: u32) {
    let s = unsafe { std::slice::from_raw_parts(p, n as usize) };
    std::io::stdout().write_all(s).ok();
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let src = if args.len() > 2 && args[1] == "-e" {
        args[2].clone()
    } else if args.len() > 1 {
        match std::fs::read_to_string(&args[1]) {
            Ok(s) => s,
            Err(e) => { eprintln!("read error: {}", e); std::process::exit(1); }
        }
    } else {
        let mut s = String::new();
        std::io::stdin().read_to_string(&mut s).unwrap();
        s
    };
    let mut eng = js::Engine::new(Some(print));
    match eng.eval(&src) {
        Ok(v) => {
            use js::value::Value;
            if !matches!(v, Value::Undefined) {
                println!("=> {}", v.to_display());
            }
        }
        Err(e) => { eprintln!("{}", e); std::process::exit(1); }
    }
}
