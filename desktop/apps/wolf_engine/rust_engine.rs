#![no_std]

extern crate alloc;

pub mod js;

use core::alloc::{GlobalAlloc, Layout};
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

const CLASSES: usize = 18;
const MIN_BLOCK: usize = 16;
const HEADER: usize = 16;
const ENGINE_STACK_SIZE: usize = 2 * 1024 * 1024;

static FREE_HEADS: [AtomicUsize; CLASSES] = [const { AtomicUsize::new(0) }; CLASSES];
static ARENA_BUMP: AtomicUsize = AtomicUsize::new(0);
static ARENA_END: AtomicUsize = AtomicUsize::new(0);
static ARENA_INIT: AtomicBool = AtomicBool::new(false);

static mut ENGINE_STACK_TOP: *mut u8 = core::ptr::null_mut();
static mut JOB_ENGINE: *mut js::Engine = core::ptr::null_mut();
static mut JOB_CODE: *const u8 = core::ptr::null();
static mut JOB_LEN: u32 = 0;
static mut JOB_OUT: *mut u8 = core::ptr::null_mut();
static mut JOB_OUT_CAP: u32 = 0;
static mut JOB_STATUS: i32 = 0;
static mut ON_ENGINE_STACK: bool = false;

extern "C" {
    fn arctian_js_arena(out_size: *mut usize) -> *mut u8;
}

core::arch::global_asm!(
    ".globl wolf_run_on_stack",
    "wolf_run_on_stack:",
    "push r12",
    "mov r12, rsp",
    "mov rsp, rdi",
    "and rsp, -16",
    "mov rdi, rdx",
    "call rsi",
    "mov rsp, r12",
    "pop r12",
    "ret",
);

extern "C" {
    fn wolf_run_on_stack(new_sp: *mut u8, f: extern "C" fn(*mut u8), arg: *mut u8);
}

struct WolfAlloc;

fn ensure_arena() {
    if ARENA_INIT.swap(true, Ordering::SeqCst) {
        return;
    }
    let mut sz: usize = 0;
    let p = unsafe { arctian_js_arena(&mut sz as *mut usize) };
    if !p.is_null() && sz >= MIN_BLOCK {
        let base = p as usize;
        let top = (base + sz) & !15usize;
        ARENA_BUMP.store(base, Ordering::Relaxed);
        if sz > ENGINE_STACK_SIZE + 65536 {
            unsafe { ENGINE_STACK_TOP = top as *mut u8; }
            ARENA_END.store(top - ENGINE_STACK_SIZE, Ordering::Relaxed);
        } else {
            ARENA_END.store(top, Ordering::Relaxed);
        }
    }
}

unsafe impl GlobalAlloc for WolfAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let size = if layout.size() == 0 { 1 } else { layout.size() };
        if layout.align() > 16 {
            return core::ptr::null_mut();
        }
        let need = match size.checked_add(HEADER) {
            Some(n) => n,
            None => return core::ptr::null_mut(),
        };
        let mut idx = 0usize;
        let mut bs = MIN_BLOCK;
        while bs < need {
            bs <<= 1;
            idx += 1;
            if idx >= CLASSES {
                return core::ptr::null_mut();
            }
        }
        let head = &FREE_HEADS[idx];
        let mut p = head.load(Ordering::Relaxed);
        while p != 0 {
            let next = *(p as *const usize);
            if head
                .compare_exchange(p, next, Ordering::Relaxed, Ordering::Relaxed)
                .is_ok()
            {
                *(p as *mut usize) = idx;
                return (p as *mut u8).add(HEADER);
            }
            p = head.load(Ordering::Relaxed);
        }
        ensure_arena();
        let start = ARENA_BUMP.load(Ordering::Relaxed);
        let end = ARENA_END.load(Ordering::Relaxed);
        if start == 0 || start + bs > end {
            return core::ptr::null_mut();
        }
        ARENA_BUMP.store(start + bs, Ordering::Relaxed);
        *(start as *mut usize) = idx;
        (start as *mut u8).add(HEADER)
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        if ptr.is_null() {
            return;
        }
        let block = ptr.sub(HEADER);
        let idx = *(block as *const usize);
        if idx >= CLASSES {
            return;
        }
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

#[cfg(target_os = "none")]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

type LogFn = js::interp::LogFn;

static mut PRINT_CB: Option<LogFn> = None;
static mut ENGINE: Option<js::Engine> = None;

