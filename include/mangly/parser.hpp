// Recursive-descent parser for Itanium C++ ABI mangled names.
//
// No exceptions: every production returns a Node* and yields nullptr once the
// parser latches failure (peek at parser.error() for a static diagnostic). AST
// nodes and their child arrays are bump-allocated from a caller-supplied Arena;
// StringViews reference the input, which must outlive the AST. The substitution
// table is just a growable array of node pointers, resolving S_/S0_/S1_ exactly
// as a conforming producer emitted them.
#ifndef MANGLY_PARSER_HPP
#define MANGLY_PARSER_HPP

#include <cstdint>

#include "mangly/nodes.hpp"
#include "mangly/support.hpp"

namespace mangly {

namespace detail {

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

// std:: substitution abbreviations (St, Sa, ...). Expand to a fixed spelling and
// are NOT entered into the substitution table.
inline const char* std_abbrev(char c) {
    switch (c) {
        case 't': return "std";
        case 'a': return "std::allocator";
        case 'b': return "std::basic_string";
        case 's': return "std::string";
        case 'i': return "std::istream";
        case 'o': return "std::ostream";
        case 'd': return "std::iostream";
        default: return nullptr;
    }
}

inline std::uint32_t from_base36(StringView s) {
    std::uint32_t value = 0;
    for (std::uint32_t k = 0; k < s.size; ++k) {
        char ch = s.data[k];
        int d;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (ch >= 'A' && ch <= 'Z') d = ch - 'A' + 10;
        else if (ch >= 'a' && ch <= 'z') d = ch - 'a' + 10;
        else d = 0;
        value = value * 36 + static_cast<std::uint32_t>(d);
    }
    return value;
}

}  // namespace detail

class Parser {
public:
    Parser(const char* s, std::uint32_t n, Arena& arena)
        : s_(s), n_(n), arena_(arena) {}

    // Returns the root node, or nullptr on failure.
    const Node* parse() {
        if (n_ < 2 || s_[0] != '_' || s_[1] != 'Z') {
            return fail("not a _Z mangled name");
        }
        i_ = 2;
        // <special-name> (vtable/typeinfo/thunks/guard) or a normal <encoding>.
        const Node* node;
        if (peek() == 'T') {
            node = parse_special_name();
        } else if (peek() == 'G' && peek2() == 'V') {
            take();  // 'G'
            take();  // 'V'
            const Node* inner = parse_name();  // often a local-name
            node = inner ? new_special(make_sv("GV"), StringView{}, inner) : nullptr;
        } else {
            node = parse_encoding();
        }
        if (!node) return nullptr;
        if (!at_end()) return fail("trailing input");
        return node;
    }

    bool failed() const { return failed_; }
    const char* error() const { return error_; }

private:
    // ------------------------------------------------------------------ cursor
    char peek() const { return i_ < n_ ? s_[i_] : '\0'; }
    char peek2() const { return i_ + 1 < n_ ? s_[i_ + 1] : '\0'; }
    bool at_end() const { return i_ >= n_; }
    char take() { return s_[i_++]; }

    const Node* fail(const char* msg) {
        if (!failed_) {
            failed_ = true;
            error_ = msg;
        }
        return nullptr;
    }

    void expect(char c) {
        if (peek() != c) {
            fail("unexpected character");
            return;
        }
        ++i_;
    }

    const Node* add_sub(const Node* n) {
        if (!n) return fail("out of memory");
        subs_.push(n);
        return n;
    }

    const Node* sub_at(std::uint32_t idx) {
        if (idx >= subs_.size()) return fail("substitution index out of range");
        return subs_[idx];
    }

