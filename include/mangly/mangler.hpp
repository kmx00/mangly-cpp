// Canonical Itanium C++ ABI mangler.
//
// Walks the AST and emits into an OutputBuffer, greedily substituting per the
// ABI (longest already-seen component first). "Already seen" is decided by
// structurally_equal against a small growable table of node pointers - no key
// strings, no hash map, no exceptions. The result is canonical: for an AST from
// mangly::parse, the re-mangling parses back to a structurally identical AST.
#ifndef MANGLY_MANGLER_HPP
#define MANGLY_MANGLER_HPP

#include <cstdint>

#include "mangly/nodes.hpp"
#include "mangly/support.hpp"

namespace mangly {

class Mangler {
public:
    explicit Mangler(OutputBuffer& out) : out_(out) {}

    // Returns true on success; false if the AST is unmanglable or on OOM.
    bool mangle(const Node* node) {
        out_.append("_Z");
        mangle_body(node);
        return !failed_ && !out_.failed();
    }

private:
    void fail() { failed_ = true; }

    // Mangle an <encoding> or <special-name> body (no "_Z" prefix).
    void mangle_body(const Node* node) {
        if (node->kind == Kind::Function) {
            mangle_function(node);
        } else if (node->kind == Kind::SpecialName) {
            mangle_special(node);
        } else {
            mangle_name(node);
        }
    }

    void mangle_special(const Node* n) {
        out_.append(n->special.code);
        char k = n->special.code.size == 2 ? n->special.code.data[1] : '\0';
        if (k == 'h' || k == 'v' || k == 'c') {  // thunk: offset spec + base
            out_.append(n->special.extra);
            mangle_body(n->special.inner);
        } else {  // TV/TI/TS/TT: a type
            mangle_type(n->special.inner);
        }
    }

    void append_base36(std::uint32_t v) {
        static const char* digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        if (v == 0) {
            out_.push('0');
            return;
        }
        char tmp[7];
        int i = 0;
        while (v) {
            tmp[i++] = digits[v % 36];
            v /= 36;
        }
        while (i) out_.push(tmp[--i]);
    }

    void sub_ref(std::uint32_t pos) {
        out_.push('S');
        if (pos != 0) append_base36(pos - 1);
        out_.push('_');
    }

    bool try_sub(const Node* node) {
        for (std::uint32_t i = 0; i < seen_.size(); ++i) {
            if (structurally_equal(seen_[i], node)) {
                sub_ref(i);
                return true;
            }
        }
        return false;
    }

    void add_sub(const Node* node) {
        for (std::uint32_t i = 0; i < seen_.size(); ++i) {
            if (structurally_equal(seen_[i], node)) return;  // dedup
        }
        seen_.push(node);
    }

    // ------------------------------------------------------------------ entry
    void mangle_function(const Node* fn) {
        mangle_name(fn->func.name, fn->func.this_quals, fn->func.ref_qual);
        if (fn->func.ret) mangle_type(fn->func.ret);
        if (fn->func.nparams > 0) {
            for (std::uint32_t i = 0; i < fn->func.nparams; ++i) {
                mangle_type(fn->func.params[i]);
            }
        } else {
            out_.push('v');  // f(void)
        }
    }

    // ------------------------------------------------------------------ names
    // <name> at encoding level: an unscoped single-component name (and a
    // top-level template-id) is emitted bare; only a genuinely nested name
    // (2+ prefix components) is wrapped in N...E.
    void mangle_name(const Node* node, StringView this_quals = StringView{},
                     char ref_qual = 0) {
        if (node->kind == Kind::QualifiedName && node->qual.nparts == 1) {
            mangle_prefix_part(node->qual.parts[0]);  // bare, not a substitution
            return;
        }
        if (node->kind == Kind::TemplateId &&
            node->tmpl.name->kind == Kind::QualifiedName &&
            node->tmpl.name->qual.nparts == 1) {
            mangle_template_id(node, /*sub_name=*/false);  // no N...E, name not subbed
            return;
        }
        out_.push('N');
        out_.append(this_quals);                 // member-fn cv (e.g. "K")
        if (ref_qual) out_.push(ref_qual);       // ref-qualifier
        if (node->kind == Kind::QualifiedName) {
            mangle_name_prefix(node->qual.parts, node->qual.nparts);
        } else {
            mangle_name_body(node);  // template-id: registers template-prefix + id
        }
        out_.push('E');
    }