extern "C" fn print_dispatch(p: *const u8, n: u32) {
    unsafe {
        if let Some(cb) = PRINT_CB {
            cb(p, n);
        }
    }
}
fn engine() -> &'static mut js::Engine {
    unsafe {
        let slot = &mut *core::ptr::addr_of_mut!(ENGINE);
        if slot.is_none() {
            *slot = Some(js::Engine::new(Some(print_dispatch)));
        }
        slot.as_mut().unwrap()
    }
}

fn write_out(out: *mut u8, cap: u32, s: &str) {
    if out.is_null() || cap == 0 {
        return;
    }
    let bytes = s.as_bytes();
    let n = bytes.len().min((cap as usize) - 1);
    unsafe {
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), out, n);
        *out.add(n) = 0;
    }
}

#[no_mangle]
pub extern "C" fn fmod(a: f64, b: f64) -> f64 {
    if b == 0.0 || a.is_nan() || b.is_nan() || a.is_infinite() {
        return f64::NAN;
    }
    if b.is_infinite() {
        return a;
    }
    let q = js::interp::itrunc(a / b);
    a - q * b
}

#[no_mangle]
pub extern "C" fn wolf_engine_version() -> u32 {
    0x0001_0000
}

#[no_mangle]
pub extern "C" fn wolf_engine_set_print(cb: Option<LogFn>) {
    unsafe {
        PRINT_CB = cb;
    }
}

#[no_mangle]
pub extern "C" fn wolf_engine_set_host(host: *const js::host::HostTable) {
    unsafe {
        if !host.is_null() {
            engine().interp.host = core::ptr::read(host);
        }
    }
}

#[no_mangle]
pub extern "C" fn wolf_engine_set_limits(steps: u32, depth: u32) {
    let e = engine();
    if steps != 0 {
        e.interp.max_steps = steps as u64;
    }
    if depth != 0 {
        e.interp.max_depth = depth;
    }
}

extern "C" fn eval_trampoline(_arg: *mut u8) {
    unsafe {
        let e = &mut *JOB_ENGINE;
        let sl = core::slice::from_raw_parts(JOB_CODE, JOB_LEN as usize);
        JOB_STATUS = match core::str::from_utf8(sl) {
            Ok(code) => match e.eval(code) {
                Ok(v) => {
                    if !matches!(v, js::value::Value::Undefined) {
                        write_out(JOB_OUT, JOB_OUT_CAP, &v.to_display());
                    } else if !JOB_OUT.is_null() && JOB_OUT_CAP > 0 {
                        *JOB_OUT = 0;
                    }
                    0
                }
                Err(msg) => {
                    write_out(JOB_OUT, JOB_OUT_CAP, &msg);
                    -1
                }
            },
            Err(_) => {
                write_out(JOB_OUT, JOB_OUT_CAP, "gecersiz UTF-8");
                -2
            }
        };
    }
}

#[no_mangle]
pub extern "C" fn wolf_engine_eval(
    src: *const u8,
    len: u32,
    out: *mut u8,
    out_cap: u32,
) -> i32 {
    if src.is_null() {
        return -1;
    }
    unsafe {
        JOB_ENGINE = engine();
        JOB_CODE = src;
        JOB_LEN = len;
        JOB_OUT = out;
        JOB_OUT_CAP = out_cap;
        JOB_STATUS = 0;
        let top = ENGINE_STACK_TOP;
        if ON_ENGINE_STACK || top.is_null() {
            eval_trampoline(core::ptr::null_mut());
        } else {
            ON_ENGINE_STACK = true;
            wolf_run_on_stack(top, eval_trampoline, core::ptr::null_mut());
            ON_ENGINE_STACK = false;
        }
        JOB_STATUS
    }
}

#[no_mangle]
pub extern "C" fn wolf_engine_reset() {
    engine().reset();
}

#[no_mangle]
pub extern "C" fn wolf_engine_dispatch_event(handle: i64, ev: *const u8, ev_len: u32) {
    if ev.is_null() {
        return;
    }
    let s = unsafe { core::slice::from_raw_parts(ev, ev_len as usize) };
    let name = core::str::from_utf8(s).unwrap_or("");
    let e = engine();
    js::host::dispatch(&mut e.interp, handle, name);
}

#[no_mangle]
pub extern "C" fn wolf_engine_listener_count() -> u32 {
    engine().interp.listeners.len() as u32
}

#[no_mangle]
pub extern "C" fn app_entry(_win: *mut core::ffi::c_void, _asi: *mut core::ffi::c_void) {}