    // --------------------------------------------------------------- builders
    const Node* new_builtin(StringView code) {
        Node* n = make_node(arena_, Kind::Builtin);
        if (!n) return fail("out of memory");
        n->builtin.code = code;
        return n;
    }
    const Node* new_source(StringView text) {
        Node* n = make_node(arena_, Kind::SourceName);
        if (!n) return fail("out of memory");
        n->source.text = text;
        return n;
    }
    const Node* new_qual(const Node* const* parts, std::uint32_t nparts) {
        Node* n = make_node(arena_, Kind::QualifiedName);
        if (!n) return fail("out of memory");
        n->qual.parts = parts;
        n->qual.nparts = nparts;
        return n;
    }
    const Node* new_qual1(const Node* comp) {
        const Node** arr = arena_.alloc<const Node*>(1);
        if (!arr) return fail("out of memory");
        arr[0] = comp;
        return new_qual(arr, 1);
    }
    const Node* new_tmpl(const Node* name, const Node* const* args,
                         std::uint32_t nargs) {
        Node* n = make_node(arena_, Kind::TemplateId);
        if (!n) return fail("out of memory");
        n->tmpl.name = name;
        n->tmpl.args = args;
        n->tmpl.nargs = nargs;
        return n;
    }
    const Node* new_ref(Kind k, const Node* inner) {
        Node* n = make_node(arena_, k);
        if (!n) return fail("out of memory");
        n->ref.inner = inner;
        return n;
    }
    const Node* new_cv(const Node* inner, StringView quals) {
        Node* n = make_node(arena_, Kind::CVQualified);
        if (!n) return fail("out of memory");
        n->cv.inner = inner;
        n->cv.quals = quals;
        return n;
    }
    const Node* new_operator(StringView code, const Node* type) {
        Node* n = make_node(arena_, Kind::OperatorName);
        if (!n) return fail("out of memory");
        n->oper.code = code;
        n->oper.type = type;
        return n;
    }
    const Node* new_ctordtor(StringView code) {
        Node* n = make_node(arena_, Kind::CtorDtor);
        if (!n) return fail("out of memory");
        n->ctordtor.code = code;
        return n;
    }
    const Node* new_literal(const Node* type, StringView value) {
        Node* n = make_node(arena_, Kind::Literal);
        if (!n) return fail("out of memory");
        n->literal.type = type;
        n->literal.value = value;
        return n;
    }
    const Node* new_function_type(const Node* ret, const Node* const* params,
                                  std::uint32_t nparams) {
        Node* n = make_node(arena_, Kind::FunctionType);
        if (!n) return fail("out of memory");
        n->func_type.ret = ret;
        n->func_type.params = params;
        n->func_type.nparams = nparams;
        return n;
    }
    const Node* new_member_pointer(const Node* cls, const Node* pointee) {
        Node* n = make_node(arena_, Kind::MemberPointer);
        if (!n) return fail("out of memory");
        n->memptr.cls = cls;
        n->memptr.pointee = pointee;
        return n;
    }
    const Node* new_template_param(std::uint32_t index, const Node* resolved) {
        Node* n = make_node(arena_, Kind::TemplateParam);
        if (!n) return fail("out of memory");
        n->tparam.index = index;
        n->tparam.resolved = resolved;
        return n;
    }
    const Node* new_expr(StringView op, const Node* const* operands,
                         std::uint32_t noperands) {
        Node* n = make_node(arena_, Kind::Expression);
        if (!n) return fail("out of memory");
        n->expr.op = op;
        n->expr.operands = operands;
        n->expr.noperands = noperands;
        return n;
    }
    const Node* new_special(StringView code, StringView extra, const Node* inner) {
        Node* n = make_node(arena_, Kind::SpecialName);
        if (!n) return fail("out of memory");
        n->special.code = code;
        n->special.extra = extra;
        n->special.inner = inner;
        return n;
    }
    const Node* new_decltype(const Node* expr, bool id_form) {
        Node* n = make_node(arena_, Kind::Decltype);
        if (!n) return fail("out of memory");
        n->decl_type.expr = expr;
        n->decl_type.id_form = id_form;
        return n;
    }
    const Node* new_local_name(const Node* scope, const Node* entity,
                               StringView disc) {
        Node* n = make_node(arena_, Kind::LocalName);
        if (!n) return fail("out of memory");
        n->local.scope = scope;
        n->local.entity = entity;
        n->local.disc = disc;
        return n;
    }
    const Node* new_closure(const Node* const* params, std::uint32_t nparams,
                            StringView num, bool unnamed) {
        Node* n = make_node(arena_, Kind::Closure);
        if (!n) return fail("out of memory");
        n->closure.params = params;
        n->closure.nparams = nparams;
        n->closure.num = num;
        n->closure.unnamed = unnamed;
        return n;
    }
    const Node* new_funcparam(StringView num) {
        Node* n = make_node(arena_, Kind::FuncParam);
        if (!n) return fail("out of memory");
        n->fparam.num = num;
        return n;
    }
    const Node* new_pack(const Node* const* elems, std::uint32_t nelems) {
        Node* n = make_node(arena_, Kind::Pack);
        if (!n) return fail("out of memory");
        n->pack.elems = elems;
        n->pack.nelems = nelems;
        return n;
    }
    const Node* new_pack_expansion(const Node* pattern) {
        Node* n = make_node(arena_, Kind::PackExpansion);
        if (!n) return fail("out of memory");
        n->pack_exp.pattern = pattern;
        return n;
    }
    const Node* new_abitag(const Node* inner, StringView tag) {
        Node* n = make_node(arena_, Kind::AbiTag);
        if (!n) return fail("out of memory");
        n->abitag.inner = inner;
        n->abitag.tag = tag;
        return n;
    }

