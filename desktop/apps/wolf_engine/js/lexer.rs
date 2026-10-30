use alloc::string::String;
use alloc::vec::Vec;

#[derive(Clone, Debug, PartialEq)]
pub enum Tok {
    Num(f64),
    Str(String),
    Template(Vec<Tpl>),
    Ident(String),
    Var,
    Let,
    Const,
    Function,
    Return,
    If,
    Else,
    While,
    Do,
    For,
    Break,
    Continue,
    True,
    False,
    Null,
    Undefined,
    New,
    This,
    Typeof,
    Instanceof,
    In,
    Of,
    Delete,
    Void,
    Try,
    Catch,
    Finally,
    Throw,
    Switch,
    Case,
    Default,
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Semi,
    Comma,
    Dot,
    Ellipsis,
    Question,
    Colon,
    Arrow,
    Assign,
    PlusEq,
    MinusEq,
    StarEq,
    SlashEq,
    PercentEq,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    StarStar,
    EqEq,
    NotEq,
    EqEqEq,
    NotEqEq,
    Lt,
    Le,
    Gt,
    Ge,
    AndAnd,
    OrOr,
    Nullish,
    Not,
    Tilde,
    Amp,
    Pipe,
    Caret,
    Shl,
    Shr,
    Ushr,
    PlusPlus,
    MinusMinus,
    Eof,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Tpl {
    Lit(String),
    Expr(String),
}

pub struct Lexer<'a> {
    s: &'a [u8],
    p: usize,
}

fn is_ident_start(c: u8) -> bool {
    (c >= b'a' && c <= b'z') || (c >= b'A' && c <= b'Z') || c == b'_' || c == b'$' || c >= 0x80
}

fn is_ident_part(c: u8) -> bool {
    is_ident_start(c) || (c >= b'0' && c <= b'9')
}

