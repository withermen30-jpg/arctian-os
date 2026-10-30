use alloc::boxed::Box;
use alloc::rc::Rc;
use alloc::string::{String, ToString};
use alloc::vec;
use alloc::vec::Vec;

use super::ast::*;
use super::lexer::{Lexer, Tok, Tpl};

pub struct Parser {
    t: Vec<Tok>,
    p: usize,
    no_in: bool,
}

impl Parser {
    pub fn new(src: &str) -> Result<Parser, String> {
        let t = Lexer::new(src).tokenize()?;
        Ok(Parser { t, p: 0, no_in: false })
    }

    pub fn parse(src: &str) -> Result<Vec<Stmt>, String> {
        let mut p = Parser::new(src)?;
        let mut out = Vec::new();
        while !p.is(&Tok::Eof) {
            out.push(p.stmt()?);
        }
        Ok(out)
    }

    fn cur(&self) -> &Tok {
        self.t.get(self.p).unwrap_or(&Tok::Eof)
    }

    fn at(&self, o: usize) -> &Tok {
        self.t.get(self.p + o).unwrap_or(&Tok::Eof)
    }

    fn is(&self, t: &Tok) -> bool {
        self.cur() == t
    }

    fn bump(&mut self) -> Tok {
        let t = self.t.get(self.p).cloned().unwrap_or(Tok::Eof);
        if self.p < self.t.len() { self.p += 1; }
        t
    }

    fn expect(&mut self, t: Tok, what: &str) -> Result<(), String> {
        if self.is(&t) { self.bump(); Ok(()) }
        else { Err(alloc::format!("expected {} but found {:?}", what, self.cur())) }
    }

    fn eat(&mut self, t: &Tok) -> bool {
        if self.is(t) { self.bump(); true } else { false }
    }

    fn semicolon(&mut self) {
        let _ = self.eat(&Tok::Semi);
    }

    fn ident(&mut self) -> Result<String, String> {
        match self.bump() {
            Tok::Ident(s) => Ok(s),
            other => Err(alloc::format!("expected identifier, found {:?}", other)),
        }
    }

    pub fn stmt(&mut self) -> Result<Stmt, String> {
        match self.cur().clone() {
            Tok::Semi => { self.bump(); Ok(Stmt::Empty) }
            Tok::LBrace => Ok(Stmt::Block(self.block()?)),
            Tok::Var => self.var_stmt(VarKind::Var),
            Tok::Let => self.var_stmt(VarKind::Let),
            Tok::Const => self.var_stmt(VarKind::Const),
            Tok::Function => {
                self.bump();
                let f = self.func_rest(true)?;
                Ok(Stmt::Func(Rc::new(f)))
            }
            Tok::Return => {
                self.bump();
                if self.is(&Tok::Semi) || self.is(&Tok::RBrace) || self.is(&Tok::Eof) {
                    self.semicolon();
                    Ok(Stmt::Return(None))
                } else {
                    let e = self.expression()?;
                    self.semicolon();
                    Ok(Stmt::Return(Some(e)))
                }
            }
            Tok::If => {
                self.bump();
                self.expect(Tok::LParen, "(")?;
                let c = self.expression()?;
                self.expect(Tok::RParen, ")")?;
                let then = Box::new(self.stmt()?);
                let els = if self.eat(&Tok::Else) { Some(Box::new(self.stmt()?)) } else { None };
                Ok(Stmt::If(c, then, els))
            }
            Tok::While => {
                self.bump();
                self.expect(Tok::LParen, "(")?;
                let c = self.expression()?;
                self.expect(Tok::RParen, ")")?;
                let b = Box::new(self.stmt()?);
                Ok(Stmt::While(c, b))
            }
            Tok::Do => {
                self.bump();
                let b = Box::new(self.stmt()?);
                self.expect(Tok::While, "while")?;
                self.expect(Tok::LParen, "(")?;
                let c = self.expression()?;
                self.expect(Tok::RParen, ")")?;
                self.semicolon();
                Ok(Stmt::DoWhile(b, c))
            }
            Tok::For => self.for_stmt(),
            Tok::Break => { self.bump(); self.semicolon(); Ok(Stmt::Break) }
            Tok::Continue => { self.bump(); self.semicolon(); Ok(Stmt::Continue) }
            Tok::Throw => {
                self.bump();
                let e = self.expression()?;
                self.semicolon();
                Ok(Stmt::Throw(e))
            }
            Tok::Try => self.try_stmt(),
            Tok::Switch => self.switch_stmt(),
            _ => {
                let e = self.expression()?;
                self.semicolon();
                Ok(Stmt::Expr(e))
            }
        }
    }