    // Copy a scratch pointer list into a stable arena array (nullptr if empty).
    const Node* const* dup(const Vec<const Node*>& v) {
        std::uint32_t count = v.size();
        if (count == 0) return nullptr;
        const Node** arr = arena_.alloc<const Node*>(count);
        if (!arr) {
            fail("out of memory");
            return nullptr;
        }
        std::memcpy(arr, v.data(), sizeof(const Node*) * count);
        return arr;
    }

    // ---------------------------------------------------------- special names
    // TV/TI/TS/TT <type>, or a thunk (Th/Tv/Tc) wrapping a base <encoding>.
    const Node* parse_special_name() {
        take();          // 'T'
        char c = take();  // V/I/S/T (type) or h/v/c (thunk)
        if (c == 'V' || c == 'I' || c == 'S' || c == 'T') {
            StringView code{s_ + i_ - 2, 2};
            const Node* t = parse_type();
            if (!t) return nullptr;
            return new_special(code, StringView{}, t);
        }
        if (c == 'h' || c == 'v' || c == 'c') {
            StringView code{s_ + i_ - 2, 2};
            std::uint32_t estart = i_;
            if (c == 'h') {
                if (!read_offset()) return nullptr;
            } else if (c == 'v') {
                if (!read_offset() || !read_offset()) return nullptr;
            } else {  // covariant: two call-offsets
                if (!read_call_offset() || !read_call_offset()) return nullptr;
            }
            StringView extra{s_ + estart, i_ - estart};
            const Node* base = parse_encoding();
            if (!base) return nullptr;
            return new_special(code, extra, base);
        }
        return fail("unsupported special-name");
    }

    // <offset> _ : optional 'n' (negative) then digits then '_'.
    bool read_offset() {
        if (peek() == 'n') take();
        if (!detail::is_digit(peek())) {
            fail("bad thunk offset");
            return false;
        }
        while (detail::is_digit(peek())) take();
        if (peek() != '_') {
            fail("expected '_' in thunk offset");
            return false;
        }
        take();
        return true;
    }

    // <call-offset> ::= h <offset> _ | v <offset> _ <offset> _
    bool read_call_offset() {
        char t = peek();
        if (t == 'h') {
            take();
            return read_offset();
        }
        if (t == 'v') {
            take();
            return read_offset() && read_offset();
        }
        fail("bad call-offset");
        return false;
    }

    // <local-name> ::= Z <encoding> E <entity> [<discriminator>]
    //               |  Z <encoding> E s [<discriminator>]   (string literal)
    const Node* parse_local_name() {
        take();  // 'Z'
        const Node* const* saved_targs = cur_targs_;
        std::uint32_t saved_ntargs = cur_ntargs_;
        const Node* scope = parse_encoding(/*stop_at_e=*/true);
        cur_targs_ = saved_targs;  // the scope must not leak template context
        cur_ntargs_ = saved_ntargs;
        if (!scope) return nullptr;
        expect('E');
        if (failed_) return nullptr;
        pending_cv_ = StringView{};  // the entity's own cv belongs to the outer fn
        pending_ref_ = 0;
        const Node* entity;
        if (peek() == 's') {  // string literal
            take();
            entity = new_source(make_sv("string literal"));
        } else {
            entity = parse_name();
        }
        if (!entity) return nullptr;
        StringView disc{};
        if (peek() == '_') {
            std::uint32_t start = i_;
            take();
            if (peek() == '_') {  // __<number>_
                take();
                while (peek() != '_') {
                    if (at_end()) return fail("bad discriminator");
                    take();
                }
                take();
            } else {
                while (detail::is_digit(peek())) take();
            }
            disc = StringView{s_ + start, i_ - start};
        }
        return new_local_name(scope, entity, disc);
    }

