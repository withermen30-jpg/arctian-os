use alloc::boxed::Box;
use alloc::rc::Rc;
use alloc::string::String;
use alloc::vec::Vec;
use super::lexer::Tpl;

#[derive(Clone, Debug, PartialEq)]
pub enum BinOp {
    Add, Sub, Mul, Div, Mod, Pow,
    Eq, Ne, StrictEq, StrictNe,
    Lt, Le, Gt, Ge,
    BitAnd, BitOr, BitXor, Shl, Shr, Ushr,
    InstanceOf, In,
}

#[derive(Clone, Debug, PartialEq)]
pub enum LogOp {
    And, Or, Nullish,
}

#[derive(Clone, Debug, PartialEq)]
pub enum UnOp {
    Neg, Pos, Not, BitNot, Typeof, Void, Delete,
}

#[derive(Clone, Debug, PartialEq)]
pub enum UpdOp {
    Inc, Dec,
}

#[derive(Clone, Debug)]
pub enum VarKind {
    Var,
    Let,
    Const,
}

#[derive(Clone, Debug)]
pub enum PropKey {
    Ident(String),
    Computed(Box<Expr>),
}

#[derive(Clone, Debug)]
pub struct FuncDecl {
    pub name: Option<String>,
    pub params: Vec<String>,
    pub body: Vec<Stmt>,
    pub is_arrow: bool,
    pub expr_body: Option<Box<Expr>>,
}

#[derive(Clone, Debug)]
pub enum Expr {
    Num(f64),
    Str(String),
    Bool(bool),
    Null,
    Undefined,
    Ident(String),
    This,
    Array(Vec<Expr>),
    Object(Vec<(PropKey, Expr)>),
    Func(Rc<FuncDecl>),
    Unary(UnOp, Box<Expr>),
    Update(UpdOp, Box<Expr>, bool),
    Binary(BinOp, Box<Expr>, Box<Expr>),
    Logical(LogOp, Box<Expr>, Box<Expr>),
    Assign(Option<BinOp>, Box<Expr>, Box<Expr>),
    Cond(Box<Expr>, Box<Expr>, Box<Expr>),
    Call(Box<Expr>, Vec<Expr>),
    New(Box<Expr>, Vec<Expr>),
    Member(Box<Expr>, PropKey, bool),
    Seq(Vec<Expr>),
    Template(Vec<TplPart>),
    Spread(Box<Expr>),
}

#[derive(Clone, Debug)]
pub enum TplPart {
    Lit(String),
    Expr(Expr),
}

#[derive(Clone, Debug)]
pub enum Stmt {
    Expr(Expr),
    Var(VarKind, Vec<(String, Option<Expr>)>),
    Func(Rc<FuncDecl>),
    Return(Option<Expr>),
    If(Expr, Box<Stmt>, Option<Box<Stmt>>),
    While(Expr, Box<Stmt>),
    DoWhile(Box<Stmt>, Expr),
    For(Option<Box<Stmt>>, Option<Expr>, Option<Expr>, Box<Stmt>),
    ForIn(String, Expr, Box<Stmt>, bool),
    Block(Vec<Stmt>),
    Break,
    Continue,
    Throw(Expr),
    Try(Box<Stmt>, Option<(String, Box<Stmt>)>, Option<Box<Stmt>>),
    Switch(Expr, Vec<(Option<Expr>, Vec<Stmt>)>),
    Empty,
}