    fn block(&mut self) -> Result<Vec<Stmt>, String> {
        self.expect(Tok::LBrace, "{")?;
        let mut out = Vec::new();
        while !self.is(&Tok::RBrace) && !self.is(&Tok::Eof) {
            out.push(self.stmt()?);
        }
        self.expect(Tok::RBrace, "}")?;
        Ok(out)
    }

    fn var_stmt(&mut self, kind: VarKind) -> Result<Stmt, String> {
        self.bump();
        let mut decls = Vec::new();
        loop {
            let name = self.ident()?;
            let init = if self.eat(&Tok::Assign) { Some(self.assignment()?) } else { None };
            decls.push((name, init));
            if !self.eat(&Tok::Comma) { break; }
        }
        self.semicolon();
        Ok(Stmt::Var(kind, decls))
    }

    fn for_stmt(&mut self) -> Result<Stmt, String> {
        self.bump();
        self.expect(Tok::LParen, "(")?;
        if self.eat(&Tok::Semi) {
            let cond = if self.is(&Tok::Semi) { None } else { Some(self.expression()?) };
            self.expect(Tok::Semi, ";")?;
            let step = if self.is(&Tok::RParen) { None } else { Some(self.expression()?) };
            self.expect(Tok::RParen, ")")?;
            let body = Box::new(self.stmt()?);
            return Ok(Stmt::For(None, cond, step, body));
        }
        let old = self.no_in;
        self.no_in = true;
        let mut init: Option<Box<Stmt>> = None;
        if self.is(&Tok::Var) || self.is(&Tok::Let) || self.is(&Tok::Const) {
            let kind = match self.cur() { Tok::Var => VarKind::Var, Tok::Let => VarKind::Let, _ => VarKind::Const };
            self.bump();
            let name = self.ident()?;
            let first_init = if self.eat(&Tok::Assign) { Some(self.assignment()?) } else { None };
            if self.is(&Tok::In) || self.is(&Tok::Of) {
                let is_of = self.is(&Tok::Of);
                self.bump();
                self.no_in = old;
                let iter = self.assignment()?;
                self.expect(Tok::RParen, ")")?;
                let body = Box::new(self.stmt()?);
                return Ok(Stmt::ForIn(name, iter, body, is_of));
            }
            let mut decls = vec![(name, first_init)];
            while self.eat(&Tok::Comma) {
                let n = self.ident()?;
                let i = if self.eat(&Tok::Assign) { Some(self.assignment()?) } else { None };
                decls.push((n, i));
            }
            init = Some(Box::new(Stmt::Var(kind, decls)));
        } else if matches!(self.cur(), Tok::Ident(_)) {
            let name = self.ident()?;
            if self.is(&Tok::In) || self.is(&Tok::Of) {
                let is_of = self.is(&Tok::Of);
                self.bump();
                self.no_in = old;
                let iter = self.assignment()?;
                self.expect(Tok::RParen, ")")?;
                let body = Box::new(self.stmt()?);
                return Ok(Stmt::ForIn(name, iter, body, is_of));
            }
            let e = if self.is(&Tok::Assign) {
                self.bump();
                let rhs = self.assignment()?;
                Expr::Assign(None, Box::new(Expr::Ident(name)), Box::new(rhs))
            } else {
                Expr::Ident(name)
            };
            init = Some(Box::new(Stmt::Expr(e)));
        } else {
            let e = self.expression()?;
            init = Some(Box::new(Stmt::Expr(e)));
        }
        self.no_in = old;
        self.expect(Tok::Semi, ";")?;
        let cond = if self.is(&Tok::Semi) { None } else { Some(self.expression()?) };
        self.expect(Tok::Semi, ";")?;
        let step = if self.is(&Tok::RParen) { None } else { Some(self.expression()?) };
        self.expect(Tok::RParen, ")")?;
        let body = Box::new(self.stmt()?);
        Ok(Stmt::For(init, cond, step, body))
    }