    // <closure-type> ::= Ul <param-types> E [<number>] _  (lambda)
    // <unnamed-type> ::= Ut [<number>] _
    const Node* parse_closure() {
        take();  // 'U'
        char t = take();
        if (t == 'l') {
            Vec<const Node*> params;
            while (peek() != 'E') {
                if (at_end()) return fail("unterminated lambda");
                const Node* p = parse_type();
                if (!p) return nullptr;
                params.push(p);
            }
            expect('E');
            if (failed_ || params.failed()) return nullptr;
            std::uint32_t ns = i_;
            while (peek() != '_') {
                if (at_end()) return fail("unterminated closure");
                take();
            }
            StringView num{s_ + ns, i_ - ns};
            take();  // '_'
            return new_closure(dup(params), params.size(), num, /*unnamed=*/false);
        }
        if (t == 't') {
            std::uint32_t ns = i_;
            while (peek() != '_') {
                if (at_end()) return fail("unterminated unnamed type");
                take();
            }
            StringView num{s_ + ns, i_ - ns};
            take();  // '_'
            return new_closure(nullptr, 0, num, /*unnamed=*/true);
        }
        return fail("unsupported U-name");
    }

    // ---------------------------------------------------------------- encoding
    const Node* parse_encoding(bool stop_at_e = false) {
        pending_cv_ = StringView{};
        pending_ref_ = 0;
        const Node* name = parse_name();
        if (!name) return nullptr;
        // The nested-name's leading cv/ref-qualifiers (member-fn `this`) are
        // captured now, before parsing param types clobbers the pending slots.
        StringView this_quals = pending_cv_;
        char ref_qual = pending_ref_;
        // A data name has no bare-function-type: input ends, or (in a local-name
        // scope) the terminating 'E' follows.
        if (at_end() || (stop_at_e && peek() == 'E')) return name;

        // A function template's signature (return type + params) references its
        // template arguments via T_/T<n>_; expose them so those resolve.
        if (name->kind == Kind::TemplateId) {
            cur_targs_ = name->tmpl.args;
            cur_ntargs_ = name->tmpl.nargs;
        }

        const Node* ret = nullptr;
        if (name->kind == Kind::TemplateId) {
            ret = parse_type();  // template functions encode a leading return type
            if (!ret) return nullptr;
        }

        Vec<const Node*> params;
        while (!at_end() && !(stop_at_e && peek() == 'E')) {
            const Node* p = parse_type();
            if (!p) return nullptr;
            params.push(p);
        }
        if (params.failed()) return fail("out of memory");

        // A lone 'void' parameter denotes an empty parameter list: f(void).
        bool lone_void = params.size() == 1 &&
                         params[0]->kind == Kind::Builtin &&
                         params[0]->builtin.code.size == 1 &&
                         params[0]->builtin.code.data[0] == 'v';

        Node* fn = make_node(arena_, Kind::Function);
        if (!fn) return fail("out of memory");
        fn->func.name = name;
        fn->func.ret = ret;
        fn->func.this_quals = this_quals;
        fn->func.ref_qual = ref_qual;
        if (lone_void || params.size() == 0) {
            fn->func.params = nullptr;
            fn->func.nparams = 0;
        } else {
            const Node* const* arr = dup(params);
            if (!arr) return nullptr;
            fn->func.params = arr;
            fn->func.nparams = params.size();
        }
        return fn;
    }

    // -------------------------------------------------------------------- names
    const Node* parse_name() {
        char c = peek();
        if (c == 'N') return parse_nested_name(/*as_type=*/false);
        if (c == 'Z') return parse_local_name();
        if (c == 'S') {
            const Node* base = parse_substitution();
            if (!base) return nullptr;
            if (peek() == 'I') return template_wrap(base);
            return base;
        }
        const Node* base = parse_unqualified_name();
        if (!base) return nullptr;
        const Node* qn = new_qual1(base);
        if (!qn) return nullptr;
        // At encoding level neither an unscoped name nor an unscoped-template
        // *name* is a substitution; for a function template only the template-id
        // is (added by template_wrap), matching g++ (S_ == the template-id).
        if (peek() == 'I') return template_wrap(qn);
        return qn;
    }

    // Wrap `base` in a template-id from the args starting at 'I', registering it.
    const Node* template_wrap(const Node* base) {
        std::uint32_t nargs = 0;
        const Node* const* args = parse_template_args(nargs);
        if (failed_) return nullptr;
        return add_sub(new_tmpl(base, args, nargs));
    }