    void mangle_name_body(const Node* node) {
        if (failed_) return;
        if (node->kind == Kind::TemplateId) {
            mangle_template_id(node);
        } else if (node->kind == Kind::QualifiedName) {
            mangle_prefix(node->qual.parts, node->qual.nparts);
        } else {
            fail();
        }
    }

    void mangle_prefix(const Node* const* parts, std::uint32_t n) {
        if (failed_) return;
        // Materialize a stable QN for the prefix so it can enter the sub table.
        Node* qn = scratch_.alloc<Node>();
        if (!qn) {
            fail();
            return;
        }
        qn->kind = Kind::QualifiedName;
        qn->qual.parts = parts;
        qn->qual.nparts = n;
        if (try_sub(qn)) return;
        if (n == 1) {
            mangle_prefix_part(parts[0]);
        } else {
            mangle_prefix(parts, n - 1);
            mangle_prefix_part(parts[n - 1]);
        }
        add_sub(qn);
    }

    // Like mangle_prefix but does NOT register the full name as a substitution:
    // a bare function/data name is not substitutable (only its inner prefixes).
    void mangle_name_prefix(const Node* const* parts, std::uint32_t n) {
        if (failed_) return;
        if (n == 1) {
            mangle_prefix_part(parts[0]);
            return;
        }
        mangle_prefix(parts, n - 1);        // inner prefixes are substitutable
        mangle_prefix_part(parts[n - 1]);   // final component is not
    }

    void mangle_prefix_part(const Node* part) {
        if (failed_) return;
        switch (part->kind) {
            case Kind::SourceName:
                out_.append_uint(part->source.text.size);
                out_.append(part->source.text);
                return;
            case Kind::TemplateId:
                mangle_template_id(part);
                return;
            case Kind::OperatorName:
                out_.append(part->oper.code);
                if (part->oper.type) {  // "cv" type / "li" source-name operand
                    if (part->oper.code.size == 2 && part->oper.code.data[0] == 'l' &&
                        part->oper.code.data[1] == 'i') {
                        out_.append_uint(part->oper.type->source.text.size);
                        out_.append(part->oper.type->source.text);
                    } else {
                        mangle_type(part->oper.type);
                    }
                }
                return;
            case Kind::CtorDtor:
                out_.append(part->ctordtor.code);
                return;
            default:
                fail();
                return;
        }
    }

    void mangle_template_id(const Node* t, bool sub_name = true) {
        if (failed_) return;
        if (try_sub(t)) return;
        const Node* nm = t->tmpl.name;
        if (!sub_name && nm->kind == Kind::QualifiedName && nm->qual.nparts == 1) {
            // Top-level function-template name: bare, not a substitution.
            mangle_prefix_part(nm->qual.parts[0]);
        } else {
            mangle_name_body(nm);  // registers prefix / template-prefix / name
        }
        out_.push('I');
        for (std::uint32_t i = 0; i < t->tmpl.nargs; ++i) {
            mangle_type(t->tmpl.args[i]);
        }
        out_.push('E');
        add_sub(t);
    }