    fn try_stmt(&mut self) -> Result<Stmt, String> {
        self.bump();
        let body = Box::new(Stmt::Block(self.block()?));
        let mut catch = None;
        if self.eat(&Tok::Catch) {
            let name = if self.eat(&Tok::LParen) {
                let n = self.ident()?;
                self.expect(Tok::RParen, ")")?;
                n
            } else { String::from("e") };
            catch = Some((name, Box::new(Stmt::Block(self.block()?))));
        }
        let finally = if self.eat(&Tok::Finally) { Some(Box::new(Stmt::Block(self.block()?))) } else { None };
        Ok(Stmt::Try(body, catch, finally))
    }

    fn switch_stmt(&mut self) -> Result<Stmt, String> {
        self.bump();
        self.expect(Tok::LParen, "(")?;
        let disc = self.expression()?;
        self.expect(Tok::RParen, ")")?;
        self.expect(Tok::LBrace, "{")?;
        let mut cases: Vec<(Option<Expr>, Vec<Stmt>)> = Vec::new();
        while !self.is(&Tok::RBrace) && !self.is(&Tok::Eof) {
            let test = if self.eat(&Tok::Case) {
                let e = self.expression()?;
                Some(e)
            } else if self.eat(&Tok::Default) {
                None
            } else {
                return Err("expected case/default".into());
            };
            self.expect(Tok::Colon, ":")?;
            let mut body = Vec::new();
            while !self.is(&Tok::Case) && !self.is(&Tok::Default) && !self.is(&Tok::RBrace) && !self.is(&Tok::Eof) {
                body.push(self.stmt()?);
            }
            cases.push((test, body));
        }
        self.expect(Tok::RBrace, "}")?;
        Ok(Stmt::Switch(disc, cases))
    }

    fn func_rest(&mut self, is_decl: bool) -> Result<FuncDecl, String> {
        let name = if let Tok::Ident(_) = self.cur() {
            Some(self.ident()?)
        } else if is_decl {
            Some(self.ident()?)
        } else {
            None
        };
        self.expect(Tok::LParen, "(")?;
        let mut params = Vec::new();
        if !self.is(&Tok::RParen) {
            loop {
                if self.eat(&Tok::Ellipsis) {
                    if let Tok::Ident(s) = self.bump() { params.push(s); }
                } else {
                    params.push(self.ident()?);
                }
                if !self.eat(&Tok::Comma) { break; }
            }
        }
        self.expect(Tok::RParen, ")")?;
        let body = self.block()?;
        Ok(FuncDecl { name, params, body, is_arrow: false, expr_body: None })
    }

    pub fn expression(&mut self) -> Result<Expr, String> {
        let first = self.assignment()?;
        if self.is(&Tok::Comma) {
            let mut list = vec![first];
            while self.eat(&Tok::Comma) {
                list.push(self.assignment()?);
            }
            Ok(Expr::Seq(list))
        } else {
            Ok(first)
        }
    }

    fn assignment(&mut self) -> Result<Expr, String> {
        if let Some(a) = self.try_arrow()? {
            return Ok(a);
        }
        let left = self.conditional()?;
        let op = match self.cur() {
            Tok::Assign => Some(None),
            Tok::PlusEq => Some(Some(BinOp::Add)),
            Tok::MinusEq => Some(Some(BinOp::Sub)),
            Tok::StarEq => Some(Some(BinOp::Mul)),
            Tok::SlashEq => Some(Some(BinOp::Div)),
            Tok::PercentEq => Some(Some(BinOp::Mod)),
            _ => None,
        };
        if let Some(bin) = op {
            self.bump();
            let right = self.assignment()?;
            Ok(Expr::Assign(bin, Box::new(left), Box::new(right)))
        } else {
            Ok(left)
        }
    }