    const Node* parse_nested_name(bool as_type) {
        expect('N');
        if (failed_) return nullptr;
        std::uint32_t cv_start = i_;
        while (peek() == 'r' || peek() == 'V' || peek() == 'K') take();  // cv
        StringView local_cv{s_ + cv_start, i_ - cv_start};
        char local_ref = 0;
        if (peek() == 'R' || peek() == 'O') local_ref = take();          // ref
        const Node* sofar = nullptr;
        for (;;) {
            char c = peek();
            if (at_end()) return fail("unterminated nested-name");
            if (c == 'E') {
                take();
                break;
            }
            if (c == 'I') {
                if (!sofar) return fail("template-id without prefix");
                std::uint32_t nargs = 0;
                const Node* const* args = parse_template_args(nargs);
                if (failed_) return nullptr;
                sofar = add_sub(new_tmpl(sofar, args, nargs));
                if (!sofar) return nullptr;
                continue;
            }
            if (c == 'S') {
                sofar = parse_substitution();  // a substitution restarts the chain
                if (!sofar) return nullptr;
                continue;
            }
            const Node* comp = parse_unqualified_name();
            if (!comp) return nullptr;
            if (!sofar) {
                sofar = new_qual1(comp);
            } else if (sofar->kind == Kind::QualifiedName) {
                std::uint32_t m = sofar->qual.nparts;
                const Node** arr = arena_.alloc<const Node*>(m + 1);
                if (!arr) return fail("out of memory");
                std::memcpy(arr, sofar->qual.parts, sizeof(const Node*) * m);
                arr[m] = comp;
                sofar = new_qual(arr, m + 1);
            } else {
                const Node** arr = arena_.alloc<const Node*>(2);
                if (!arr) return fail("out of memory");
                arr[0] = sofar;
                arr[1] = comp;
                sofar = new_qual(arr, 2);
            }
            // The final component of a nested-name is a substitution only when
            // the name is a class-enum type or a template-prefix (peek=='I'/next
            // component). A bare function/data name is never entered.
            if (as_type || peek() != 'E') {
                if (!add_sub(sofar)) return nullptr;
            }
        }
        if (!sofar) return fail("empty nested-name");
        pending_cv_ = local_cv;    // publish only on success (after nested calls)
        pending_ref_ = local_ref;
        return sofar;
    }

    const Node* parse_unqualified_name() {
        char c = peek();
        if (detail::is_digit(c)) return parse_source_name();
        // constructor / destructor: C1/C2/C3, D0/D1/D2 (2-char forms).
        if ((c == 'C' || c == 'D') && detail::is_digit(peek2())) {
            StringView code{s_ + i_, 2};
            i_ += 2;
            return new_ctordtor(code);
        }
        if (c == 'U') return parse_closure();  // lambda / unnamed type
        // operator names (incl. "cv <type>" and "li <source-name>").
        if (is_operator_code(c, peek2())) return parse_operator_name();
        return fail("unsupported unqualified-name");
    }

    const Node* parse_operator_name() {
        StringView code{s_ + i_, 2};
        char a = take();
        char b = take();
        if (a == 'c' && b == 'v') {  // conversion operator: cv <type>
            const Node* t = parse_type();
            if (!t) return nullptr;
            return new_operator(code, t);
        }
        if (a == 'l' && b == 'i') {  // literal operator: li <source-name>
            const Node* nm = parse_source_name();
            if (!nm) return nullptr;
            return new_operator(code, nm);
        }
        return new_operator(code, nullptr);
    }

    const Node* parse_source_name() {
        if (!detail::is_digit(peek())) return fail("expected source-name length");
        std::size_t len = 0;
        while (detail::is_digit(peek())) {
            len = len * 10 + static_cast<std::size_t>(take() - '0');
        }
        if (static_cast<std::size_t>(i_) + len > n_) {
            return fail("source-name length overruns input");
        }
        StringView t{s_ + i_, static_cast<std::uint32_t>(len)};
        i_ += static_cast<std::uint32_t>(len);
        const Node* node = new_source(t);
        // trailing abi-tags: B <source-name>
        while (node && peek() == 'B') {
            take();
            if (!detail::is_digit(peek())) return fail("expected abi-tag length");
            std::size_t tl = 0;
            while (detail::is_digit(peek())) {
                tl = tl * 10 + static_cast<std::size_t>(take() - '0');
            }
            if (static_cast<std::size_t>(i_) + tl > n_) return fail("abi-tag overruns");
            StringView tag{s_ + i_, static_cast<std::uint32_t>(tl)};
            i_ += static_cast<std::uint32_t>(tl);
            node = new_abitag(node, tag);
        }
        return node;
    }

