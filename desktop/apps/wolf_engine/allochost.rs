extern crate alloc;
mod js;

use core::alloc::{GlobalAlloc, Layout};
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::io::Write;

const CLASSES: usize = 18;
const MIN_BLOCK: usize = 16;
const HEADER: usize = 16;

static FREE_HEADS: [AtomicUsize; CLASSES] = [const { AtomicUsize::new(0) }; CLASSES];
static ARENA_BUMP: AtomicUsize = AtomicUsize::new(0);
static ARENA_END: AtomicUsize = AtomicUsize::new(0);
static ARENA_INIT: AtomicBool = AtomicBool::new(false);

static mut ARENA: [u8; 8 * 1024 * 1024] = [0; 8 * 1024 * 1024];

#[no_mangle]
pub extern "C" fn arctian_js_arena(out_size: *mut usize) -> *mut u8 {
    unsafe {
        if !out_size.is_null() { *out_size = ARENA.len(); }
        core::ptr::addr_of_mut!(ARENA) as *mut u8
    }
}

struct WolfAlloc;

fn ensure_arena() {
    if ARENA_INIT.swap(true, Ordering::SeqCst) { return; }
    let mut sz: usize = 0;
    let p = unsafe { arctian_js_arena(&mut sz as *mut usize) };
    if !p.is_null() && sz >= MIN_BLOCK {
        ARENA_BUMP.store(p as usize, Ordering::Relaxed);
        ARENA_END.store(p as usize + sz, Ordering::Relaxed);
    }
}

unsafe impl GlobalAlloc for WolfAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let size = if layout.size() == 0 { 1 } else { layout.size() };
        if layout.align() > 16 { return core::ptr::null_mut(); }
        let need = match size.checked_add(HEADER) { Some(n) => n, None => return core::ptr::null_mut() };
        let mut idx = 0usize;
        let mut bs = MIN_BLOCK;
        while bs < need { bs <<= 1; idx += 1; if idx >= CLASSES { return core::ptr::null_mut(); } }
        let head = &FREE_HEADS[idx];
        let mut p = head.load(Ordering::Relaxed);
        while p != 0 {
            let next = *(p as *const usize);
            if head.compare_exchange(p, next, Ordering::Relaxed, Ordering::Relaxed).is_ok() {
                *(p as *mut usize) = idx;
                return (p as *mut u8).add(HEADER);
            }
            p = head.load(Ordering::Relaxed);
        }
        ensure_arena();
        let start = ARENA_BUMP.load(Ordering::Relaxed);
        let end = ARENA_END.load(Ordering::Relaxed);
        if start == 0 || start + bs > end { return core::ptr::null_mut(); }
        ARENA_BUMP.store(start + bs, Ordering::Relaxed);
        *(start as *mut usize) = idx;
        (start as *mut u8).add(HEADER)
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        if ptr.is_null() { return; }
        let block = ptr.sub(HEADER);
        let idx = *(block as *const usize);
        if idx >= CLASSES { return; }
        let head = &FREE_HEADS[idx];
        let mut cur = head.load(Ordering::Relaxed);
        loop {
            *(block as *mut usize) = cur;
            match head.compare_exchange(cur, block as usize, Ordering::Relaxed, Ordering::Relaxed) {
                Ok(_) => break,
                Err(x) => cur = x,
            }
        }
    }
}

#[global_allocator]
static ALLOC: WolfAlloc = WolfAlloc;

extern "C" fn print(p: *const u8, n: u32) {
    let s = unsafe { std::slice::from_raw_parts(p, n as usize) };
    std::io::stdout().write_all(s).ok();
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let src = if args.len() > 2 && args[1] == "-e" { args[2].clone() } else { String::new() };
    let mut eng = js::Engine::new(Some(print));
    match eng.eval(&src) {
        Ok(v) => { use js::value::Value; if !matches!(v, Value::Undefined) { println!("=> {}", v.to_display()); } }
        Err(e) => { eprintln!("{}", e); std::process::exit(1); }
    }
}