    fn try_arrow(&mut self) -> Result<Option<Expr>, String> {
        if let Tok::Ident(name) = self.cur().clone() {
            if *self.at(1) == Tok::Arrow {
                self.bump();
                self.bump();
                let (body, expr_body) = self.arrow_body()?;
                return Ok(Some(Expr::Func(Rc::new(FuncDecl {
                    name: None, params: vec![name], body, is_arrow: true, expr_body,
                }))));
            }
            return Ok(None);
        }
        if !self.is(&Tok::LParen) {
            return Ok(None);
        }
        let save = self.p;
        let mut depth = 0i32;
        let mut i = self.p;
        let mut ok = false;
        while i < self.t.len() {
            match &self.t[i] {
                Tok::LParen | Tok::LBracket | Tok::LBrace => depth += 1,
                Tok::RParen | Tok::RBracket | Tok::RBrace => {
                    depth -= 1;
                    if depth == 0 {
                        if i + 1 < self.t.len() {
                            ok = *self.t.get(i + 1).unwrap_or(&Tok::Eof) == Tok::Arrow;
                        }
                        break;
                    }
                }
                Tok::Eof => break,
                _ => {}
            }
            i += 1;
        }
        if !ok {
            return Ok(None);
        }
        self.bump();
        let mut params = Vec::new();
        if !self.is(&Tok::RParen) {
            loop {
                if self.eat(&Tok::Ellipsis) {
                    if let Tok::Ident(s) = self.bump() { params.push(s); }
                } else {
                    params.push(self.ident()?);
                }
                if !self.eat(&Tok::Comma) { break; }
            }
        }
        self.expect(Tok::RParen, ")")?;
        self.expect(Tok::Arrow, "=>")?;
        let (body, expr_body) = self.arrow_body()?;
        let _ = save;
        Ok(Some(Expr::Func(Rc::new(FuncDecl {
            name: None, params, body, is_arrow: true, expr_body,
        }))))
    }

    fn arrow_body(&mut self) -> Result<(Vec<Stmt>, Option<Box<Expr>>), String> {
        if self.is(&Tok::LBrace) {
            Ok((self.block()?, None))
        } else {
            let e = self.assignment()?;
            Ok((Vec::new(), Some(Box::new(e))))
        }
    }

    fn conditional(&mut self) -> Result<Expr, String> {
        let c = self.nullish()?;
        if self.eat(&Tok::Question) {
            let t = self.assignment()?;
            self.expect(Tok::Colon, ":")?;
            let f = self.assignment()?;
            Ok(Expr::Cond(Box::new(c), Box::new(t), Box::new(f)))
        } else {
            Ok(c)
        }
    }

    fn nullish(&mut self) -> Result<Expr, String> {
        let mut e = self.logical_or()?;
        while self.is(&Tok::Nullish) {
            self.bump();
            let r = self.logical_or()?;
            e = Expr::Logical(LogOp::Nullish, Box::new(e), Box::new(r));
        }
        Ok(e)
    }

    fn logical_or(&mut self) -> Result<Expr, String> {
        let mut e = self.logical_and()?;
        while self.is(&Tok::OrOr) {
            self.bump();
            let r = self.logical_and()?;
            e = Expr::Logical(LogOp::Or, Box::new(e), Box::new(r));
        }
        Ok(e)
    }

    fn logical_and(&mut self) -> Result<Expr, String> {
        let mut e = self.bit_or()?;
        while self.is(&Tok::AndAnd) {
            self.bump();
            let r = self.bit_or()?;
            e = Expr::Logical(LogOp::And, Box::new(e), Box::new(r));
        }
        Ok(e)
    }

    fn bit_or(&mut self) -> Result<Expr, String> {
        let mut e = self.bit_xor()?;
        while self.is(&Tok::Pipe) {
            self.bump();
            let r = self.bit_xor()?;
            e = Expr::Binary(BinOp::BitOr, Box::new(e), Box::new(r));
        }
        Ok(e)
    }

    fn bit_xor(&mut self) -> Result<Expr, String> {
        let mut e = self.bit_and()?;
        while self.is(&Tok::Caret) {
            self.bump();
            let r = self.bit_and()?;
            e = Expr::Binary(BinOp::BitXor, Box::new(e), Box::new(r));
        }
        Ok(e)
    }