    // -------------------------------------------------------------- templates
    const Node* const* parse_template_args(std::uint32_t& count) {
        count = 0;
        expect('I');
        if (failed_) return nullptr;
        Vec<const Node*> args;
        while (peek() != 'E') {
            if (at_end()) {
                fail("unterminated template-args");
                return nullptr;
            }
            const Node* a = parse_template_arg();
            if (!a) return nullptr;
            args.push(a);
        }
        expect('E');
        if (failed_ || args.failed()) return nullptr;
        count = args.size();
        return dup(args);
    }

    // A template argument is a type, an <expr-primary> literal (L..E), or an
    // expression (X <expression> E).
    const Node* parse_template_arg() {
        if (peek() == 'L') return parse_literal();
        if (peek() == 'X') {
            take();
            const Node* e = parse_expression();
            if (!e) return nullptr;
            expect('E');
            return failed_ ? nullptr : e;
        }
        if (peek() == 'J') {  // argument pack: J <template-arg>* E
            take();
            Vec<const Node*> elems;
            while (peek() != 'E') {
                if (at_end()) return fail("unterminated argument pack");
                const Node* e = parse_template_arg();
                if (!e) return nullptr;
                elems.push(e);
            }
            expect('E');
            if (failed_ || elems.failed()) return nullptr;
            return new_pack(dup(elems), elems.size());
        }
        return parse_type();
    }

    const Node* parse_literal() {
        expect('L');
        if (failed_) return nullptr;
        const Node* t = parse_type();
        if (!t) return nullptr;
        std::uint32_t start = i_;
        while (peek() != 'E') {
            if (at_end()) return fail("unterminated literal");
            take();
        }
        StringView value{s_ + start, i_ - start};
        expect('E');
        if (failed_) return nullptr;
        return new_literal(t, value);  // literals are not substitutable
    }

    // ------------------------------------------------------------------- types
    const Node* parse_type() {
        char c = peek();
        if (c == 'N') return parse_nested_name(/*as_type=*/true);
        if (c == 'Z') return add_sub(parse_local_name());  // local-name as a type
        if (c == 'S') {
            const Node* base = parse_substitution();
            if (!base) return nullptr;
            if (peek() == 'I') return template_wrap(base);
            return base;
        }
        if (c == 'D' && (peek2() == 't' || peek2() == 'T')) {
            return parse_decltype();
        }
        if (c == 'D' && peek2() == 'p') {  // pack expansion
            i_ += 2;
            const Node* pattern = parse_type();
            if (!pattern) return nullptr;
            return add_sub(new_pack_expansion(pattern));
        }
        if (c == 'D' && i_ + 1 < n_ && is_builtin_d_code(s_[i_ + 1])) {
            StringView code{s_ + i_, 2};
            i_ += 2;
            return new_builtin(code);  // builtins are never substitutions
        }
        if (is_builtin_code(c)) {
            StringView code{s_ + i_, 1};
            take();
            return new_builtin(code);
        }
        if (c == 'P') {
            take();
            const Node* inner = parse_type();
            if (!inner) return nullptr;
            return add_sub(new_ref(Kind::Pointer, inner));
        }
        if (c == 'R') {
            take();
            const Node* inner = parse_type();
            if (!inner) return nullptr;
            return add_sub(new_ref(Kind::LValueRef, inner));
        }
        if (c == 'O') {
            take();
            const Node* inner = parse_type();
            if (!inner) return nullptr;
            return add_sub(new_ref(Kind::RValueRef, inner));
        }
        if (c == 'A') return parse_array();
        if (c == 'T') return parse_template_param();
        if (c == 'F') return parse_function_type();
        if (c == 'M') {
            take();
            const Node* cls = parse_type();
            if (!cls) return nullptr;
            const Node* pointee = parse_type();
            if (!pointee) return nullptr;
            return add_sub(new_member_pointer(cls, pointee));
        }
        if (c == 'K' || c == 'V' || c == 'r') {
            std::uint32_t start = i_;
            while (peek() == 'K' || peek() == 'V' || peek() == 'r') take();
            StringView quals{s_ + start, i_ - start};
            const Node* inner = parse_type();
            if (!inner) return nullptr;
            return add_sub(new_cv(inner, quals));
        }
        if (detail::is_digit(c)) {
            const Node* sn = parse_source_name();
            if (!sn) return nullptr;
            const Node* qn = new_qual1(sn);
            if (!add_sub(qn)) return nullptr;
            if (peek() == 'I') return template_wrap(qn);
            return qn;
        }
        return fail("unsupported type");
    }