impl<'a> Lexer<'a> {
    pub fn new(s: &'a str) -> Lexer<'a> {
        Lexer { s: s.as_bytes(), p: 0 }
    }

    fn peek(&self) -> u8 {
        if self.p < self.s.len() { self.s[self.p] } else { 0 }
    }

    fn peek_at(&self, o: usize) -> u8 {
        if self.p + o < self.s.len() { self.s[self.p + o] } else { 0 }
    }

    fn bump(&mut self) -> u8 {
        let c = self.peek();
        if self.p < self.s.len() { self.p += 1; }
        c
    }

    fn skip_ws(&mut self) {
        loop {
            let c = self.peek();
            if c == b' ' || c == b'\t' || c == b'\r' || c == b'\n' || c == 0x0b || c == 0x0c {
                self.p += 1;
            } else if c == b'/' && self.peek_at(1) == b'/' {
                self.p += 2;
                while self.p < self.s.len() && self.s[self.p] != b'\n' { self.p += 1; }
            } else if c == b'/' && self.peek_at(1) == b'*' {
                self.p += 2;
                while self.p + 1 < self.s.len() && !(self.s[self.p] == b'*' && self.s[self.p + 1] == b'/') {
                    self.p += 1;
                }
                self.p += 2;
            } else {
                break;
            }
        }
    }

    fn read_str(&mut self, quote: u8) -> Result<String, String> {
        let mut out = String::new();
        loop {
            if self.p >= self.s.len() { return Err("unterminated string".into()); }
            let c = self.bump();
            if c == quote { break; }
            if c == b'\\' {
                let e = self.bump();
                match e {
                    b'n' => out.push('\n'),
                    b't' => out.push('\t'),
                    b'r' => out.push('\r'),
                    b'0' => out.push('\0'),
                    b'b' => out.push('\u{8}'),
                    b'f' => out.push('\u{c}'),
                    b'v' => out.push('\u{b}'),
                    b'\\' => out.push('\\'),
                    b'\'' => out.push('\''),
                    b'"' => out.push('"'),
                    b'`' => out.push('`'),
                    b'\n' => {}
                    b'x' => {
                        let mut v: u32 = 0;
                        for _ in 0..2 {
                            let h = hex(self.bump());
                            if h < 0 { return Err("bad hex escape".into()); }
                            v = v * 16 + h as u32;
                        }
                        out.push(char::from_u32(v).unwrap_or('?'));
                    }
                    b'u' => {
                        if self.peek() == b'{' {
                            self.bump();
                            let mut v: u32 = 0;
                            while self.peek() != b'}' {
                                let h = hex(self.bump());
                                if h < 0 { return Err("bad unicode escape".into()); }
                                v = v * 16 + h as u32;
                            }
                            self.bump();
                            out.push(char::from_u32(v).unwrap_or('?'));
                        } else {
                            let mut v: u32 = 0;
                            for _ in 0..4 {
                                let h = hex(self.bump());
                                if h < 0 { return Err("bad unicode escape".into()); }
                                v = v * 16 + h as u32;
                            }
                            out.push(char::from_u32(v).unwrap_or('?'));
                        }
                    }
                    other => out.push(other as char),
                }
                continue;
            }
            push_byte_utf8(&mut out, c, self.s, &mut self.p);
        }
        Ok(out)
    }

    fn read_template(&mut self) -> Result<Vec<Tpl>, String> {
        let mut parts: Vec<Tpl> = Vec::new();
        let mut lit = String::new();
        loop {
            if self.p >= self.s.len() { return Err("unterminated template".into()); }
            let c = self.bump();
            if c == b'`' { break; }
            if c == b'\\' {
                let e = self.bump();
                match e {
                    b'n' => lit.push('\n'),
                    b't' => lit.push('\t'),
                    b'r' => lit.push('\r'),
                    b'\\' => lit.push('\\'),
                    b'`' => lit.push('`'),
                    b'$' => lit.push('$'),
                    other => lit.push(other as char),
                }
                continue;
            }
            if c == b'$' && self.peek() == b'{' {
                self.bump();
                parts.push(Tpl::Lit(core::mem::take(&mut lit)));
                let start = self.p;
                let mut depth = 1i32;
                while self.p < self.s.len() && depth > 0 {
                    let d = self.s[self.p];
                    if d == b'{' { depth += 1; }
                    else if d == b'}' { depth -= 1; if depth == 0 { break; } }
                    else if d == b'\'' || d == b'"' || d == b'`' {
                        let q = d; self.p += 1;
                        while self.p < self.s.len() && self.s[self.p] != q {
                            if self.s[self.p] == b'\\' { self.p += 1; }
                            self.p += 1;
                        }
                    }
                    self.p += 1;
                }
                let expr = core::str::from_utf8(&self.s[start..self.p]).unwrap_or("").into();
                if self.p < self.s.len() { self.p += 1; }
                parts.push(Tpl::Expr(expr));
                continue;
            }
            push_byte_utf8(&mut lit, c, self.s, &mut self.p);
        }
        if !lit.is_empty() || parts.is_empty() {
            parts.push(Tpl::Lit(lit));
        }
        Ok(parts)
    }

    pub fn tokenize(mut self) -> Result<Vec<Tok>, String> {
        let mut out: Vec<Tok> = Vec::new();
        loop {
            self.skip_ws();
            if self.p >= self.s.len() { out.push(Tok::Eof); break; }
            let c = self.peek();
            if c >= b'0' && c <= b'9' {
                out.push(Tok::Num(self.read_number()?));
                continue;
            }
            if c == b'.' && self.peek_at(1) >= b'0' && self.peek_at(1) <= b'9' {
                out.push(Tok::Num(self.read_number()?));
                continue;
            }
            if is_ident_start(c) {
                let start = self.p;
                while self.p < self.s.len() && is_ident_part(self.s[self.p]) { self.p += 1; }
                let w = core::str::from_utf8(&self.s[start..self.p]).unwrap_or("");
                out.push(keyword(w));
                continue;
            }
            if c == b'\'' || c == b'"' {
                self.p += 1;
                let s = self.read_str(c)?;
                out.push(Tok::Str(s));
                continue;
            }
            if c == b'`' {
                self.p += 1;
                out.push(Tok::Template(self.read_template()?));
                continue;
            }
            match c {
                b'(' => { self.p += 1; out.push(Tok::LParen); }
                b')' => { self.p += 1; out.push(Tok::RParen); }
                b'{' => { self.p += 1; out.push(Tok::LBrace); }
                b'}' => { self.p += 1; out.push(Tok::RBrace); }
                b'[' => { self.p += 1; out.push(Tok::LBracket); }
                b']' => { self.p += 1; out.push(Tok::RBracket); }
                b';' => { self.p += 1; out.push(Tok::Semi); }
                b',' => { self.p += 1; out.push(Tok::Comma); }
                b':' => { self.p += 1; out.push(Tok::Colon); }
                b'?' => {
                    if self.peek_at(1) == b'?' { self.p += 2; out.push(Tok::Nullish); }
                    else { self.p += 1; out.push(Tok::Question); }
                }
                b'.' => {
                    if self.peek_at(1) == b'.' && self.peek_at(2) == b'.' { self.p += 3; out.push(Tok::Ellipsis); }
                    else { self.p += 1; out.push(Tok::Dot); }
                }
                b'~' => { self.p += 1; out.push(Tok::Tilde); }
                b'+' => {
                    if self.peek_at(1) == b'+' { self.p += 2; out.push(Tok::PlusPlus); }
                    else if self.peek_at(1) == b'=' { self.p += 2; out.push(Tok::PlusEq); }
                    else { self.p += 1; out.push(Tok::Plus); }
                }
                b'-' => {
                    if self.peek_at(1) == b'-' { self.p += 2; out.push(Tok::MinusMinus); }
                    else if self.peek_at(1) == b'=' { self.p += 2; out.push(Tok::MinusEq); }
                    else { self.p += 1; out.push(Tok::Minus); }
                }
                b'*' => {
                    if self.peek_at(1) == b'*' { self.p += 2; out.push(Tok::StarStar); }
                    else if self.peek_at(1) == b'=' { self.p += 2; out.push(Tok::StarEq); }
                    else { self.p += 1; out.push(Tok::Star); }
                }
                b'/' => {
                    if self.peek_at(1) == b'=' { self.p += 2; out.push(Tok::SlashEq); }
                    else { self.p += 1; out.push(Tok::Slash); }
                }
                b'%' => {
                    if self.peek_at(1) == b'=' { self.p += 2; out.push(Tok::PercentEq); }
                    else { self.p += 1; out.push(Tok::Percent); }
                }
                b'!' => {
                    if self.peek_at(1) == b'=' && self.peek_at(2) == b'=' { self.p += 3; out.push(Tok::NotEqEq); }
                    else if self.peek_at(1) == b'=' { self.p += 2; out.push(Tok::NotEq); }
                    else { self.p += 1; out.push(Tok::Not); }
                }
                b'=' => {
                    if self.peek_at(1) == b'=' && self.peek_at(2) == b'=' { self.p += 3; out.push(Tok::EqEqEq); }
                    else if self.peek_at(1) == b'=' { self.p += 2; out.push(Tok::EqEq); }
                    else if self.peek_at(1) == b'>' { self.p += 2; out.push(Tok::Arrow); }
                    else { self.p += 1; out.push(Tok::Assign); }
                }
                b'<' => {
                    if self.peek_at(1) == b'=' { self.p += 2; out.push(Tok::Le); }
                    else if self.peek_at(1) == b'<' { self.p += 2; out.push(Tok::Shl); }
                    else { self.p += 1; out.push(Tok::Lt); }
                }
                b'>' => {
                    if self.peek_at(1) == b'=' { self.p += 2; out.push(Tok::Ge); }
                    else if self.peek_at(1) == b'>' && self.peek_at(2) == b'>' { self.p += 3; out.push(Tok::Ushr); }
                    else if self.peek_at(1) == b'>' { self.p += 2; out.push(Tok::Shr); }
                    else { self.p += 1; out.push(Tok::Gt); }
                }
                b'&' => {
                    if self.peek_at(1) == b'&' { self.p += 2; out.push(Tok::AndAnd); }
                    else { self.p += 1; out.push(Tok::Amp); }
                }
                b'|' => {
                    if self.peek_at(1) == b'|' { self.p += 2; out.push(Tok::OrOr); }
                    else { self.p += 1; out.push(Tok::Pipe); }
                }
                b'^' => { self.p += 1; out.push(Tok::Caret); }
                _ => return Err(alloc::format!("unexpected character '{}'", c as char)),
            }
        }
        Ok(out)
    }

    fn read_number(&mut self) -> Result<f64, String> {
        let start = self.p;
        if self.peek() == b'0' && (self.peek_at(1) == b'x' || self.peek_at(1) == b'X') {
            self.p += 2;
            let hs = self.p;
            while hex(self.peek()) >= 0 { self.p += 1; }
            let t = core::str::from_utf8(&self.s[hs..self.p]).unwrap_or("0");
            return u64::from_str_radix(t, 16).map(|v| v as f64).map_err(|_| "bad hex number".into());
        }
        if self.peek() == b'0' && (self.peek_at(1) == b'b' || self.peek_at(1) == b'B') {
            self.p += 2;
            let hs = self.p;
            while self.peek() == b'0' || self.peek() == b'1' { self.p += 1; }
            let t = core::str::from_utf8(&self.s[hs..self.p]).unwrap_or("0");
            return u64::from_str_radix(t, 2).map(|v| v as f64).map_err(|_| "bad binary number".into());
        }
        if self.peek() == b'0' && (self.peek_at(1) == b'o' || self.peek_at(1) == b'O') {
            self.p += 2;
            let hs = self.p;
            while self.peek() >= b'0' && self.peek() <= b'7' { self.p += 1; }
            let t = core::str::from_utf8(&self.s[hs..self.p]).unwrap_or("0");
            return u64::from_str_radix(t, 8).map(|v| v as f64).map_err(|_| "bad octal number".into());
        }
        while self.peek() >= b'0' && self.peek() <= b'9' { self.p += 1; }
        if self.peek() == b'.' { self.p += 1; while self.peek() >= b'0' && self.peek() <= b'9' { self.p += 1; } }
        if self.peek() == b'e' || self.peek() == b'E' {
            self.p += 1;
            if self.peek() == b'+' || self.peek() == b'-' { self.p += 1; }
            while self.peek() >= b'0' && self.peek() <= b'9' { self.p += 1; }
        }
        let t = core::str::from_utf8(&self.s[start..self.p]).unwrap_or("0");
        t.parse::<f64>().map_err(|_| "bad number".into())
    }
}

fn keyword(w: &str) -> Tok {
    match w {
        "var" => Tok::Var,
        "let" => Tok::Let,
        "const" => Tok::Const,
        "function" => Tok::Function,
        "return" => Tok::Return,
        "if" => Tok::If,
        "else" => Tok::Else,
        "while" => Tok::While,
        "do" => Tok::Do,
        "for" => Tok::For,
        "break" => Tok::Break,
        "continue" => Tok::Continue,
        "true" => Tok::True,
        "false" => Tok::False,
        "null" => Tok::Null,
        "undefined" => Tok::Undefined,
        "new" => Tok::New,
        "this" => Tok::This,
        "typeof" => Tok::Typeof,
        "instanceof" => Tok::Instanceof,
        "in" => Tok::In,
        "of" => Tok::Of,
        "delete" => Tok::Delete,
        "void" => Tok::Void,
        "try" => Tok::Try,
        "catch" => Tok::Catch,
        "finally" => Tok::Finally,
        "throw" => Tok::Throw,
        "switch" => Tok::Switch,
        "case" => Tok::Case,
        "default" => Tok::Default,
        _ => Tok::Ident(String::from(w)),
    }
}

fn hex(c: u8) -> i32 {
    if c >= b'0' && c <= b'9' { (c - b'0') as i32 }
    else if c >= b'a' && c <= b'f' { (c - b'a' + 10) as i32 }
    else if c >= b'A' && c <= b'F' { (c - b'A' + 10) as i32 }
    else { -1 }
}

fn push_byte_utf8(out: &mut String, first: u8, s: &[u8], p: &mut usize) {
    if first < 0x80 {
        out.push(first as char);
    } else {
        let extra = if first >= 0xF0 { 3 } else if first >= 0xE0 { 2 } else { 1 };
        let mut buf = [0u8; 4];
        buf[0] = first;
        let mut n = 1;
        for _ in 0..extra {
            if *p < s.len() { buf[n] = s[*p]; *p += 1; n += 1; }
        }
        if let Ok(t) = core::str::from_utf8(&buf[..n]) {
            out.push_str(t);
        }
    }
}
