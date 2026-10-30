pub mod ast;
pub mod builtins;
pub mod host;
pub mod interp;
pub mod lexer;
pub mod parser;
pub mod value;

use alloc::rc::Rc;
use alloc::string::String;

use interp::{Interp, LogFn};
use value::{Control, Env, Value};

pub struct Engine {
    pub interp: Interp,
}

impl Engine {
    pub fn new(log: Option<LogFn>) -> Engine {
        let globals = Env::new(None);
        let mut interp = Interp::new(globals, log);
        builtins::setup(&mut interp);
        host::setup_dom(&mut interp);
        Engine { interp }
    }

    pub fn reset(&mut self) {
        let log = self.interp.log;
        let host = core::mem::take(&mut self.interp.host);
        let globals = Env::new(None);
        let mut interp = Interp::new(globals, log);
        interp.host = host;
        builtins::setup(&mut interp);
        host::setup_dom(&mut interp);
        self.interp = interp;
    }

    pub fn eval(&mut self, src: &str) -> Result<Value, String> {
        self.interp.steps = 0;
        let stmts = parser::Parser::parse(src).map_err(|e| alloc::format!("SyntaxError: {}", e))?;
        let env = Rc::clone(&self.interp.globals);
        match self.interp.run(&stmts, &env) {
            Ok(v) => Ok(v),
            Err(Control::Throw(v)) => Err(alloc::format!("Uncaught {}", v.to_display())),
            Err(Control::Error(e)) => Err(alloc::format!("Error: {}", e)),
            Err(_) => Err(String::from("Uncaught control flow")),
        }
    }

    pub fn set_log(&mut self, log: Option<LogFn>) {
        self.interp.log = log;
    }
}