    // ------------------------------------------------------------------ types
    void mangle_type(const Node* node) {
        if (failed_) return;
        if (node->kind == Kind::Builtin) {
            out_.append(node->builtin.code);
            return;
        }
        if (node->kind == Kind::Literal) {  // not substitutable
            out_.push('L');
            mangle_type(node->literal.type);
            out_.append(node->literal.value);
            out_.push('E');
            return;
        }
        if (node->kind == Kind::Expression) {  // template-arg expression: X..E
            out_.push('X');
            mangle_expression(node);
            out_.push('E');
            return;
        }
        if (try_sub(node)) return;
        switch (node->kind) {
            case Kind::QualifiedName:
                // Unscoped single-component class type -> bare `<len><name>`;
                // a genuinely nested class type -> N...E.
                if (node->qual.nparts == 1) {
                    mangle_prefix(node->qual.parts, 1);
                } else {
                    out_.push('N');
                    mangle_prefix(node->qual.parts, node->qual.nparts);
                    out_.push('E');
                }
                return;
            case Kind::TemplateId:
                // Unscoped template-id -> `<name><args>` bare; nested -> N...E.
                if (node->tmpl.name->kind == Kind::QualifiedName &&
                    node->tmpl.name->qual.nparts == 1) {
                    mangle_template_id(node);
                } else {
                    out_.push('N');
                    mangle_template_id(node);
                    out_.push('E');
                }
                return;
            case Kind::Pointer:
                out_.push('P');
                mangle_type(node->ref.inner);
                add_sub(node);
                return;
            case Kind::LValueRef:
                out_.push('R');
                mangle_type(node->ref.inner);
                add_sub(node);
                return;
            case Kind::RValueRef:
                out_.push('O');
                mangle_type(node->ref.inner);
                add_sub(node);
                return;
            case Kind::Array:
                out_.push('A');
                if (node->array.has_dim) out_.append(node->array.dim);
                out_.push('_');
                mangle_type(node->array.inner);
                add_sub(node);
                return;
            case Kind::CVQualified:
                out_.append(node->cv.quals);
                mangle_type(node->cv.inner);
                add_sub(node);
                return;
            case Kind::FunctionType:
                out_.push('F');
                mangle_type(node->func_type.ret);
                if (node->func_type.nparams > 0) {
                    for (std::uint32_t i = 0; i < node->func_type.nparams; ++i) {
                        mangle_type(node->func_type.params[i]);
                    }
                } else {
                    out_.push('v');
                }
                out_.push('E');
                add_sub(node);
                return;
            case Kind::MemberPointer:
                out_.push('M');
                mangle_type(node->memptr.cls);
                mangle_type(node->memptr.pointee);
                add_sub(node);
                return;
            case Kind::TemplateParam:
                out_.push('T');
                if (node->tparam.index) append_base36(node->tparam.index - 1);
                out_.push('_');
                add_sub(node);  // template-params are substitutable
                return;
            case Kind::Decltype:
                out_.append(node->decl_type.id_form ? "Dt" : "DT");
                mangle_operand(node->decl_type.expr);
                out_.push('E');
                add_sub(node);
                return;
            default:
                fail();
                return;
        }
    }

    // Emit an expression body (no surrounding X..E). Operators/st/sz are fixed
    // arity; tl/il/cl are E-terminated.
    void mangle_expression(const Node* e) {
        if (failed_) return;
        out_.append(e->expr.op);
        for (std::uint32_t i = 0; i < e->expr.noperands; ++i) {
            mangle_operand(e->expr.operands[i]);
        }
        StringView op = e->expr.op;
        bool e_terminated =
            op.size == 2 && ((op.data[0] == 't' && op.data[1] == 'l') ||
                             (op.data[0] == 'i' && op.data[1] == 'l') ||
                             (op.data[0] == 'c' && op.data[1] == 'l'));
        if (e_terminated) out_.push('E');
    }

    void mangle_operand(const Node* n) {
        if (n->kind == Kind::Expression) {
            mangle_expression(n);
        } else {
            mangle_type(n);
        }
    }

    OutputBuffer& out_;
    Arena scratch_;
    Vec<const Node*> seen_;
    bool failed_ = false;
};

// Mangle AST `node` into `out`; returns true on success.
inline bool mangle(const Node* node, OutputBuffer& out) {
    return Mangler(out).mangle(node);
}

}  // namespace mangly

#endif  // MANGLY_MANGLER_HPP