    fn bit_and(&mut self) -> Result<Expr, String> {
        let mut e = self.equality()?;
        while self.is(&Tok::Amp) {
            self.bump();
            let r = self.equality()?;
            e = Expr::Binary(BinOp::BitAnd, Box::new(e), Box::new(r));
        }
        Ok(e)
    }

    fn equality(&mut self) -> Result<Expr, String> {
        let mut e = self.relational()?;
        loop {
            let op = match self.cur() {
                Tok::EqEq => BinOp::Eq,
                Tok::NotEq => BinOp::Ne,
                Tok::EqEqEq => BinOp::StrictEq,
                Tok::NotEqEq => BinOp::StrictNe,
                _ => break,
            };
            self.bump();
            let r = self.relational()?;
            e = Expr::Binary(op, Box::new(e), Box::new(r));
        }
        Ok(e)
    }

    fn relational(&mut self) -> Result<Expr, String> {
        let mut e = self.shift()?;
        loop {
            let op = match self.cur() {
                Tok::Lt => BinOp::Lt,
                Tok::Le => BinOp::Le,
                Tok::Gt => BinOp::Gt,
                Tok::Ge => BinOp::Ge,
                Tok::Instanceof => BinOp::InstanceOf,
                Tok::In if !self.no_in => BinOp::In,
                _ => break,
            };
            self.bump();
            let r = self.shift()?;
            e = Expr::Binary(op, Box::new(e), Box::new(r));
        }
        Ok(e)
    }

    fn shift(&mut self) -> Result<Expr, String> {
        let mut e = self.additive()?;
        loop {
            let op = match self.cur() {
                Tok::Shl => BinOp::Shl,
                Tok::Shr => BinOp::Shr,
                Tok::Ushr => BinOp::Ushr,
                _ => break,
            };
            self.bump();
            let r = self.additive()?;
            e = Expr::Binary(op, Box::new(e), Box::new(r));
        }
        Ok(e)
    }

    fn additive(&mut self) -> Result<Expr, String> {
        let mut e = self.multiplicative()?;
        loop {
            let op = match self.cur() {
                Tok::Plus => BinOp::Add,
                Tok::Minus => BinOp::Sub,
                _ => break,
            };
            self.bump();
            let r = self.multiplicative()?;
            e = Expr::Binary(op, Box::new(e), Box::new(r));
        }
        Ok(e)
    }

    fn multiplicative(&mut self) -> Result<Expr, String> {
        let mut e = self.exponent()?;
        loop {
            let op = match self.cur() {
                Tok::Star => BinOp::Mul,
                Tok::Slash => BinOp::Div,
                Tok::Percent => BinOp::Mod,
                _ => break,
            };
            self.bump();
            let r = self.exponent()?;
            e = Expr::Binary(op, Box::new(e), Box::new(r));
        }
        Ok(e)
    }

    fn exponent(&mut self) -> Result<Expr, String> {
        let e = self.unary()?;
        if self.is(&Tok::StarStar) {
            self.bump();
            let r = self.exponent()?;
            Ok(Expr::Binary(BinOp::Pow, Box::new(e), Box::new(r)))
        } else {
            Ok(e)
        }
    }

    fn unary(&mut self) -> Result<Expr, String> {
        let op = match self.cur() {
            Tok::Not => Some(UnOp::Not),
            Tok::Tilde => Some(UnOp::BitNot),
            Tok::Plus => Some(UnOp::Pos),
            Tok::Minus => Some(UnOp::Neg),
            Tok::Typeof => Some(UnOp::Typeof),
            Tok::Void => Some(UnOp::Void),
            Tok::Delete => Some(UnOp::Delete),
            _ => None,
        };
        if let Some(o) = op {
            self.bump();
            let e = self.unary()?;
            return Ok(Expr::Unary(o, Box::new(e)));
        }
        if self.is(&Tok::PlusPlus) || self.is(&Tok::MinusMinus) {
            let o = if self.is(&Tok::PlusPlus) { UpdOp::Inc } else { UpdOp::Dec };
            self.bump();
            let e = self.unary()?;
            return Ok(Expr::Update(o, Box::new(e), true));
        }
        let mut e = self.call_member()?;
        loop {
            if self.is(&Tok::PlusPlus) {
                self.bump();
                e = Expr::Update(UpdOp::Inc, Box::new(e), false);
            } else if self.is(&Tok::MinusMinus) {
                self.bump();
                e = Expr::Update(UpdOp::Dec, Box::new(e), false);
            } else {
                break;
            }
        }
        Ok(e)
    }