    const Node* parse_array() {
        expect('A');
        if (failed_) return nullptr;
        bool has_dim = false;
        StringView dim{};
        if (peek() == '_') {
            take();
        } else if (detail::is_digit(peek())) {
            std::uint32_t start = i_;
            while (detail::is_digit(peek())) take();
            dim = StringView{s_ + start, i_ - start};
            has_dim = true;
            expect('_');
            if (failed_) return nullptr;
        } else {  // a dimension expression; retained verbatim as the bound
            std::uint32_t start = i_;
            while (peek() != '_' && !at_end()) take();
            dim = StringView{s_ + start, i_ - start};
            has_dim = true;
            expect('_');
            if (failed_) return nullptr;
        }
        bool elem_is_sub = peek() == 'S';
        const Node* inner = parse_type();
        if (!inner) return nullptr;
        Node* n = make_node(arena_, Kind::Array);
        if (!n) return fail("out of memory");
        n->array.inner = inner;
        n->array.dim = dim;
        n->array.has_dim = has_dim;
        n->array.elem_is_sub = elem_is_sub;
        return add_sub(n);
    }

    // <decltype> ::= Dt <expression> E   (id-expression)
    //             |  DT <expression> E   (expression)
    const Node* parse_decltype() {
        take();  // 'D'
        bool id_form = take() == 't';  // 't' -> Dt, 'T' -> DT
        const Node* e = parse_expression();
        if (!e) return nullptr;
        expect('E');
        if (failed_) return nullptr;
        return add_sub(new_decltype(e, id_form));  // decltype is a type: substitutable
    }

    const Node* parse_function_type() {
        expect('F');
        if (failed_) return nullptr;
        if (peek() == 'Y') take();  // extern "C" flag (ignored, rare)
        const Node* ret = parse_type();
        if (!ret) return nullptr;
        Vec<const Node*> params;
        while (peek() != 'E') {
            if (at_end()) return fail("unterminated function type");
            const Node* p = parse_type();
            if (!p) return nullptr;
            params.push(p);
        }
        expect('E');
        if (failed_ || params.failed()) return nullptr;
        bool lone_void = params.size() == 1 &&
                         params[0]->kind == Kind::Builtin &&
                         params[0]->builtin.code.size == 1 &&
                         params[0]->builtin.code.data[0] == 'v';
        const Node* const* arr = nullptr;
        std::uint32_t np = 0;
        if (!lone_void && params.size() > 0) {
            arr = dup(params);
            if (!arr) return nullptr;
            np = params.size();
        }
        return add_sub(new_function_type(ret, arr, np));
    }

    // <template-param> ::= T_ | T <number> _   (T_ == arg 0, T0_ == arg 1, ...)
    // Substitutable; resolved against the enclosing template's args for rendering.
    const Node* parse_template_param() {
        expect('T');
        if (failed_) return nullptr;
        std::uint32_t index = 0;
        if (peek() == '_') {
            take();
        } else {
            std::uint32_t start = i_;
            while (peek() != '_') {
                if (at_end()) return fail("unterminated template-param");
                take();
            }
            index = detail::from_base36(StringView{s_ + start, i_ - start}) + 1;
            take();  // consume '_'
        }
        const Node* resolved =
            index < cur_ntargs_ ? cur_targs_[index] : nullptr;
        return add_sub(new_template_param(index, resolved));
    }

