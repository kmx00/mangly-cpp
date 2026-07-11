// Itanium C++ ABI AST node model.
//
// A Node is a trivially-constructible tagged union (no vtable, no RTTI): the
// Kind selects the active member. Nodes are bump-allocated from an Arena and
// wired together with raw pointers; substitutions make the graph a DAG, which
// raw pointers into the arena represent for free.
//
// Behaviours are free functions:
//   render(node, out)              - human-readable demangled form
//   structurally_equal(a, b)       - substitution-independent structural equality
//                                    (excludes presentation-only fields such as
//                                    Array::elem_is_sub), used both for the
//                                    mangler's substitution table and for the
//                                    round-trip tests.
#ifndef MANGLY_NODES_HPP
#define MANGLY_NODES_HPP

#include <cstdint>

#include "mangly/support.hpp"

namespace mangly {

enum class Kind : std::uint8_t {
    Builtin,
    SourceName,
    TemplateId,
    QualifiedName,
    Pointer,
    LValueRef,
    RValueRef,
    Array,
    CVQualified,
    Function,
    FunctionType,   // F <ret> <params...> E
    MemberPointer,  // M <class> <pointee>
    OperatorName,   // operator symbol (e.g. "pl"); `type` set for "cv"
    CtorDtor,       // C1/C2/C3 or D0/D1/D2
    Literal,        // L <type> <value> E  (non-type template argument)
    TemplateParam,  // T_ / T<n>_  (references a template argument)
    Expression,     // X <expression> E  (dependent template argument)
    SpecialName,    // TV/TI/TS/TT <type>, thunks (Th/Tv/Tc), guard vars
    Decltype,       // Dt <expr> E (id) / DT <expr> E (expression)
    LocalName,      // Z <encoding> E <entity> [<discriminator>]
    Closure,        // Ul <types> E <num>_ (lambda) / Ut <num>_ (unnamed type)
    FuncParam,      // fp_ / fp<n>_  (function parameter, in expressions)
    Pack,           // J <template-args> E  (argument pack)
    PackExpansion,  // Dp <type>  (pack expansion pattern)
    AbiTag,         // <name> B <source-name>  (abi tag)
    VectorType,     // Dv <num> _ <type>  (vendor vector)
    Vendor,         // u <source-name>  (vendor-extended type)
    Fold,           // fl/fr/fL/fR  (fold expression)
    MangledName,    // L <mangled-name> E  (external-name / pointer literal)
};

struct Node {
    Kind kind;
    union {
        struct {
            StringView code;
        } builtin;
        struct {
            StringView text;
        } source;
        struct {
            const Node* name;
            const Node* const* args;
            std::uint32_t nargs;
        } tmpl;
        struct {
            const Node* const* parts;
            std::uint32_t nparts;
        } qual;
        struct {
            const Node* inner;
        } ref;  // Pointer / LValueRef / RValueRef
        struct {
            const Node* inner;
            StringView dim;    // bound text, verbatim; valid iff has_dim
            bool has_dim;
            bool elem_is_sub;  // presentation-only: excluded from equality
        } array;
        struct {
            const Node* inner;
            StringView quals;  // raw ABI qualifier chars, source order
        } cv;
        struct {
            const Node* name;
            const Node* ret;   // nullable (template return type)
            const Node* const* params;
            std::uint32_t nparams;
            StringView this_quals;  // cv on the implicit this (member fns), source order
            char ref_qual;          // 'R', 'O', or '\0'
        } func;
        struct {
            const Node* ret;
            const Node* const* params;
            std::uint32_t nparams;
        } func_type;  // FunctionType
        struct {
            const Node* cls;
            const Node* pointee;
        } memptr;  // MemberPointer
        struct {
            StringView code;   // ABI operator code, e.g. "pl", "cl", "cv"
            const Node* type;  // non-null only for "cv" (conversion operator)
        } oper;
        struct {
            StringView code;  // "C1".."C3" / "D0".."D2", verbatim
        } ctordtor;
        struct {
            const Node* type;
            StringView value;  // literal value text, verbatim
        } literal;
        struct {
            std::uint32_t index;   // 0 == T_, 1 == T0_, ...
            const Node* resolved;  // the referenced template argument (for rendering)
        } tparam;
        struct {
            StringView op;              // ABI code: operator ("pl") or "st"/"tl"/...
            const Node* const* operands;
            std::uint32_t noperands;    // operands may be types or sub-expressions
        } expr;
        struct {
            StringView code;   // "TV"/"TI"/"TS"/"TT" or thunk "Th"/"Tv"/"Tc"
            StringView extra;  // thunk offset spec (verbatim); empty otherwise
            const Node* inner; // the type (TV..) / base encoding (thunk) / TC "in" type
            const Node* inner2; // second type for construction vtable (TC)
        } special;
        struct {
            const Node* expr;
            bool id_form;  // true == Dt (id-expression), false == DT (expression)
        } decl_type;
        struct {
            const Node* scope;   // enclosing <encoding>
            const Node* entity;  // the local <name>
            StringView disc;     // discriminator (verbatim); empty if none
        } local;
        struct {
            const Node* const* params;  // lambda parameter types (null for Ut)
            std::uint32_t nparams;
            StringView num;  // instance number text (empty == first)
            bool unnamed;    // true == Ut (unnamed type), false == Ul (lambda)
        } closure;
        struct {
            StringView num;  // fp<num>_ (empty == parameter 0)
        } fparam;
        struct {
            const Node* const* elems;
            std::uint32_t nelems;
        } pack;
        struct {
            const Node* pattern;
        } pack_exp;
        struct {
            const Node* inner;
            StringView tag;
        } abitag;
        struct {
            const Node* elem;
            StringView num;  // dimension (verbatim)
        } vec;
        struct {
            StringView name;  // vendor-extended type name
        } vendor;
        struct {
            StringView kind;  // "fl"/"fr"/"fL"/"fR"
            StringView op;    // fold operator code, e.g. "pl"
            const Node* a;    // pack (unary) or first operand (binary)
            const Node* b;    // second operand (binary), else null
        } fold;
        struct {
            const Node* encoding;  // the _Z <encoding> named by the literal
        } mangled;
    };
};

inline Node* make_node(Arena& a, Kind k) {
    Node* n = a.alloc<Node>();
    if (n) n->kind = k;
    return n;
}

// -------------------------------------------------------------------- builtins

// Human spelling for a builtin type code, or nullptr if unrecognized.
inline const char* builtin_spelling(StringView c) {
    if (c.size == 2 && c.data[0] == 'D') {
        switch (c.data[1]) {
            case 'd': return "decimal64";
            case 'e': return "decimal128";
            case 'f': return "decimal32";
            case 'h': return "half";
            case 'i': return "char32_t";
            case 's': return "char16_t";
            case 'u': return "char8_t";
            case 'a': return "auto";
            case 'c': return "decltype(auto)";
            case 'n': return "std::nullptr_t";
            default: return nullptr;
        }
    }
    if (c.size == 1) {
        switch (c.data[0]) {
            case 'v': return "void";
            case 'w': return "wchar_t";
            case 'b': return "bool";
            case 'c': return "char";
            case 'a': return "signed char";
            case 'h': return "unsigned char";
            case 's': return "short";
            case 't': return "unsigned short";
            case 'i': return "int";
            case 'j': return "unsigned int";
            case 'l': return "long";
            case 'm': return "unsigned long";
            case 'x': return "long long";
            case 'y': return "unsigned long long";
            case 'n': return "__int128";
            case 'o': return "unsigned __int128";
            case 'f': return "float";
            case 'd': return "double";
            case 'e': return "long double";
            case 'g': return "__float128";
            case 'z': return "...";
            default: return nullptr;
        }
    }
    return nullptr;
}

// Grammar recognizers (single source of truth, mirrored by builtin_spelling).
inline bool is_builtin_code(char c) {
    switch (c) {
        case 'v': case 'w': case 'b': case 'c': case 'a': case 'h': case 's':
        case 't': case 'i': case 'j': case 'l': case 'm': case 'x': case 'y':
        case 'n': case 'o': case 'f': case 'd': case 'e': case 'g': case 'z':
            return true;
        default:
            return false;
    }
}

inline bool is_builtin_d_code(char second) {
    switch (second) {
        case 'd': case 'e': case 'f': case 'h': case 'i': case 's': case 'u':
        case 'a': case 'c': case 'n':
            return true;
        default:
            return false;
    }
}

inline const char* cv_name(char q) {
    switch (q) {
        case 'K': return "const";
        case 'V': return "volatile";
        case 'r': return "restrict";
        default: return "";
    }
}

// Spelling for an <operator-name> code (the part after "operator"). Returns
// nullptr for unknown codes. "cv" (conversion) and "li" (literal) carry extra
// operands and are handled by the caller.
inline const char* operator_symbol(StringView c) {
    if (c.size != 2) return nullptr;
    char a = c.data[0], b = c.data[1];
#define MANGLY_OP(x, y, s) if (a == x && b == y) return s;
    MANGLY_OP('n', 'w', " new") MANGLY_OP('n', 'a', " new[]")
    MANGLY_OP('d', 'l', " delete") MANGLY_OP('d', 'a', " delete[]")
    MANGLY_OP('p', 's', "+") MANGLY_OP('n', 'g', "-")
    MANGLY_OP('a', 'd', "&") MANGLY_OP('d', 'e', "*") MANGLY_OP('c', 'o', "~")
    MANGLY_OP('p', 'l', "+") MANGLY_OP('m', 'i', "-") MANGLY_OP('m', 'l', "*")
    MANGLY_OP('d', 'v', "/") MANGLY_OP('r', 'm', "%") MANGLY_OP('a', 'n', "&")
    MANGLY_OP('o', 'r', "|") MANGLY_OP('e', 'o', "^")
    MANGLY_OP('a', 'S', "=") MANGLY_OP('p', 'L', "+=") MANGLY_OP('m', 'I', "-=")
    MANGLY_OP('m', 'L', "*=") MANGLY_OP('d', 'V', "/=") MANGLY_OP('r', 'M', "%=")
    MANGLY_OP('a', 'N', "&=") MANGLY_OP('o', 'R', "|=") MANGLY_OP('e', 'O', "^=")
    MANGLY_OP('l', 's', "<<") MANGLY_OP('r', 's', ">>")
    MANGLY_OP('l', 'S', "<<=") MANGLY_OP('r', 'S', ">>=")
    MANGLY_OP('e', 'q', "==") MANGLY_OP('n', 'e', "!=")
    MANGLY_OP('l', 't', "<") MANGLY_OP('g', 't', ">")
    MANGLY_OP('l', 'e', "<=") MANGLY_OP('g', 'e', ">=") MANGLY_OP('s', 's', "<=>")
    MANGLY_OP('n', 't', "!") MANGLY_OP('a', 'a', "&&") MANGLY_OP('o', 'o', "||")
    MANGLY_OP('p', 'p', "++") MANGLY_OP('m', 'm', "--") MANGLY_OP('c', 'm', ",")
    MANGLY_OP('p', 'm', "->*") MANGLY_OP('p', 't', "->")
    MANGLY_OP('c', 'l', "()") MANGLY_OP('i', 'x', "[]") MANGLY_OP('q', 'u', "?")
#undef MANGLY_OP
    return nullptr;
}

// True if `a b` begins an <operator-name>. "cv"/"li" take an extra operand.
inline bool is_operator_code(char a, char b) {
    char buf[2] = {a, b};
    if (operator_symbol(StringView{buf, 2})) return true;
    return (a == 'c' && b == 'v') || (a == 'l' && b == 'i');
}

// Arity of a 2-char operator code in <expression> context (0 if not one).
inline int expr_operator_arity(char a, char b) {
    if (a == 'q' && b == 'u') return 3;  // ternary ?:
    switch (a) {  // unary operators
        case 'p': if (b == 's' || b == 'p') return 1; break;  // +x, ++x
        case 'n': if (b == 'g' || b == 't') return 1; break;  // -x, !x
        case 'a': if (b == 'd') return 1; break;              // &x
        case 'd': if (b == 'e') return 1; break;              // *x
        case 'c': if (b == 'o') return 1; break;              // ~x
        case 'm': if (b == 'm') return 1; break;              // --x
        default: break;
    }
    switch (a) {  // binary operators
        case 'p': if (b == 'l' || b == 'L' || b == 'm' || b == 't') return 2; break;
        case 'm': if (b == 'i' || b == 'l' || b == 'I' || b == 'L') return 2; break;
        case 'd': if (b == 'v' || b == 'V') return 2; break;
        case 'r': if (b == 'm' || b == 'M' || b == 's' || b == 'S') return 2; break;
        case 'a': if (b == 'n' || b == 'N' || b == 'S' || b == 'a') return 2; break;
        case 'o': if (b == 'r' || b == 'R' || b == 'o') return 2; break;
        case 'e': if (b == 'o' || b == 'O' || b == 'q') return 2; break;
        case 'l': if (b == 's' || b == 'S' || b == 't' || b == 'e') return 2; break;
        case 'g': if (b == 't' || b == 'e') return 2; break;
        case 'n': if (b == 'e') return 2; break;
        case 's': if (b == 's') return 2; break;
        case 'c': if (b == 'm') return 2; break;
        default: break;
    }
    return 0;
}

// ---------------------------------------------------------------------- render

inline void render(const Node* n, OutputBuffer& o);       // forward declaration
inline void render_expr(const Node* n, OutputBuffer& o);  // forward declaration

// Render the bare class identifier of `p` (used to name a ctor/dtor): a source
// name directly, a template-id / qualified-name via its base identifier.
inline void render_class_name(const Node* p, OutputBuffer& o) {
    switch (p->kind) {
        case Kind::SourceName: o.append(p->source.text); return;
        case Kind::TemplateId: render_class_name(p->tmpl.name, o); return;
        case Kind::QualifiedName:
            if (p->qual.nparts) render_class_name(p->qual.parts[p->qual.nparts - 1], o);
            return;
        default: render(p, o); return;
    }
}

inline void render_params(const Node* const* params, std::uint32_t n, OutputBuffer& o) {
    o.push('(');
    for (std::uint32_t i = 0; i < n; ++i) {
        if (i) o.push(',');
        render(params[i], o);
    }
    o.push(')');
}

// Render `target` behind an indirection token `op` ("*", "&", "&&"). Function
// and array targets take declarator form: `ret (op)(params)`, `elem (op)[dim]`.
inline void render_indirection(const Node* target, const char* op, OutputBuffer& o) {
    if (target->kind == Kind::FunctionType) {
        render(target->func_type.ret, o);
        o.append(" (");
        o.append(op);
        o.push(')');
        render_params(target->func_type.params, target->func_type.nparams, o);
    } else if (target->kind == Kind::Array) {
        render(target->array.inner, o);
        o.append(" (");
        o.append(op);
        o.append(")[");
        if (target->array.has_dim) o.append(target->array.dim);
        o.push(']');
    } else {
        render(target, o);
        o.push(' ');
        o.append(op);
    }
}

inline void render(const Node* n, OutputBuffer& o) {
    switch (n->kind) {
        case Kind::Builtin: {
            const char* s = builtin_spelling(n->builtin.code);
            if (s) o.append(s);
            else o.append(n->builtin.code);
            return;
        }
        case Kind::SourceName:
            o.append(n->source.text);
            return;
        case Kind::TemplateId: {
            render(n->tmpl.name, o);
            o.push('<');
            for (std::uint32_t i = 0; i < n->tmpl.nargs; ++i) {
                if (i) o.push(',');
                render(n->tmpl.args[i], o);
            }
            if (o.back() == '>') o.push(' ');  // avoid a ">>" digraph
            o.push('>');
            return;
        }
        case Kind::QualifiedName:
            for (std::uint32_t i = 0; i < n->qual.nparts; ++i) {
                if (i) o.append("::");
                const Node* p = n->qual.parts[i];
                if (p->kind == Kind::CtorDtor) {
                    bool dtor = p->ctordtor.code.size && p->ctordtor.code.data[0] == 'D';
                    if (dtor) o.push('~');
                    if (i > 0) render_class_name(n->qual.parts[i - 1], o);
                    else o.append(p->ctordtor.code);
                } else {
                    render(p, o);
                }
            }
            return;
        case Kind::Pointer:
            render_indirection(n->ref.inner, "*", o);
            return;
        case Kind::LValueRef:
            render_indirection(n->ref.inner, "&", o);
            return;
        case Kind::RValueRef:
            render_indirection(n->ref.inner, "&&", o);
            return;
        case Kind::Array:
            render(n->array.inner, o);
            o.append(n->array.elem_is_sub ? "[]" : " []");
            return;
        case Kind::CVQualified: {
            render(n->cv.inner, o);
            o.push(' ');
            for (std::uint32_t i = 0; i < n->cv.quals.size; ++i) {
                if (i) o.push(' ');
                o.append(cv_name(n->cv.quals.data[i]));
            }
            return;
        }
        case Kind::Function:
            render(n->func.name, o);
            render_params(n->func.params, n->func.nparams, o);
            for (std::uint32_t i = 0; i < n->func.this_quals.size; ++i) {
                o.push(' ');
                o.append(cv_name(n->func.this_quals.data[i]));
            }
            if (n->func.ref_qual == 'R') o.append(" &");
            else if (n->func.ref_qual == 'O') o.append(" &&");
            return;
        case Kind::FunctionType:
            render(n->func_type.ret, o);
            o.push(' ');
            render_params(n->func_type.params, n->func_type.nparams, o);
            return;
        case Kind::MemberPointer: {
            const Node* pt = n->memptr.pointee;
            if (pt->kind == Kind::FunctionType) {
                render(pt->func_type.ret, o);
                o.append(" (");
                render(n->memptr.cls, o);
                o.append("::*)");
                render_params(pt->func_type.params, pt->func_type.nparams, o);
            } else if (pt->kind == Kind::Array) {
                render(pt->array.inner, o);
                o.append(" (");
                render(n->memptr.cls, o);
                o.append("::*)[");
                if (pt->array.has_dim) o.append(pt->array.dim);
                o.push(']');
            } else {
                render(pt, o);
                o.push(' ');
                render(n->memptr.cls, o);
                o.append("::*");
            }
            return;
        }
        case Kind::OperatorName: {
            o.append("operator");
            const char* sym = operator_symbol(n->oper.code);
            if (sym) {
                o.append(sym);
            } else if (n->oper.code.size == 2 && n->oper.code.data[0] == 'c' &&
                       n->oper.code.data[1] == 'v') {
                o.push(' ');
                if (n->oper.type) render(n->oper.type, o);
            } else if (n->oper.code.size == 2 && n->oper.code.data[0] == 'l' &&
                       n->oper.code.data[1] == 'i') {
                o.append("\"\" ");
                if (n->oper.type) render(n->oper.type, o);
            } else {
                o.append(n->oper.code);
            }
            return;
        }
        case Kind::CtorDtor:
            o.append(n->ctordtor.code);  // rendered in context by QualifiedName
            return;
        case Kind::Literal: {
            const Node* t = n->literal.type;
            if (t && t->kind == Kind::Builtin && t->builtin.code.size == 1 &&
                t->builtin.code.data[0] == 'b') {
                bool one = n->literal.value.size == 1 && n->literal.value.data[0] == '1';
                o.append(one ? "true" : "false");
            } else if (n->literal.value.size && n->literal.value.data[0] == 'n') {
                o.push('-');  // negative literal: leading 'n'
                o.append(n->literal.value.data + 1, n->literal.value.size - 1);
            } else if (!n->literal.value.size && t) {
                render(t, o);  // valueless literal (e.g. LDnE): show the type
            } else {
                o.append(n->literal.value);
            }
            return;
        }
        case Kind::TemplateParam:
            if (n->tparam.resolved) {
                render(n->tparam.resolved, o);
            } else {  // unresolved: emit the ABI spelling
                o.push('T');
                if (n->tparam.index) o.append_uint(n->tparam.index - 1);
                o.push('_');
            }
            return;
        case Kind::Expression:
            render_expr(n, o);
            return;
        case Kind::SpecialName: {
            StringView c = n->special.code;
            const char* phrase = "";
            if (c.size == 2) {
                char a = c.data[0], b = c.data[1];
                if (a == 'T' && b == 'V') phrase = "vtable for ";
                else if (a == 'T' && b == 'T') phrase = "VTT for ";
                else if (a == 'T' && b == 'I') phrase = "typeinfo for ";
                else if (a == 'T' && b == 'S') phrase = "typeinfo name for ";
                else if (a == 'T' && b == 'h') phrase = "non-virtual thunk to ";
                else if (a == 'T' && b == 'v') phrase = "virtual thunk to ";
                else if (a == 'T' && b == 'c') phrase = "covariant return thunk to ";
                else if (a == 'G' && b == 'V') phrase = "guard variable for ";
            }
            if (c.size == 2 && c.data[0] == 'T' && c.data[1] == 'C') {
                // construction vtable for <inner2>-in-<inner>
                o.append("construction vtable for ");
                render(n->special.inner2, o);
                o.append("-in-");
                render(n->special.inner, o);
                return;
            }
            o.append(phrase);
            render(n->special.inner, o);
            return;
        }
        case Kind::Decltype:
            o.append("decltype (");
            render(n->decl_type.expr, o);
            o.push(')');
            return;
        case Kind::LocalName:
            render(n->local.scope, o);
            o.append("::");
            render(n->local.entity, o);
            return;
        case Kind::Closure: {
            // display index: first (empty) == #1, "0" == #2, "1" == #3, ...
            std::uint64_t idx = 1;
            if (n->closure.num.size) {
                std::uint64_t raw = 0;
                for (std::uint32_t i = 0; i < n->closure.num.size; ++i)
                    raw = raw * 10 +
                          static_cast<std::uint64_t>(n->closure.num.data[i] - '0');
                idx = raw + 2;
            }
            if (n->closure.unnamed) {
                o.append("{unnamed type#");
            } else {
                o.append("{lambda(");
                for (std::uint32_t i = 0; i < n->closure.nparams; ++i) {
                    if (i) o.push(',');
                    render(n->closure.params[i], o);
                }
                o.append(")#");
            }
            o.append_uint(idx);
            o.push('}');
            return;
        }
        case Kind::FuncParam: {
            std::uint64_t idx = 1;
            if (n->fparam.num.size) {
                std::uint64_t raw = 0;
                bool digits = true;
                for (std::uint32_t i = 0; i < n->fparam.num.size; ++i) {
                    char ch = n->fparam.num.data[i];
                    if (ch < '0' || ch > '9') { digits = false; break; }
                    raw = raw * 10 + static_cast<std::uint64_t>(ch - '0');
                }
                if (digits) idx = raw + 2;
            }
            o.append("{parm#");
            o.append_uint(idx);
            o.push('}');
            return;
        }
        case Kind::Pack:
            for (std::uint32_t i = 0; i < n->pack.nelems; ++i) {
                if (i) o.append(", ");
                render(n->pack.elems[i], o);
            }
            return;
        case Kind::PackExpansion:
            // Approximation: render the pattern; a bare pack param expands to its
            // comma-separated elements, patterns render once.
            render(n->pack_exp.pattern, o);
            return;
        case Kind::AbiTag:
            render(n->abitag.inner, o);
            o.append("[abi:");
            o.append(n->abitag.tag);
            o.push(']');
            return;
        case Kind::VectorType:
            render(n->vec.elem, o);
            o.append(" __vector(");
            o.append(n->vec.num);
            o.push(')');
            return;
        case Kind::Vendor:
            o.append(n->vendor.name);
            return;
        case Kind::Fold: {
            const char* sym = operator_symbol(n->fold.op);
            if (!sym) sym = "?";
            char k = n->fold.kind.size == 2 ? n->fold.kind.data[1] : 0;
            o.push('(');
            if (k == 'l') {  // unary left: (... op a)
                o.append("...");
                o.append(sym);
                render(n->fold.a, o);
            } else if (k == 'r') {  // unary right: (a op ...)
                render(n->fold.a, o);
                o.append(sym);
                o.append("...");
            } else {  // binary (fL/fR): (a op ... op b)
                render(n->fold.a, o);
                o.append(sym);
                o.append("...");
                o.append(sym);
                render(n->fold.b, o);
            }
            o.push(')');
            return;
        }
        case Kind::MangledName:
            render(n->mangled.encoding, o);  // the named function/object
            return;
    }
}

// Render a dependent expression (a template argument written as X<expr>E).
// Style is readable and parenthesized; it is not required to match c++filt.
inline void render_expr(const Node* n, OutputBuffer& o) {
    StringView op = n->expr.op;
    const Node* const* a = n->expr.operands;
    std::uint32_t k = n->expr.noperands;
    auto is = [&](const char* s) {
        std::uint32_t m = static_cast<std::uint32_t>(std::strlen(s));
        return op.size == m && std::memcmp(op.data, s, m) == 0;
    };
    if (is("sZ") && k >= 1) {  // sizeof...(pack)
        o.append("sizeof...(");
        render(a[0], o);
        o.push(')');
        return;
    }
    if (is("cv") && k >= 2) {  // cast: (type)(expr)
        o.push('(');
        render(a[0], o);
        o.append(")(");
        render(a[1], o);
        o.push(')');
        return;
    }
    if ((is("dt") || is("pt")) && k >= 2) {  // member access: a.b / a->b
        render(a[0], o);
        o.append(is("dt") ? "." : "->");
        render(a[1], o);
        return;
    }
    if ((is("st") || is("sz")) && k >= 1) {  // sizeof type / expr
        o.append("sizeof (");
        render(a[0], o);
        o.push(')');
        return;
    }
    if ((is("at") || is("az")) && k >= 1) {  // alignof type / expr
        o.append("alignof (");
        render(a[0], o);
        o.push(')');
        return;
    }
    if (is("tl") && k >= 1) {  // T{ args }
        render(a[0], o);
        o.push('{');
        for (std::uint32_t i = 1; i < k; ++i) {
            if (i > 1) o.push(',');
            render(a[i], o);
        }
        o.push('}');
        return;
    }
    if (is("il")) {  // { args }
        o.push('{');
        for (std::uint32_t i = 0; i < k; ++i) {
            if (i) o.push(',');
            render(a[i], o);
        }
        o.push('}');
        return;
    }
    if (is("cl") && k >= 1) {  // callee(args)
        render(a[0], o);
        o.push('(');
        for (std::uint32_t i = 1; i < k; ++i) {
            if (i > 1) o.push(',');
            render(a[i], o);
        }
        o.push(')');
        return;
    }
    if (is("sr") && k >= 2) {  // scope resolution: type::name
        render(a[0], o);
        o.append("::");
        render(a[1], o);
        return;
    }
    if (is("sp") && k >= 1) {  // pack expansion in an expression: expr...
        render(a[0], o);
        o.append("...");
        return;
    }
    if ((is("nw") || is("na")) && k >= 1) {  // new T / new[] T (+ initializer)
        o.append(is("na") ? "new[] " : "new ");
        render(a[0], o);
        if (k > 1) {
            o.push('(');
            for (std::uint32_t i = 1; i < k; ++i) {
                if (i > 1) o.push(',');
                render(a[i], o);
            }
            o.push(')');
        }
        return;
    }
    if ((is("sc") || is("dc") || is("cc") || is("rc")) && k >= 2) {  // named casts
        o.append(is("sc") ? "static_cast<"
                 : is("dc") ? "dynamic_cast<"
                 : is("cc") ? "const_cast<"
                            : "reinterpret_cast<");
        render(a[0], o);
        o.append(">(");
        render(a[1], o);
        o.push(')');
        return;
    }
    const char* sym = operator_symbol(op);
    if (sym && k == 3) {  // ternary (qu)
        o.push('(');
        render(a[0], o);
        o.append(") ? (");
        render(a[1], o);
        o.append(") : (");
        render(a[2], o);
        o.push(')');
        return;
    }
    if (sym && k == 2) {  // binary
        o.push('(');
        render(a[0], o);
        o.push(')');
        o.append(sym);
        o.push('(');
        render(a[1], o);
        o.push(')');
        return;
    }
    if (sym && k == 1) {  // unary (prefix)
        o.append(sym);
        o.push('(');
        render(a[0], o);
        o.push(')');
        return;
    }
    // Fallback: emit the raw op then operands, so nothing crashes.
    o.append(op);
    for (std::uint32_t i = 0; i < k; ++i) {
        o.push(i ? ',' : '(');
        render(a[i], o);
    }
    if (k) o.push(')');
}

// ------------------------------------------------------------ structural equality

inline bool structurally_equal(const Node* a, const Node* b) {
    if (a == b) return true;
    if (!a || !b || a->kind != b->kind) return false;
    switch (a->kind) {
        case Kind::Builtin:
            return sv_equal(a->builtin.code, b->builtin.code);
        case Kind::SourceName:
            return sv_equal(a->source.text, b->source.text);
        case Kind::TemplateId: {
            if (a->tmpl.nargs != b->tmpl.nargs) return false;
            if (!structurally_equal(a->tmpl.name, b->tmpl.name)) return false;
            for (std::uint32_t i = 0; i < a->tmpl.nargs; ++i) {
                if (!structurally_equal(a->tmpl.args[i], b->tmpl.args[i])) {
                    return false;
                }
            }
            return true;
        }
        case Kind::QualifiedName: {
            if (a->qual.nparts != b->qual.nparts) return false;
            for (std::uint32_t i = 0; i < a->qual.nparts; ++i) {
                if (!structurally_equal(a->qual.parts[i], b->qual.parts[i])) {
                    return false;
                }
            }
            return true;
        }
        case Kind::Pointer:
        case Kind::LValueRef:
        case Kind::RValueRef:
            return structurally_equal(a->ref.inner, b->ref.inner);
        case Kind::Array:
            if (a->array.has_dim != b->array.has_dim) return false;
            if (a->array.has_dim && !sv_equal(a->array.dim, b->array.dim)) {
                return false;  // elem_is_sub is presentation-only, excluded
            }
            return structurally_equal(a->array.inner, b->array.inner);
        case Kind::CVQualified:
            return sv_equal(a->cv.quals, b->cv.quals) &&
                   structurally_equal(a->cv.inner, b->cv.inner);
        case Kind::Function: {
            if (a->func.nparams != b->func.nparams) return false;
            if (a->func.ref_qual != b->func.ref_qual) return false;
            if (!sv_equal(a->func.this_quals, b->func.this_quals)) return false;
            if (!structurally_equal(a->func.name, b->func.name)) return false;
            if ((a->func.ret == nullptr) != (b->func.ret == nullptr)) return false;
            if (a->func.ret && !structurally_equal(a->func.ret, b->func.ret)) {
                return false;
            }
            for (std::uint32_t i = 0; i < a->func.nparams; ++i) {
                if (!structurally_equal(a->func.params[i], b->func.params[i])) {
                    return false;
                }
            }
            return true;
        }
        case Kind::FunctionType: {
            if (a->func_type.nparams != b->func_type.nparams) return false;
            if (!structurally_equal(a->func_type.ret, b->func_type.ret)) return false;
            for (std::uint32_t i = 0; i < a->func_type.nparams; ++i) {
                if (!structurally_equal(a->func_type.params[i], b->func_type.params[i])) {
                    return false;
                }
            }
            return true;
        }
        case Kind::MemberPointer:
            return structurally_equal(a->memptr.cls, b->memptr.cls) &&
                   structurally_equal(a->memptr.pointee, b->memptr.pointee);
        case Kind::OperatorName:
            if (!sv_equal(a->oper.code, b->oper.code)) return false;
            if ((a->oper.type == nullptr) != (b->oper.type == nullptr)) return false;
            return !a->oper.type || structurally_equal(a->oper.type, b->oper.type);
        case Kind::CtorDtor:
            return sv_equal(a->ctordtor.code, b->ctordtor.code);
        case Kind::Literal:
            return sv_equal(a->literal.value, b->literal.value) &&
                   structurally_equal(a->literal.type, b->literal.type);
        case Kind::TemplateParam:
            return a->tparam.index == b->tparam.index;
        case Kind::Expression: {
            if (a->expr.noperands != b->expr.noperands) return false;
            if (!sv_equal(a->expr.op, b->expr.op)) return false;
            for (std::uint32_t i = 0; i < a->expr.noperands; ++i) {
                if (!structurally_equal(a->expr.operands[i], b->expr.operands[i])) {
                    return false;
                }
            }
            return true;
        }
        case Kind::SpecialName:
            return sv_equal(a->special.code, b->special.code) &&
                   sv_equal(a->special.extra, b->special.extra) &&
                   structurally_equal(a->special.inner, b->special.inner) &&
                   structurally_equal(a->special.inner2, b->special.inner2);
        case Kind::Decltype:
            return a->decl_type.id_form == b->decl_type.id_form &&
                   structurally_equal(a->decl_type.expr, b->decl_type.expr);
        case Kind::LocalName:
            return sv_equal(a->local.disc, b->local.disc) &&
                   structurally_equal(a->local.scope, b->local.scope) &&
                   structurally_equal(a->local.entity, b->local.entity);
        case Kind::Closure: {
            if (a->closure.unnamed != b->closure.unnamed) return false;
            if (a->closure.nparams != b->closure.nparams) return false;
            if (!sv_equal(a->closure.num, b->closure.num)) return false;
            for (std::uint32_t i = 0; i < a->closure.nparams; ++i) {
                if (!structurally_equal(a->closure.params[i], b->closure.params[i])) {
                    return false;
                }
            }
            return true;
        }
        case Kind::FuncParam:
            return sv_equal(a->fparam.num, b->fparam.num);
        case Kind::Pack: {
            if (a->pack.nelems != b->pack.nelems) return false;
            for (std::uint32_t i = 0; i < a->pack.nelems; ++i) {
                if (!structurally_equal(a->pack.elems[i], b->pack.elems[i])) {
                    return false;
                }
            }
            return true;
        }
        case Kind::PackExpansion:
            return structurally_equal(a->pack_exp.pattern, b->pack_exp.pattern);
        case Kind::AbiTag:
            return sv_equal(a->abitag.tag, b->abitag.tag) &&
                   structurally_equal(a->abitag.inner, b->abitag.inner);
        case Kind::VectorType:
            return sv_equal(a->vec.num, b->vec.num) &&
                   structurally_equal(a->vec.elem, b->vec.elem);
        case Kind::Vendor:
            return sv_equal(a->vendor.name, b->vendor.name);
        case Kind::Fold:
            return sv_equal(a->fold.kind, b->fold.kind) &&
                   sv_equal(a->fold.op, b->fold.op) &&
                   structurally_equal(a->fold.a, b->fold.a) &&
                   structurally_equal(a->fold.b, b->fold.b);
        case Kind::MangledName:
            return structurally_equal(a->mangled.encoding, b->mangled.encoding);
    }
    return false;
}

}  // namespace mangly

#endif  // MANGLY_NODES_HPP