    fn call_member(&mut self) -> Result<Expr, String> {
        if self.is(&Tok::New) {
            self.bump();
            let callee = self.member_only()?;
            let args = if self.is(&Tok::LParen) { self.args()? } else { Vec::new() };
            return Ok(Expr::New(Box::new(callee), args));
        }
        let mut e = self.primary()?;
        loop {
            match self.cur().clone() {
                Tok::Dot => {
                    self.bump();
                    let name = self.prop_name()?;
                    e = Expr::Member(Box::new(e), PropKey::Ident(name), false);
                }
                Tok::LBracket => {
                    self.bump();
                    let idx = self.expression()?;
                    self.expect(Tok::RBracket, "]")?;
                    e = Expr::Member(Box::new(e), PropKey::Computed(Box::new(idx)), true);
                }
                Tok::LParen => {
                    let args = self.args()?;
                    e = Expr::Call(Box::new(e), args);
                }
                _ => break,
            }
        }
        Ok(e)
    }

    fn member_only(&mut self) -> Result<Expr, String> {
        let mut e = self.primary()?;
        loop {
            match self.cur().clone() {
                Tok::Dot => {
                    self.bump();
                    let name = self.prop_name()?;
                    e = Expr::Member(Box::new(e), PropKey::Ident(name), false);
                }
                Tok::LBracket => {
                    self.bump();
                    let idx = self.expression()?;
                    self.expect(Tok::RBracket, "]")?;
                    e = Expr::Member(Box::new(e), PropKey::Computed(Box::new(idx)), true);
                }
                _ => break,
            }
        }
        Ok(e)
    }

    fn args(&mut self) -> Result<Vec<Expr>, String> {
        self.expect(Tok::LParen, "(")?;
        let mut out = Vec::new();
        if !self.is(&Tok::RParen) {
            loop {
                if self.eat(&Tok::Ellipsis) {
                    out.push(Expr::Spread(Box::new(self.assignment()?)));
                } else {
                    out.push(self.assignment()?);
                }
                if !self.eat(&Tok::Comma) { break; }
            }
        }
        self.expect(Tok::RParen, ")")?;
        Ok(out)
    }

    fn prop_name(&mut self) -> Result<String, String> {
        match self.bump() {
            Tok::Ident(s) => Ok(s),
            Tok::Num(n) => Ok(num_to_key(n)),
            Tok::Str(s) => Ok(s),
            other => Ok(tok_word(&other).unwrap_or_else(|| String::from("?"))),
        }
    }