    // <expression>: the common dependent forms. Operands may be types or
    // sub-expressions. Anything unrecognized fails cleanly (no assumptions).
    const Node* parse_expression() {
        char c = peek();
        if (c == 'L') return parse_literal();
        if (c == 'T') return parse_template_param();
        if (c == 'f' && peek2() == 'p') {  // function parameter: fp_ / fp<n>_
            i_ += 2;
            std::uint32_t ns = i_;
            while (peek() != '_') {
                if (at_end()) return fail("unterminated function param");
                take();
            }
            StringView num{s_ + ns, i_ - ns};
            take();  // '_'
            return new_funcparam(num);
        }
        if (c == 's' && peek2() == 't') return parse_expr_op("st", 1, /*type0=*/true);
        if (c == 's' && peek2() == 'z') return parse_expr_op("sz", 1, false);
        if (c == 'a' && peek2() == 't') return parse_expr_op("at", 1, true);
        if (c == 'a' && peek2() == 'z') return parse_expr_op("az", 1, false);
        if (c == 't' && peek2() == 'l') return parse_expr_list("tl", /*type0=*/true);
        if (c == 'i' && peek2() == 'l') return parse_expr_list("il", false);
        if (c == 'c' && peek2() == 'l') return parse_expr_list("cl", false);
        if (c == 's' && peek2() == 'Z') return parse_expr_op("sZ", 1, false);
        if (c == 'c' && peek2() == 'v') return parse_expr_op("cv", 2, /*type0=*/true);
        if ((c == 'd' || c == 'p') && peek2() == 't') {  // member access . / ->
            StringView op{s_ + i_, 2};
            i_ += 2;
            const Node* obj = parse_expression();
            if (!obj) return nullptr;
            const Node* mem = parse_source_name();  // unresolved-name (common case)
            if (!mem) return nullptr;
            const Node** arr = arena_.alloc<const Node*>(2);
            if (!arr) return fail("out of memory");
            arr[0] = obj;
            arr[1] = mem;
            return new_expr(op, arr, 2);
        }
        // operators (arithmetic/logical/comparison/...): fixed arity.
        int arity = expr_operator_arity(c, peek2());
        if (arity > 0) {
            StringView op{s_ + i_, 2};
            i_ += 2;
            Vec<const Node*> ops;
            for (int k = 0; k < arity; ++k) {
                const Node* e = parse_expression();
                if (!e) return nullptr;
                ops.push(e);
            }
            if (ops.failed()) return fail("out of memory");
            return new_expr(op, dup(ops), ops.size());
        }
        // Fallback: a bare <type> can appear as an operand (e.g. under a cast).
        return parse_type();
    }

    // Fixed-arity operator: 1 leading type or expression operand.
    const Node* parse_expr_op(const char* code, int n, bool type0) {
        StringView op{s_ + i_, 2};
        i_ += 2;
        (void)code;
        Vec<const Node*> ops;
        const Node* first = type0 ? parse_type() : parse_expression();
        if (!first) return nullptr;
        ops.push(first);
        for (int k = 1; k < n; ++k) {
            const Node* e = parse_expression();
            if (!e) return nullptr;
            ops.push(e);
        }
        if (ops.failed()) return fail("out of memory");
        return new_expr(op, dup(ops), ops.size());
    }

    // <op> [<type>] <expression>* E   (tl/il/cl and similar list forms).
    const Node* parse_expr_list(const char* code, bool type0) {
        StringView op{s_ + i_, 2};
        i_ += 2;
        (void)code;
        Vec<const Node*> ops;
        if (type0) {
            const Node* t = parse_type();
            if (!t) return nullptr;
            ops.push(t);
        }
        while (peek() != 'E') {
            if (at_end()) return fail("unterminated expression list");
            const Node* e = parse_expression();
            if (!e) return nullptr;
            ops.push(e);
        }
        expect('E');
        if (failed_ || ops.failed()) return nullptr;
        return new_expr(op, dup(ops), ops.size());
    }

    const Node* parse_substitution() {
        expect('S');
        if (failed_) return nullptr;
        char c = peek();
        if (c == '_') {
            take();
            return sub_at(0);
        }
        if (const char* ab = detail::std_abbrev(c)) {
            take();
            return new_source(make_sv(ab));  // not a table entry
        }
        std::uint32_t start = i_;
        while (peek() != '_') {
            if (at_end()) return fail("unterminated substitution");
            take();
        }
        StringView seq{s_ + start, i_ - start};
        take();  // consume '_'
        return sub_at(detail::from_base36(seq) + 1);
    }

    const char* s_;
    std::uint32_t n_;
    std::uint32_t i_ = 0;
    Arena& arena_;
    Vec<const Node*> subs_;
    bool failed_ = false;
    const char* error_ = nullptr;
    // Scratch for the current nested-name's this-qualifiers, published on the
    // successful return of parse_nested_name and read by parse_encoding.
    StringView pending_cv_{};
    char pending_ref_ = 0;
    // The enclosing function template's arguments, so T_/T<n>_ in the signature
    // resolve to concrete types for rendering.
    const Node* const* cur_targs_ = nullptr;
    std::uint32_t cur_ntargs_ = 0;
};

// Convenience: parse `mangled` (length `len`) into `arena`.
inline const Node* parse(const char* mangled, std::uint32_t len, Arena& arena) {
    return Parser(mangled, len, arena).parse();
}
inline const Node* parse(const char* mangled, Arena& arena) {
    return parse(mangled, static_cast<std::uint32_t>(std::strlen(mangled)), arena);
}

}  // namespace mangly

#endif  // MANGLY_PARSER_HPP