    fn primary(&mut self) -> Result<Expr, String> {
        match self.cur().clone() {
            Tok::Num(n) => { self.bump(); Ok(Expr::Num(n)) }
            Tok::Str(s) => { self.bump(); Ok(Expr::Str(s)) }
            Tok::True => { self.bump(); Ok(Expr::Bool(true)) }
            Tok::False => { self.bump(); Ok(Expr::Bool(false)) }
            Tok::Null => { self.bump(); Ok(Expr::Null) }
            Tok::Undefined => { self.bump(); Ok(Expr::Undefined) }
            Tok::This => { self.bump(); Ok(Expr::This) }
            Tok::Ident(s) => { self.bump(); Ok(Expr::Ident(s)) }
            Tok::Function => {
                self.bump();
                let f = self.func_rest(false)?;
                Ok(Expr::Func(Rc::new(f)))
            }
            Tok::LParen => {
                self.bump();
                let e = self.expression()?;
                self.expect(Tok::RParen, ")")?;
                Ok(e)
            }
            Tok::LBracket => {
                self.bump();
                let mut items = Vec::new();
                if !self.is(&Tok::RBracket) {
                    loop {
                        if self.eat(&Tok::Ellipsis) {
                            items.push(Expr::Spread(Box::new(self.assignment()?)));
                        } else if self.is(&Tok::Comma) {
                            items.push(Expr::Undefined);
                        } else {
                            items.push(self.assignment()?);
                        }
                        if !self.eat(&Tok::Comma) { break; }
                    }
                }
                self.expect(Tok::RBracket, "]")?;
                Ok(Expr::Array(items))
            }
            Tok::LBrace => {
                self.bump();
                let mut props = Vec::new();
                if !self.is(&Tok::RBrace) {
                    loop {
                        if self.is(&Tok::Ellipsis) {
                            self.bump();
                            let e = self.assignment()?;
                            props.push((PropKey::Computed(Box::new(Expr::Str(String::from("__spread")))), e));
                        } else {
                            let key = self.object_key()?;
                            if self.eat(&Tok::Colon) {
                                let v = self.assignment()?;
                                props.push((key, v));
                            } else if self.is(&Tok::LParen) {
                                let mut params = Vec::new();
                                self.bump();
                                if !self.is(&Tok::RParen) {
                                    loop {
                                        params.push(self.ident()?);
                                        if !self.eat(&Tok::Comma) { break; }
                                    }
                                }
                                self.expect(Tok::RParen, ")")?;
                                let body = self.block()?;
                                let fd = FuncDecl { name: None, params, body, is_arrow: false, expr_body: None };
                                props.push((key, Expr::Func(Rc::new(fd))));
                            } else {
                                let name = match &key {
                                    PropKey::Ident(s) => s.clone(),
                                    _ => return Err("invalid shorthand property".into()),
                                };
                                props.push((key, Expr::Ident(name)));
                            }
                        }
                        if !self.eat(&Tok::Comma) { break; }
                    }
                }
                self.expect(Tok::RBrace, "}")?;
                Ok(Expr::Object(props))
            }
            Tok::Template(parts) => {
                self.bump();
                let mut out = Vec::new();
                for part in parts {
                    match part {
                        Tpl::Lit(s) => out.push(TplPart::Lit(s)),
                        Tpl::Expr(src) => {
                            let stmts = Parser::parse(&src)?;
                            let e = match stmts.into_iter().next() {
                                Some(Stmt::Expr(e)) => e,
                                _ => Expr::Undefined,
                            };
                            out.push(TplPart::Expr(e));
                        }
                    }
                }
                Ok(Expr::Template(out))
            }
            other => Err(alloc::format!("unexpected token {:?}", other)),
        }
    }

    fn object_key(&mut self) -> Result<PropKey, String> {
        match self.bump() {
            Tok::Ident(s) => Ok(PropKey::Ident(s)),
            Tok::Str(s) => Ok(PropKey::Ident(s)),
            Tok::Num(n) => Ok(PropKey::Ident(num_to_key(n))),
            Tok::LBracket => {
                let e = self.assignment()?;
                self.expect(Tok::RBracket, "]")?;
                Ok(PropKey::Computed(Box::new(e)))
            }
            other => Ok(PropKey::Ident(tok_word(&other).unwrap_or_else(|| String::from("?")))),
        }
    }
}

pub fn num_to_key(n: f64) -> String {
    if n == (n as i64) as f64 {
        alloc::format!("{}", n as i64)
    } else {
        alloc::format!("{}", n)
    }
}

fn tok_word(t: &Tok) -> Option<String> {
    let s = match t {
        Tok::Var => "var", Tok::Let => "let", Tok::Const => "const",
        Tok::Function => "function", Tok::Return => "return", Tok::If => "if",
        Tok::Else => "else", Tok::While => "while", Tok::Do => "do", Tok::For => "for",
        Tok::Break => "break", Tok::Continue => "continue", Tok::True => "true",
        Tok::False => "false", Tok::Null => "null", Tok::Undefined => "undefined",
        Tok::New => "new", Tok::This => "this", Tok::Typeof => "typeof",
        Tok::Instanceof => "instanceof", Tok::In => "in", Tok::Of => "of",
        Tok::Delete => "delete", Tok::Void => "void", Tok::Try => "try",
        Tok::Catch => "catch", Tok::Finally => "finally", Tok::Throw => "throw",
        Tok::Switch => "switch", Tok::Case => "case", Tok::Default => "default",
        _ => return None,
    };
    Some(s.to_string())
}
