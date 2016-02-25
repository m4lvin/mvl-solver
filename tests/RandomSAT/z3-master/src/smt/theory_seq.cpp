/*++
Copyright (c) 2015 Microsoft Corporation

Module Name:

    theory_seq.h

Abstract:

    Native theory solver for sequences.

Author:

    Nikolaj Bjorner (nbjorner) 2015-6-12

Revision History:

    // Use instead reference counts for dependencies to GC?

--*/

#include "value_factory.h"
#include "smt_context.h"
#include "smt_model_generator.h"
#include "theory_seq.h"
#include "ast_trail.h"
#include "theory_arith.h"

using namespace smt;

struct display_expr {
    ast_manager& m;
    display_expr(ast_manager& m): m(m) {}
    std::ostream& display(std::ostream& out, sym_expr* e) const {
        return e->display(out);
    }
};



void theory_seq::solution_map::update(expr* e, expr* r, dependency* d) {
    if (e == r) {
        return;
    }
    m_cache.reset();
    std::pair<expr*, dependency*> value;
    if (m_map.find(e, value)) {
        add_trail(DEL, e, value.first, value.second);
    }
    value.first = r;
    value.second = d;
    m_map.insert(e, value);
    add_trail(INS, e, r, d);
}

void theory_seq::solution_map::add_trail(map_update op, expr* l, expr* r, dependency* d) {
    m_updates.push_back(op);
    m_lhs.push_back(l);
    m_rhs.push_back(r);
    m_deps.push_back(d);
}

bool theory_seq::solution_map::is_root(expr* e) const {
    return !m_map.contains(e);
}

// e1 -> ... -> e2
// e2 -> e3
// e1 -> .... -> e3

// e1 -> ... x, e2 -> ... x
void theory_seq::solution_map::find_rec(expr* e, svector<std::pair<expr*, dependency*> >& finds) {
    dependency* d = 0;
    std::pair<expr*, dependency*> value(e, d);
    do {
        e = value.first;
        d = m_dm.mk_join(d, value.second);
        finds.push_back(value);
    }
    while (m_map.find(e, value));
}

bool theory_seq::solution_map::find1(expr* e, expr*& r, dependency*& d) {
    std::pair<expr*, dependency*> value;    
    if (m_map.find(e, value)) {
        d = m_dm.mk_join(d, value.second);
        r = value.first;
        return true;
    }
    else {
        return false;
    }
}

expr* theory_seq::solution_map::find(expr* e, dependency*& d) {
    std::pair<expr*, dependency*> value;
    d = 0;
    expr* result = e;
    while (m_map.find(result, value)) {
        d = m_dm.mk_join(d, value.second);
        SASSERT(result != value.first);
        SASSERT(e != value.first);
        result = value.first;
    }
    return result;
}

expr* theory_seq::solution_map::find(expr* e) {
    std::pair<expr*, dependency*> value;
    while (m_map.find(e, value)) {
        e = value.first;
    }
    return e;
}

void theory_seq::solution_map::pop_scope(unsigned num_scopes) {
    if (num_scopes == 0) return;
    m_cache.reset();
    unsigned start = m_limit[m_limit.size() - num_scopes];
    for (unsigned i = m_updates.size(); i > start; ) {
        --i;
        if (m_updates[i] == INS) {
            m_map.remove(m_lhs[i].get());
        }
        else {
            m_map.insert(m_lhs[i].get(), std::make_pair(m_rhs[i].get(), m_deps[i]));
        }
    }
    m_updates.resize(start);
    m_lhs.resize(start);
    m_rhs.resize(start);
    m_deps.resize(start);
    m_limit.resize(m_limit.size() - num_scopes);
}

void theory_seq::solution_map::display(std::ostream& out) const {
    eqdep_map_t::iterator it = m_map.begin(), end = m_map.end();
    for (; it != end; ++it) {
        out << mk_pp(it->m_key, m) << " |-> " << mk_pp(it->m_value.first, m) << "\n";
    }
}

bool theory_seq::exclusion_table::contains(expr* e, expr* r) const {
    if (e->get_id() > r->get_id()) {
        std::swap(e, r);
    }
    return m_table.contains(std::make_pair(e, r));
}

void theory_seq::exclusion_table::update(expr* e, expr* r) {
    if (e->get_id() > r->get_id()) {
        std::swap(e, r);
    }
    if (e != r && !m_table.contains(std::make_pair(e, r))) {
        m_lhs.push_back(e);
        m_rhs.push_back(r);
        m_table.insert(std::make_pair(e, r));
    }
}

void theory_seq::exclusion_table::pop_scope(unsigned num_scopes) {
    if (num_scopes == 0) return;
    unsigned start = m_limit[m_limit.size() - num_scopes];
    for (unsigned i = start; i < m_lhs.size(); ++i) {
        m_table.erase(std::make_pair(m_lhs[i].get(), m_rhs[i].get()));
    }
    m_lhs.resize(start);
    m_rhs.resize(start);
    m_limit.resize(m_limit.size() - num_scopes);
}

void theory_seq::exclusion_table::display(std::ostream& out) const {
    table_t::iterator it = m_table.begin(), end = m_table.end();
    for (; it != end; ++it) {
        out << mk_pp(it->first, m) << " != " << mk_pp(it->second, m) << "\n";
    }
}


theory_seq::theory_seq(ast_manager& m):
    theory(m.mk_family_id("seq")),
    m(m),
    m_rep(m, m_dm),
    m_eq_id(0),
    m_factory(0),
    m_exclude(m),
    m_axioms(m),
    m_axioms_head(0),
    m_mg(0),
    m_rewrite(m),
    m_seq_rewrite(m),
    m_util(m),
    m_autil(m),
    m_trail_stack(*this),
    m_ls(m), m_rs(m),
    m_lhs(m), m_rhs(m),
    m_atoms_qhead(0),
    m_new_solution(false),
    m_new_propagation(false),
    m_mk_aut(m) {
    m_prefix = "seq.prefix.suffix";
    m_suffix = "seq.suffix.prefix";
    m_contains_left = "seq.contains.left";
    m_contains_right = "seq.contains.right";
    m_accept = "aut.accept";
    m_reject = "aut.reject";
    m_tail           = "seq.tail";
    m_nth            = "seq.nth";
    m_seq_first      = "seq.first";
    m_seq_last       = "seq.last";
    m_indexof_left   = "seq.indexof.left";
    m_indexof_right  = "seq.indexof.right";
    m_aut_step       = "aut.step";
    m_pre            = "seq.pre";  // (seq.pre s l):  prefix of string s of length l
    m_post           = "seq.post"; // (seq.post s l): suffix of string s of length l
    m_eq             = "seq.eq";
}

theory_seq::~theory_seq() {
    m_trail_stack.reset();
}


final_check_status theory_seq::final_check_eh() {
    TRACE("seq", display(tout << "level: " << get_context().get_scope_level() << "\n"););
    if (simplify_and_solve_eqs()) {
        ++m_stats.m_solve_eqs;
        TRACE("seq", tout << ">>solve_eqs\n";);
        return FC_CONTINUE;
    }
    if (solve_nqs(0)) {
        ++m_stats.m_solve_nqs;
        TRACE("seq", tout << ">>solve_nqs\n";);
        return FC_CONTINUE;
    }
    if (branch_variable()) {
        ++m_stats.m_branch_variable;
        TRACE("seq", tout << ">>branch_variable\n";);
        return FC_CONTINUE;
    }
    if (check_length_coherence()) {
        ++m_stats.m_check_length_coherence;
        TRACE("seq", tout << ">>check_length_coherence\n";);
        return FC_CONTINUE;
    }
    if (!check_extensionality()) {
        ++m_stats.m_extensionality;
        TRACE("seq", tout << ">>extensionality\n";);
        return FC_CONTINUE;
    }
    if (propagate_automata()) {
        ++m_stats.m_propagate_automata;
        TRACE("seq", tout << ">>propagate_automata\n";);
        return FC_CONTINUE;
    }
    if (is_solved()) {
        TRACE("seq", tout << ">>is_solved\n";);
        return FC_DONE;
    }
    TRACE("seq", tout << ">>give_up\n";);
    return FC_GIVEUP;
}


bool theory_seq::branch_variable() {
    context& ctx = get_context();
    unsigned sz = m_eqs.size();
    int start = ctx.get_random_value();
    unsigned s = 0;
    for (unsigned i = 0; i < sz; ++i) {
        unsigned k = (i + start) % sz;
        eq const& e = m_eqs[k];
        unsigned id = e.id();

        s = find_branch_start(2*id);
        TRACE("seq", tout << s << " " << 2*id << ": " << e.ls() << " = " << e.rs() << "\n";);
        bool found = find_branch_candidate(s, e.dep(), e.ls(), e.rs());
        insert_branch_start(2*id, s);
        if (found) {
            return true;
        }
        s = find_branch_start(2*id + 1);
        found = find_branch_candidate(s, e.dep(), e.rs(), e.ls());
        insert_branch_start(2*id + 1, s);
        if (found) {
            return true;
        }

#if 0
        if (!has_length(e.ls())) {
            enforce_length(ensure_enode(e.ls()));
        }
        if (!has_length(e.rs())) {
            enforce_length(ensure_enode(e.rs()));
        }
#endif
    }
    return ctx.inconsistent();
}

void theory_seq::insert_branch_start(unsigned k, unsigned s) {
    m_branch_start.insert(k, s);
    m_trail_stack.push(pop_branch(k));
}

unsigned theory_seq::find_branch_start(unsigned k) {
    unsigned s = 0;
    if (m_branch_start.find(k, s)) {
        return s;
    }
    return 0;
}

bool theory_seq::find_branch_candidate(unsigned& start, dependency* dep, expr_ref_vector const& ls, expr_ref_vector const& rs) {

    if (ls.empty()) {
        return false;
    }
    expr* l = ls[0];

    if (!is_var(l)) {
        return false;
    }

    expr_ref v0(m);
    v0 = m_util.str.mk_empty(m.get_sort(l));
    literal_vector lits;
    if (can_be_equal(ls.size() - 1, ls.c_ptr() + 1, rs.size(), rs.c_ptr())) {
        if (l_false != assume_equality(l, v0)) {
            TRACE("seq", tout << mk_pp(l, m) << " " << v0 << "\n";);
            return true;
        }
        lits.push_back(~mk_eq_empty(l));
    }
    for (; start < rs.size(); ++start) {
        unsigned j = start;
        SASSERT(!m_util.str.is_concat(rs[j]));
        SASSERT(!m_util.str.is_string(rs[j]));
        if (l == rs[j]) {
            return false;
        }
        if (!can_be_equal(ls.size() - 1, ls.c_ptr() + 1, rs.size() - j - 1, rs.c_ptr() + j + 1)) {
            continue;
        }
        v0 = mk_concat(j + 1, rs.c_ptr());
        if (l_false != assume_equality(l, v0)) {
            TRACE("seq", tout << mk_pp(l, m) << " " << v0 << "\n";);
            ++start;
            return true;
        }
    }

    bool all_units = true;
    for (unsigned j = 0; all_units && j < rs.size(); ++j) {
        all_units &= m_util.str.is_unit(rs[j]);
    }
    if (all_units) {
        for (unsigned i = 0; i < rs.size(); ++i) {
            if (can_be_equal(ls.size() - 1, ls.c_ptr() + 1, rs.size() - i - 1, rs.c_ptr() + i + 1)) {
                v0 = mk_concat(i + 1, rs.c_ptr());
                lits.push_back(~mk_eq(l, v0, false));
            }
        }
        set_conflict(dep, lits);
        TRACE("seq", tout << mk_pp(l, m) << " " << v0 << "\n";);
        return true;
    }
    return false;
}

bool theory_seq::can_be_equal(unsigned szl, expr* const* ls, unsigned szr, expr* const* rs) const {
    unsigned i = 0;
    for (; i < szl && i < szr; ++i) {
        if (m.are_distinct(ls[i], rs[i])) {
            return false;
        }
        if (!m.are_equal(ls[i], rs[i])) {
            break;
        }
    }
    if (i == szr) {
        std::swap(ls, rs);
        std::swap(szl, szr);
    }
    if (i == szl && i < szr) {
        for (; i < szr; ++i) {
            if (m_util.str.is_unit(rs[i])) {
                return false;
            }
        }
    }
    return true;
}


lbool theory_seq::assume_equality(expr* l, expr* r) {
    context& ctx = get_context();
    if (m_exclude.contains(l, r)) {
        return l_false;
    }

    expr_ref eq(m.mk_eq(l, r), m);
    m_rewrite(eq);
    if (m.is_true(eq)) {
        return l_true;
    }
    if (m.is_false(eq)) {
        return l_false;
    }

    TRACE("seq", tout << mk_pp(l, m) << " = " << mk_pp(r, m) << "\n";);
    enode* n1 = ensure_enode(l);
    enode* n2 = ensure_enode(r);
    if (n1->get_root() == n2->get_root()) {
        return l_true;
    }
    ctx.mark_as_relevant(n1);
    ctx.mark_as_relevant(n2);
    ctx.assume_eq(n1, n2);
    return l_undef;
}

bool theory_seq::propagate_length_coherence(expr* e) {
    expr_ref head(m), tail(m);
    rational lo, hi;

    if (!is_var(e) || !m_rep.is_root(e)) {
        return false;
    }
    if (!lower_bound(e, lo) || !lo.is_pos() || lo >= rational(2048)) {
        return false;
    }
    TRACE("seq", tout << "Unsolved " << mk_pp(e, m);
          if (!lower_bound(e, lo)) lo = -rational::one();
          if (!upper_bound(e, hi)) hi = -rational::one();
          tout << " lo: " << lo << " hi: " << hi << "\n";
          );

    expr_ref seq(e, m);
    expr_ref_vector elems(m);
    unsigned _lo = lo.get_unsigned();
    for (unsigned j = 0; j < _lo; ++j) {
        mk_decompose(seq, head, tail);
        elems.push_back(head);
        seq = tail;
    }
    expr_ref emp(m_util.str.mk_empty(m.get_sort(e)), m);
    elems.push_back(seq);
    tail = mk_concat(elems.size(), elems.c_ptr());
    // len(e) >= low => e = tail;
    literal low(mk_literal(m_autil.mk_ge(m_util.str.mk_length(e), m_autil.mk_numeral(lo, true))));
    add_axiom(~low, mk_seq_eq(e, tail));
    if (upper_bound(e, hi)) {
        // len(e) <= hi => len(tail) <= hi - lo
        expr_ref high1(m_autil.mk_le(m_util.str.mk_length(e), m_autil.mk_numeral(hi, true)), m);
        if (hi == lo) {
            add_axiom(~mk_literal(high1), mk_seq_eq(seq, emp));
        }
        else {
            expr_ref high2(m_autil.mk_le(m_util.str.mk_length(seq), m_autil.mk_numeral(hi-lo, true)), m);
            add_axiom(~mk_literal(high1), mk_literal(high2));
        }
    }
    else {
        assume_equality(seq, emp);
    }
    return true;
}

bool theory_seq::check_length_coherence(expr* e) {
    if (is_var(e) && m_rep.is_root(e)) {
        expr_ref emp(m_util.str.mk_empty(m.get_sort(e)), m);
        expr_ref head(m), tail(m);
        if (!propagate_length_coherence(e) &&
            l_false == assume_equality(e, emp)) {
            // e = emp \/ e = unit(head.elem(e))*tail(e)
            mk_decompose(e, head, tail);
            expr_ref conc = mk_concat(head, tail);
            propagate_is_conc(e, conc);
            assume_equality(tail, emp);
        }
        else if (!get_context().at_base_level()) {
            m_trail_stack.push(push_replay(alloc(replay_length_coherence, m, e)));
        }
        return true;
    }
    return false;
}

bool theory_seq::check_length_coherence() {
    obj_hashtable<expr>::iterator it = m_length.begin(), end = m_length.end();
    for (; it != end; ++it) {
        expr* e = *it;
        if (check_length_coherence(e)) {
            return true;
        }
    }
    return false;
}

/*
    lit => s != ""
*/
void theory_seq::propagate_non_empty(literal lit, expr* s) {
    SASSERT(get_context().get_assignment(lit) == l_true);
    propagate_lit(0, 1, &lit, ~mk_eq_empty(s));
}

void theory_seq::propagate_is_conc(expr* e, expr* conc) {
    TRACE("seq", tout << mk_pp(conc, m) << " is non-empty\n";);
    context& ctx = get_context();
    literal lit = ~mk_eq_empty(e);
    SASSERT(ctx.get_assignment(lit) == l_true);
    propagate_lit(0, 1, &lit, mk_eq(e, conc, false));
    expr_ref e1(e, m), e2(conc, m);
    new_eq_eh(m_dm.mk_leaf(assumption(lit)), ctx.get_enode(e1), ctx.get_enode(e2));
}

bool theory_seq::is_nth(expr* e) const {
    return is_skolem(m_nth, e);
}

bool theory_seq::is_tail(expr* e, expr*& s, unsigned& idx) const {
    rational r;
    return
        is_skolem(m_tail, e) &&
        m_autil.is_numeral(to_app(e)->get_arg(1), r) &&
        (idx = r.get_unsigned(), s = to_app(e)->get_arg(0), true);
}

bool theory_seq::is_eq(expr* e, expr*& a, expr*& b) const {
    return is_skolem(m_eq, e) &&
        (a = to_app(e)->get_arg(0), b = to_app(e)->get_arg(1), true);
}

bool theory_seq::is_pre(expr* e, expr*& s, expr*& i) {
    return is_skolem(m_pre, e) && (s = to_app(e)->get_arg(0), i = to_app(e)->get_arg(1), true);
}

bool theory_seq::is_post(expr* e, expr*& s, expr*& i) {
    return is_skolem(m_post, e) && (s = to_app(e)->get_arg(0), i = to_app(e)->get_arg(1), true);
}



expr_ref theory_seq::mk_nth(expr* s, expr* idx) {
    sort* char_sort = 0;
    VERIFY(m_util.is_seq(m.get_sort(s), char_sort));
    return mk_skolem(m_nth, s, idx, 0, char_sort);
}

expr_ref theory_seq::mk_last(expr* s) {
    zstring str;
    if (m_util.str.is_string(s, str) && str.length() > 0) {
        return expr_ref(m_util.str.mk_char(str, str.length()-1), m);
    }
    sort* char_sort = 0;
    VERIFY(m_util.is_seq(m.get_sort(s), char_sort));
    return mk_skolem(m_seq_last, s, 0, 0, char_sort);
}

expr_ref theory_seq::mk_first(expr* s) {
    zstring str;
    if (m_util.str.is_string(s, str) && str.length() > 0) {
        return expr_ref(m_util.str.mk_string(str.extract(0, str.length()-1)), m);
    }
    return mk_skolem(m_seq_first, s);
}


void theory_seq::mk_decompose(expr* e, expr_ref& head, expr_ref& tail) {
    expr* e1, *e2;
    zstring s;
    if (m_util.str.is_empty(e)) {
        head = m_util.str.mk_unit(mk_nth(e, m_autil.mk_int(0)));
        tail = e;
    }
    else if (m_util.str.is_string(e, s)) {
        head = m_util.str.mk_unit(m_util.str.mk_char(s, 0));
        tail = m_util.str.mk_string(s.extract(1, s.length()-1));
    }
    else if (m_util.str.is_unit(e)) {
        head = e;
        tail = m_util.str.mk_empty(m.get_sort(e));
    }
    else if (m_util.str.is_concat(e, e1, e2) && m_util.str.is_unit(e1)) {
        head = e1;
        tail = e2;
    }
    else if (is_skolem(m_tail, e)) {
        rational r;
        app* a = to_app(e);
        expr* s = a->get_arg(0);
        VERIFY (m_autil.is_numeral(a->get_arg(1), r));
        expr* idx = m_autil.mk_int(r.get_unsigned() + 1);
        head = m_util.str.mk_unit(mk_nth(s, idx));
        tail = mk_skolem(m_tail, s, idx);
    }
    else {
        head = m_util.str.mk_unit(mk_nth(e, m_autil.mk_int(0)));
        tail = mk_skolem(m_tail, e, m_autil.mk_int(0));
    }
}

/*
   \brief Check extensionality (for sequences).
 */
bool theory_seq::check_extensionality() {
    context& ctx = get_context();
    unsigned sz = get_num_vars();
    unsigned_vector seqs;
    for (unsigned v = 0; v < sz; ++v) {
        enode* n1 = get_enode(v);
        expr* o1 = n1->get_owner();
        if (n1 != n1->get_root()) {
            continue;
        }
        if (!seqs.empty() && ctx.is_relevant(n1) && m_util.is_seq(o1) && ctx.is_shared(n1)) {
            dependency* dep = 0;
            expr_ref e1 = canonize(o1, dep);
            for (unsigned i = 0; i < seqs.size(); ++i) {
                enode* n2 = get_enode(seqs[i]);
                expr* o2 = n2->get_owner();
                if (m.get_sort(o1) != m.get_sort(o2)) {
                    continue;
                }
                if (ctx.is_diseq(n1, n2) || m_exclude.contains(o1, o2)) {
                    continue;
                }
                expr_ref e2 = canonize(n2->get_owner(), dep);
                m_lhs.reset(); m_rhs.reset();
                bool change = false;
                if (!m_seq_rewrite.reduce_eq(e1, e2, m_lhs, m_rhs, change)) {
                    m_exclude.update(o1, o2);
                    continue;
                }
                bool excluded = false;
                for (unsigned j = 0; !excluded && j < m_lhs.size(); ++j) {
                    if (m_exclude.contains(m_lhs[j].get(), m_rhs[j].get())) {
                        excluded = true;
                    }
                }
                if (excluded) {
                    continue;
                }
                TRACE("seq", tout << m_lhs << " = " << m_rhs << "\n";);
                ctx.assume_eq(n1, n2);                
                return false;
            }
        }
        seqs.push_back(v);
    }
    return true;
}

/*
   - Eqs = 0
   - Diseqs evaluate to false
   - lengths are coherent.
*/

bool theory_seq::is_solved() {
    if (!m_eqs.empty()) {
        TRACE("seq", tout << "(seq.giveup " << m_eqs[0].ls() << " = " << m_eqs[0].rs() << " is unsolved)\n";);
        IF_VERBOSE(10, verbose_stream() << "(seq.giveup " << m_eqs[0].ls() << " = " << m_eqs[0].rs() << " is unsolved)\n";);
        return false;
    }
    for (unsigned i = 0; i < m_automata.size(); ++i) {
        if (!m_automata[i]) {
            TRACE("seq", tout  << "(seq.giveup regular expression did not compile to automaton)\n";);
            IF_VERBOSE(10, verbose_stream() << "(seq.giveup regular expression did not compile to automaton)\n";);
            return false;
        }
    }
    if (false && !m_nqs.empty()) {
        TRACE("seq", display_disequation(tout << "(seq.giveup ", m_nqs[0]); tout << " is unsolved)\n";);
        IF_VERBOSE(10, display_disequation(verbose_stream() << "(seq.giveup ", m_nqs[0]); verbose_stream() << " is unsolved)\n";);
        return false;
    }

    return true;
}

void theory_seq::linearize(dependency* dep, enode_pair_vector& eqs, literal_vector& lits) const {
    svector<assumption> assumptions;
    const_cast<dependency_manager&>(m_dm).linearize(dep, assumptions);
    for (unsigned i = 0; i < assumptions.size(); ++i) {
        assumption const& a = assumptions[i];
        if (a.lit != null_literal) {
            lits.push_back(a.lit);
        }
        if (a.n1 != 0) {
            eqs.push_back(enode_pair(a.n1, a.n2));
        }
    }
}



void theory_seq::propagate_lit(dependency* dep, unsigned n, literal const* _lits, literal lit) {
    context& ctx = get_context();
    ctx.mark_as_relevant(lit);
    literal_vector lits(n, _lits);
    enode_pair_vector eqs;
    linearize(dep, eqs, lits);
    TRACE("seq", ctx.display_detailed_literal(tout, lit);
          tout << " <- "; ctx.display_literals_verbose(tout, lits.size(), lits.c_ptr()); if (!lits.empty()) tout << "\n"; display_deps(tout, dep););
    justification* js =
        ctx.mk_justification(
            ext_theory_propagation_justification(
                get_id(), ctx.get_region(), lits.size(), lits.c_ptr(), eqs.size(), eqs.c_ptr(), lit));

    m_new_propagation = true;
    ctx.assign(lit, js);
}

void theory_seq::set_conflict(dependency* dep, literal_vector const& _lits) {
    context& ctx = get_context();
    enode_pair_vector eqs;
    literal_vector lits(_lits);
    linearize(dep, eqs, lits);
    TRACE("seq", display_deps(tout, lits, eqs););
    m_new_propagation = true;
    ctx.set_conflict(
        ctx.mk_justification(
            ext_theory_conflict_justification(
                get_id(), ctx.get_region(), lits.size(), lits.c_ptr(), eqs.size(), eqs.c_ptr(), 0, 0)));
}

void theory_seq::propagate_eq(dependency* dep, enode* n1, enode* n2) {
    if (n1->get_root() == n2->get_root()) {
        return;
    }
    context& ctx = get_context();
    literal_vector lits;
    enode_pair_vector eqs;
    linearize(dep, eqs, lits);
    TRACE("seq",
          tout << mk_pp(n1->get_owner(), m) << " = " << mk_pp(n2->get_owner(), m) << " <- \n";
          display_deps(tout, dep);
          );

    justification* js = ctx.mk_justification(
        ext_theory_eq_propagation_justification(
            get_id(), ctx.get_region(), lits.size(), lits.c_ptr(), eqs.size(), eqs.c_ptr(), n1, n2));
    ctx.assign_eq(n1, n2, eq_justification(js));
    m_new_propagation = true;

    enforce_length_coherence(n1, n2);
}

void theory_seq::enforce_length_coherence(enode* n1, enode* n2) {
    expr* o1 = n1->get_owner();
    expr* o2 = n2->get_owner();
    if (m_util.str.is_concat(o1) && m_util.str.is_concat(o2)) {
        return;
    }
    if (has_length(o1) && !has_length(o2)) {
        enforce_length(n2);
    }
    else if (has_length(o2) && !has_length(o1)) {
        enforce_length(n1);
    }
}



bool theory_seq::simplify_eq(expr_ref_vector& ls, expr_ref_vector& rs, dependency* deps) {
    context& ctx = get_context();
    expr_ref_vector lhs(m), rhs(m);
    bool changed = false;
    TRACE("seq", tout << ls << " = " << rs << "\n";);
    if (!m_seq_rewrite.reduce_eq(ls, rs, lhs, rhs, changed)) {
        // equality is inconsistent.
        TRACE("seq", tout << ls << " != " << rs << "\n";);
        set_conflict(deps);
        return true;
    }
    if (!changed) {
        SASSERT(lhs.empty() && rhs.empty());
        return false;
    }
    SASSERT(lhs.size() == rhs.size());
    m_seq_rewrite.add_seqs(ls, rs, lhs, rhs);
    if (lhs.empty()) {
        return true;
    }
    TRACE("seq", 
          tout << ls << " = " << rs << "\n";
          tout << lhs << " = " << rhs << "\n";);
    for (unsigned i = 0; !ctx.inconsistent() && i < lhs.size(); ++i) {
        expr_ref li(lhs[i].get(), m);
        expr_ref ri(rhs[i].get(), m);
        if (solve_unit_eq(li, ri, deps)) {
            // no-op
        }
        else if (m_util.is_seq(li) || m_util.is_re(li)) {
            m_eqs.push_back(mk_eqdep(li, ri, deps));            
        }
        else {
            propagate_eq(deps, ensure_enode(li), ensure_enode(ri));
        }
    }
    TRACE("seq",
          if (!ls.empty() || !rs.empty()) tout << ls << " = " << rs << ";\n";
          for (unsigned i = 0; i < lhs.size(); ++i) {
              tout << mk_pp(lhs[i].get(), m) << " = " << mk_pp(rhs[i].get(), m) << ";\n";
          });


    return true;
}

bool theory_seq::solve_unit_eq(expr_ref_vector const& l, expr_ref_vector const& r, dependency* deps) {
    if (l.size() == 1 && is_var(l[0]) && !occurs(l[0], r) && add_solution(l[0], mk_concat(r, m.get_sort(l[0])), deps)) {
        return true;
    }
    if (r.size() == 1 && is_var(r[0]) && !occurs(r[0], l) && add_solution(r[0], mk_concat(l, m.get_sort(r[0])), deps)) {
        return true;
    }

    return false;
}

bool theory_seq::reduce_length(expr* l, expr* r, literal_vector& lits) {

    rational val1, val2;
    if (has_length(l) && has_length(r) && 
        get_length(l, val1) && get_length(r, val2) && val1 == val2) {
        context& ctx = get_context();
        expr_ref len1(m_util.str.mk_length(l), m);
        expr_ref len2(m_util.str.mk_length(r), m);
        literal lit = mk_eq(len1, len2, false);
        if (ctx.get_assignment(lit) == l_true) {
            lits.push_back(lit);
            return true;
        }
        else {
            TRACE("seq", tout << "Assignment: " << len1 << " = " << len2 << " " << ctx.get_assignment(lit) << "\n";);
            return false;
        }
    }
    expr_ref len1(m), len2(m);
    lits.reset();
    if (get_length(l, len1, lits) &&
        get_length(r, len2, lits) && len1 == len2) {
        return true;
    }
    else {
        return false;
    }
}

bool theory_seq::solve_unit_eq(expr* l, expr* r, dependency* deps) {
    if (l == r) {
        return true;
    }
    if (is_var(l) && !occurs(l, r) && add_solution(l, r, deps)) {
        return true;
    }
    if (is_var(r) && !occurs(r, l) && add_solution(r, l, deps)) {
        return true;
    }

    return false;
}


bool theory_seq::occurs(expr* a, expr_ref_vector const& b) {
    for (unsigned i = 0; i < b.size(); ++i) {
        if (a == b[i]) return true;
    }
    return false;
}

bool theory_seq::occurs(expr* a, expr* b) {
     // true if a occurs under an interpreted function or under left/right selector.
    SASSERT(is_var(a));
    SASSERT(m_todo.empty());
    expr* e1, *e2;
    m_todo.push_back(b);
    while (!m_todo.empty()) {
        b = m_todo.back();
        if (a == b) {
            m_todo.reset();
            return true;
        }
        m_todo.pop_back();
        if (m_util.str.is_concat(b, e1, e2)) {
            m_todo.push_back(e1);
            m_todo.push_back(e2);
        }
    }
     return false;
}


bool theory_seq::is_var(expr* a) {
    return
        m_util.is_seq(a) &&
        !m_util.str.is_concat(a) &&
        !m_util.str.is_empty(a)  &&
        !m_util.str.is_string(a) &&
        !m_util.str.is_unit(a);
}



bool theory_seq::add_solution(expr* l, expr* r, dependency* deps)  {
    if (l == r) {
        return false;
    }
    TRACE("seq", tout << mk_pp(l, m) << " ==> " << mk_pp(r, m) << "\n";);
    m_new_solution = true;
    m_rep.update(l, r, deps);
    enode* n1 = ensure_enode(l);
    enode* n2 = ensure_enode(r);
    if (n1->get_root() != n2->get_root()) {
        propagate_eq(deps, n1, n2);
    }
    return true;
}

bool theory_seq::solve_eqs(unsigned i) {
    context& ctx = get_context();
    bool change = false;
    for (; !ctx.inconsistent() && i < m_eqs.size(); ++i) {
        eq const& e = m_eqs[i];
        if (solve_eq(e.ls(), e.rs(), e.dep())) {
            if (i + 1 != m_eqs.size()) {
                eq e1 = m_eqs[m_eqs.size()-1];
                m_eqs.set(i, e1);
                --i;
            }
            ++m_stats.m_num_reductions;
            m_eqs.pop_back();
            change = true;
        }
    }
    return change || ctx.inconsistent();
}

bool theory_seq::solve_eq(expr_ref_vector const& l, expr_ref_vector const& r, dependency* deps) {
    context& ctx = get_context();
    expr_ref_vector& ls = m_ls;
    expr_ref_vector& rs = m_rs;
    rs.reset(); ls.reset();
    dependency* dep2 = 0;
    bool change = canonize(l, ls, dep2);
    change = canonize(r, rs, dep2) || change;
    deps = m_dm.mk_join(dep2, deps);
    TRACE("seq", tout << l << " = " << r << " ==> ";
          tout << ls << " = " << rs << "\n";);
    if (!ctx.inconsistent() && simplify_eq(ls, rs, deps)) {
        return true;
    }
    TRACE("seq", tout << ls << " = " << rs << "\n";);
    if (ls.empty() && rs.empty()) {
        return true;
    }
    if (!ctx.inconsistent() && solve_unit_eq(ls, rs, deps)) {
        TRACE("seq", tout << "unit\n";);
        return true;
    }
    if (!ctx.inconsistent() && reduce_length_eq(ls, rs, deps)) {
        TRACE("seq", tout << "length\n";);
        return true;
    }
    if (!ctx.inconsistent() && solve_binary_eq(ls, rs, deps)) {
        TRACE("seq", tout << "binary\n";);
        return true;
    }
    if (!ctx.inconsistent() && change) {
        m_eqs.push_back(eq(m_eq_id++, ls, rs, deps));
        return true;
    }
    return false;
}

bool theory_seq::propagate_max_length(expr* l, expr* r, dependency* deps) {
    unsigned idx;
    expr* s;
    if (m_util.str.is_empty(l)) {
        std::swap(l, r);
    }
    rational hi;
    if (is_tail(l, s, idx) && has_length(s) && m_util.str.is_empty(r) && !upper_bound(s, hi)) {
        propagate_lit(deps, 0, 0, mk_literal(m_autil.mk_le(m_util.str.mk_length(s), m_autil.mk_int(idx+1))));
        return true;
    }
    return false;
}

bool theory_seq::is_binary_eq(expr_ref_vector const& ls, expr_ref_vector const& rs, expr*& x, ptr_vector<expr>& xs, ptr_vector<expr>& ys, expr*& y) {
    if (ls.size() > 1 && is_var(ls[0]) &&
        rs.size() > 1 && is_var(rs.back())) {
        xs.reset();
        ys.reset();
        x = ls[0];
        y = rs.back();
        for (unsigned i = 1; i < ls.size(); ++i) {
            if (!m_util.str.is_unit(ls[i])) return false;
        }
        for (unsigned i = 0; i < rs.size()-1; ++i) {
            if (!m_util.str.is_unit(rs[i])) return false;
        }
        xs.append(ls.size()-1, ls.c_ptr() + 1);
        ys.append(rs.size()-1, rs.c_ptr());
        return true;
    }
    return false;
}

bool theory_seq::reduce_length_eq(expr_ref_vector const& ls, expr_ref_vector const& rs, dependency* deps) {
    if (ls.empty() || rs.empty()) {
        return false;
    }
    if (ls.size() <= 1 && rs.size() <= 1) {
        return false;
    }
    SASSERT(ls.size() > 1 || rs.size() > 1);

    literal_vector lits;
    expr_ref l(ls[0], m), r(rs[0], m);
    if (reduce_length(l, r, lits)) {
        expr_ref_vector lhs(m), rhs(m);
        lhs.append(ls.size()-1, ls.c_ptr() + 1);
        rhs.append(rs.size()-1, rs.c_ptr() + 1);
        SASSERT(!lhs.empty() || !rhs.empty());
        m_eqs.push_back(eq(m_eq_id++, lhs, rhs, deps));
        TRACE("seq", tout << "Propagate equal lengths " << l << " " << r << "\n";);
        propagate_eq(deps, lits, l, r, true);
        return true;
    }

    l = ls.back(); r = rs.back();
    if (reduce_length(l, r, lits)) {
        expr_ref_vector lhs(m), rhs(m);
        lhs.append(ls.size()-1, ls.c_ptr());
        rhs.append(rs.size()-1, rs.c_ptr());
        SASSERT(!lhs.empty() || !rhs.empty());
        m_eqs.push_back(eq(m_eq_id++, lhs, rhs, deps));
        TRACE("seq", tout << "Propagate equal lengths " << l << " " << r << "\n";);
        propagate_eq(deps, lits, l, r, true);
        return true;
    }

    return false;
 }

bool theory_seq::solve_binary_eq(expr_ref_vector const& ls, expr_ref_vector const& rs, dependency* dep) {
    context& ctx = get_context();
    ptr_vector<expr> xs, ys;
    expr* x, *y;
    bool is_binary = is_binary_eq(ls, rs, x, xs, ys, y);
    if (!is_binary) {
        is_binary = is_binary_eq(rs, ls, x, xs, ys, y);
    }
    if (!is_binary) {
        return false;
    }
    // Equation is of the form x ++ xs = ys ++ y
    // where xs, ys are units.
    if (x != y) {
        return false;
    }
    if (xs.size() != ys.size()) {
        TRACE("seq", tout << "binary conflict\n";);
        set_conflict(dep);
        return false;
    }
    if (xs.empty()) {
        // this should have been solved already
        UNREACHABLE();
        return false;
    }
    unsigned sz = xs.size();
    literal_vector conflict;
    for (unsigned offset = 0; offset < sz; ++offset) {
        bool has_conflict = false;
        for (unsigned j = 0; !has_conflict && j < sz; ++j) {
            unsigned j1 = (offset + j) % sz;
            literal eq = mk_eq(xs[j], ys[j1], false);
            switch (ctx.get_assignment(eq)) {
            case l_false:
                conflict.push_back(~eq);
                has_conflict = true;
                break;
            case l_undef: {
                enode* n1 = ensure_enode(xs[j]);
                enode* n2 = ensure_enode(ys[j1]);
                if (n1->get_root() == n2->get_root()) {
                    break;
                }
                ctx.mark_as_relevant(eq);
                if (sz == 1) {
                    propagate_lit(dep, 0, 0, eq);
                    return true;
                }
                m_new_propagation = true;
                break;
            }
            case l_true:
                break;
            }
        }
        if (!has_conflict) {
            TRACE("seq", tout << "offset: " << offset << " equality ";
                  for (unsigned j = 0; j < sz; ++j) {
                      tout << mk_pp(xs[j], m) << " = " << mk_pp(ys[(offset+j) % sz], m) << "; ";
                  }
                  tout << "\n";);
            // current equalities can work when solving x ++ xs = ys ++ y
            return false;
        }
    }
    TRACE("seq", tout << conflict << "\n";);
    set_conflict(dep, conflict);
    return false;
}

bool theory_seq::get_length(expr* e, expr_ref& len, literal_vector& lits) {
    context& ctx = get_context();
    expr* s, *i, *l;
    rational r;
    if (m_util.str.is_extract(e, s, i, l)) {
        // 0 <= i <= len(s), 0 <= l, i + l <= len(s)
        expr_ref zero(m_autil.mk_int(0), m);        
        expr_ref ls(m_util.str.mk_length(s), m);
        expr_ref ls_minus_i_l(mk_sub(mk_sub(ls, i),l), m);
        bool i_is_zero = m_autil.is_numeral(i, r) && r.is_zero();
        literal i_ge_0 = i_is_zero?true_literal:mk_literal(m_autil.mk_ge(i, zero));
        literal i_lt_len_s = ~mk_literal(m_autil.mk_ge(mk_sub(i, ls), zero));
        literal li_ge_ls  = mk_literal(m_autil.mk_ge(ls_minus_i_l, zero));
        literal l_ge_zero = mk_literal(m_autil.mk_ge(l, zero));
        literal _lits[4] = { i_ge_0, i_lt_len_s, li_ge_ls, l_ge_zero };
        if (ctx.get_assignment(i_ge_0) == l_true &&
            ctx.get_assignment(i_lt_len_s) == l_true && 
            ctx.get_assignment(li_ge_ls) == l_true &&
            ctx.get_assignment(l_ge_zero) == l_true) {
            len = l;
            lits.append(4, _lits);
            return true;
        }
        TRACE("seq", tout << mk_pp(e, m) << "\n"; ctx.display_literals_verbose(tout, 4, _lits); tout << "\n";
              for (unsigned i = 0; i < 4; ++i) tout << ctx.get_assignment(_lits[i]) << "\n";);
    }
    else if (m_util.str.is_at(e, s, i)) {
        // has length 1 if 0 <= i < len(s)
        expr_ref zero(m_autil.mk_int(0), m);
        bool i_is_zero = m_autil.is_numeral(i, r) && r.is_zero();
        literal i_ge_0 = i_is_zero?true_literal:mk_literal(m_autil.mk_ge(i, zero));
        literal i_lt_len_s = ~mk_literal(m_autil.mk_ge(mk_sub(i, m_util.str.mk_length(s)), zero));
        literal _lits[2] = { i_ge_0, i_lt_len_s};
        if (ctx.get_assignment(i_ge_0) == l_true &&
            ctx.get_assignment(i_lt_len_s) == l_true) {
            len = m_autil.mk_int(1);
            lits.append(2, _lits);
            return true;
        }
        TRACE("seq", ctx.display_literals_verbose(tout, 2, _lits); tout << "\n";);
    }
    else if (is_pre(e, s, i)) {
        expr_ref zero(m_autil.mk_int(0), m);
        bool i_is_zero = m_autil.is_numeral(i, r) && r.is_zero();
        literal i_ge_0 = i_is_zero?true_literal:mk_literal(m_autil.mk_ge(i, zero));
        literal i_lt_len_s = ~mk_literal(m_autil.mk_ge(mk_sub(i, m_util.str.mk_length(s)), zero));
        literal _lits[2] = { i_ge_0, i_lt_len_s };
        if (ctx.get_assignment(i_ge_0) == l_true &&
            ctx.get_assignment(i_lt_len_s) == l_true) {
            len = i;
            lits.append(2, _lits);
            return true;
        }
        TRACE("seq", ctx.display_literals_verbose(tout, 2, _lits); tout << "\n";);
    }
    else if (is_post(e, s, l)) {
        expr_ref zero(m_autil.mk_int(0), m);
        literal l_ge_0 = mk_literal(m_autil.mk_ge(l, zero));
        literal l_le_len_s = mk_literal(m_autil.mk_ge(mk_sub(m_util.str.mk_length(s), l), zero));
        literal _lits[2] = { l_ge_0, l_le_len_s };
        if (ctx.get_assignment(l_ge_0) == l_true && 
            ctx.get_assignment(l_le_len_s) == l_true) {
            len = l;
            lits.append(2, _lits);
            return true;
        }
        TRACE("seq", ctx.display_literals_verbose(tout, 2, _lits); tout << "\n";);
    }
    else if (m_util.str.is_unit(e)) {
        len = m_autil.mk_int(1);
        return true;
    }
    else {
        TRACE("seq", tout << "unhandled: " << mk_pp(e, m) << "\n";);
    }
    return false;    
}


bool theory_seq::solve_nqs(unsigned i) {
    context & ctx = get_context();
    for (; !ctx.inconsistent() && i < m_nqs.size(); ++i) {
        if (solve_ne(i)) {
            if (i + 1 != m_nqs.size()) {
                ne n = m_nqs[m_nqs.size()-1];
                m_nqs.set(i, n);
                --i;
            }
            m_nqs.pop_back();
        }
    }
    return m_new_propagation || ctx.inconsistent();
}


bool theory_seq::solve_ne(unsigned idx) {
    context& ctx = get_context();
    ne const& n = m_nqs[idx];

    unsigned num_undef_lits = 0;
    for (unsigned i = 0; i < n.lits().size(); ++i) {
        switch (ctx.get_assignment(n.lits(i))) {
        case l_false:
            TRACE("seq", display_disequation(tout << "has false literal\n", n););
            return true;
        case l_true:
            break;
        case l_undef:
            ++num_undef_lits;
            break;
        }
    }

    bool updated = false;
    dependency* new_deps = n.dep();
    vector<expr_ref_vector> new_ls, new_rs;
    literal_vector new_lits(n.lits());
    for (unsigned i = 0; i < n.ls().size(); ++i) {
        expr_ref_vector& ls = m_ls;
        expr_ref_vector& rs = m_rs;
        expr_ref_vector& lhs = m_lhs;
        expr_ref_vector& rhs = m_rhs;
        ls.reset(); rs.reset(); lhs.reset(); rhs.reset();
        dependency* deps = 0;
        bool change = false;
        change = canonize(n.ls(i), ls, deps) || change;
        change = canonize(n.rs(i), rs, deps) || change;

        if (!m_seq_rewrite.reduce_eq(ls, rs, lhs, rhs, change)) {
            TRACE("seq", display_disequation(tout << "reduces to false: ", n););
            return true;
        }
        else if (!change) {
            TRACE("seq", tout << "no change " << n.ls(i) << " " << n.rs(i) << "\n";);
            if (updated) {
                new_ls.push_back(n.ls(i));
                new_rs.push_back(n.rs(i));
            }
            continue;
        }
        else {

            if (!updated) {
                for (unsigned j = 0; j < i; ++j) {
                    new_ls.push_back(n.ls(j));
                    new_rs.push_back(n.rs(j));
                }
            }
            updated = true;
            if (!ls.empty() || !rs.empty()) {
                new_ls.push_back(ls);
                new_rs.push_back(rs);
            }
            TRACE("seq",
                  tout << lhs << " != " << rhs << "\n";
                  for (unsigned j = 0; j < new_ls.size(); ++j) tout << new_ls[j] << " != " << new_rs[j] << "\n";
                  tout << n.ls(i) << " != " << n.rs(i) << "\n";);

            for (unsigned j = 0; j < lhs.size(); ++j) {
                expr* nl = lhs[j].get();
                expr* nr = rhs[j].get();
                if (m_util.is_seq(nl) || m_util.is_re(nl)) {
                    ls.reset();
                    rs.reset(); 
                    m_util.str.get_concat(nl, ls);
                    m_util.str.get_concat(nr, rs);
                    new_ls.push_back(ls);
                    new_rs.push_back(rs);
                }
                else {
                    literal lit(mk_eq(nl, nr, false));
                    ctx.mark_as_relevant(lit);
                    new_lits.push_back(lit);
                    switch (ctx.get_assignment(lit)) {
                    case l_false:
                        return true;
                    case l_true:
                        break;
                    case l_undef:
                        ++num_undef_lits;
                        m_new_propagation = true;
                        break;
                    }
                }
            }
            new_deps = m_dm.mk_join(deps, new_deps);
        }
    }

    TRACE("seq", display_disequation(tout, n););

    if (!updated && num_undef_lits == 0) {
        return false;
    }
    if (!updated) {
        for (unsigned j = 0; j < n.ls().size(); ++j) {
            new_ls.push_back(n.ls(j));
            new_rs.push_back(n.rs(j));
        }
    }

    if (num_undef_lits == 1 && new_ls.empty()) {
        literal_vector lits;
        literal undef_lit = null_literal;
        for (unsigned i = 0; i < new_lits.size(); ++i) {
            literal lit = new_lits[i];
            switch (ctx.get_assignment(lit)) {
            case l_true:
                lits.push_back(lit);
                break;
            case l_false:
                UNREACHABLE();
                break;
            case l_undef:
                SASSERT(undef_lit == null_literal);
                undef_lit = lit;
                break;
            }
        }
        TRACE("seq", tout << "propagate: " << undef_lit << "\n";);
        SASSERT(undef_lit != null_literal);
        propagate_lit(new_deps, lits.size(), lits.c_ptr(), ~undef_lit);
        return true;
    }
    if (updated) {
        if (num_undef_lits == 0 && new_ls.empty()) {
            TRACE("seq", tout << "conflict\n";);

            dependency* deps1 = 0;
            if (explain_eq(n.l(), n.r(), deps1)) {
                new_lits.reset();
                new_lits.push_back(~mk_eq(n.l(), n.r(), false));
                new_deps = deps1;
                TRACE("seq", tout << "conflict explained\n";);
            }
            set_conflict(new_deps, new_lits);
            SASSERT(m_new_propagation);
        }
        else {
            m_nqs.push_back(ne(n.l(), n.r(), new_ls, new_rs, new_lits, new_deps));
        }
    }
    return updated;
}

theory_seq::cell* theory_seq::mk_cell(cell* p, expr* e, dependency* d) {
    cell* c = alloc(cell, p, e, d);
    m_all_cells.push_back(c);
    return c;
}

void theory_seq::unfold(cell* c, ptr_vector<cell>& cons) {
    dependency* dep = 0;
    expr* a, *e1, *e2;
    if (m_rep.find1(c->m_expr, a, dep)) {
        cell* c1 = mk_cell(c, a, m_dm.mk_join(dep, c->m_dep));
        unfold(c1, cons);
    }
    else if (m_util.str.is_concat(c->m_expr, e1, e2)) {
        cell* c1 = mk_cell(c, e1, c->m_dep);
        cell* c2 = mk_cell(0, e2, 0);
        unfold(c1, cons);
        unfold(c2, cons);
    }
    else {
        cons.push_back(c);
    }
    c->m_last = cons.size()-1;
} 
// 
// a -> a1.a2, a2 -> a3.a4 -> ...
// b -> b1.b2, b2 -> b3.a4 
// 
// e1 
//

void theory_seq::display_explain(std::ostream& out, unsigned indent, expr* e) {
    expr* e1, *e2, *a;
    dependency* dep = 0;
    smt2_pp_environment_dbg env(m);
    params_ref p;
    for (unsigned i = 0; i < indent; ++i) out << " ";
    ast_smt2_pp(out, e, env, p, indent);
    out << "\n";

    if (m_rep.find1(e, a, dep)) {
        display_explain(out, indent + 1, a);
    }
    else if (m_util.str.is_concat(e, e1, e2)) {
        display_explain(out, indent + 1, e1);
        display_explain(out, indent + 1, e2);        
    }
}

bool theory_seq::explain_eq(expr* e1, expr* e2, dependency*& dep) {

    if (e1 == e2) {
        return true;
    }
    expr* a1, *a2;
    ptr_vector<cell> v1, v2;
    unsigned cells_sz = m_all_cells.size();
    cell* c1 = mk_cell(0, e1, 0);
    cell* c2 = mk_cell(0, e2, 0);
    unfold(c1, v1);
    unfold(c2, v2);
    unsigned i = 0, j = 0;
    
    TRACE("seq", 
          tout << "1:\n";
          display_explain(tout, 0, e1);
          tout << "2:\n";
          display_explain(tout, 0, e2););

    bool result = true;
    while (i < v1.size() || j < v2.size()) {
        if (i == v1.size()) {
            while (j < v2.size() && m_util.str.is_empty(v2[j]->m_expr)) {
                dep = m_dm.mk_join(dep, v2[j]->m_dep);
                ++j;
            }
            result = j == v2.size();
            break;
        }
        if (j == v2.size()) {
            while (i < v1.size() && m_util.str.is_empty(v1[i]->m_expr)) {
                dep = m_dm.mk_join(dep, v1[i]->m_dep);
                ++i;
            }
            result = i == v1.size();
            break;
        }
        cell* c1 = v1[i];
        cell* c2 = v2[j];
        e1 = c1->m_expr;
        e2 = c2->m_expr;
        if (e1 == e2) {
            if (c1->m_parent && c2->m_parent && 
                c1->m_parent->m_expr == c2->m_parent->m_expr) {
                TRACE("seq", tout << "parent: " << mk_pp(e1, m) << " " << mk_pp(c1->m_parent->m_expr, m) << "\n";);
                c1 = c1->m_parent;
                c2 = c2->m_parent;
                v1[c1->m_last] = c1;
                i = c1->m_last;
                v2[c2->m_last] = c2;
                j = c2->m_last;
            }
            else {
                dep = m_dm.mk_join(dep, c1->m_dep);
                dep = m_dm.mk_join(dep, c2->m_dep);
                ++i;
                ++j;
            }
        }
        else if (m_util.str.is_empty(e1)) {
            dep = m_dm.mk_join(dep, c1->m_dep);
            ++i;
        }
        else if (m_util.str.is_empty(e2)) {
            dep = m_dm.mk_join(dep, c2->m_dep);
            ++j;
        }
        else if (m_util.str.is_unit(e1, a1) &&
                 m_util.str.is_unit(e2, a2)) {
            if (explain_eq(a1, a2, dep)) {
                ++i;
                ++j;
            }
            else {
                result = false;
                break;
            }
        }
        else {
            TRACE("seq", tout << "Could not solve " << mk_pp(e1, m) << " = " << mk_pp(e2, m) << "\n";);
            result = false;
            break;
        }
    }   
    m_all_cells.resize(cells_sz);
    return result;
    
}

bool theory_seq::explain_empty(expr_ref_vector& es, dependency*& dep) {
    while (!es.empty()) {
        expr* e = es.back();
        if (m_util.str.is_empty(e)) {
            es.pop_back();
            continue;
        }
        expr* a;
        if (m_rep.find1(e, a, dep)) {
            es.pop_back();
            m_util.str.get_concat(a, es);
            continue;
        }
        TRACE("seq", tout << "Could not set to empty: " << es << "\n";);
        return false;
    }
    return true;
}

bool theory_seq::simplify_and_solve_eqs() {
    context & ctx = get_context();
    m_new_propagation = false;
    m_new_solution = true;
    while (m_new_solution && !ctx.inconsistent()) {
        m_new_solution = false;
        solve_eqs(0);
    }
    return m_new_propagation || ctx.inconsistent();
}


bool theory_seq::internalize_term(app* term) {
    context & ctx   = get_context();
    if (ctx.e_internalized(term)) {
        enode* e = ctx.get_enode(term);
        mk_var(e);
        return true;
    }
    TRACE("seq_verbose", tout << mk_pp(term, m) << "\n";);
    unsigned num_args = term->get_num_args();
    expr* arg;
    for (unsigned i = 0; i < num_args; i++) {
        arg = term->get_arg(i);
        mk_var(ensure_enode(arg));
    }
    if (m.is_bool(term)) {
        bool_var bv = ctx.mk_bool_var(term);
        ctx.set_var_theory(bv, get_id());
        ctx.mark_as_relevant(bv);
    }

    enode* e = 0;
    if (ctx.e_internalized(term)) {
        e = ctx.get_enode(term);
    }
    else {
        e = ctx.mk_enode(term, false, m.is_bool(term), true);
    }
    mk_var(e);

    return true;
}

void theory_seq::add_length(expr* e) {
    SASSERT(!has_length(e));
    m_length.insert(e);
    m_trail_stack.push(insert_obj_trail<theory_seq, expr>(m_length, e));
}

/*
  ensure that all elements in equivalence class occur under an applicatin of 'length'
*/
void theory_seq::enforce_length(enode* n) {
    enode* n1 = n;
    do {
        expr* o = n->get_owner();
        if (!has_length(o)) {
            expr_ref len(m_util.str.mk_length(o), m);
            enque_axiom(len);
            add_length(o);
        }
        n = n->get_next();
    }
    while (n1 != n);
}

void theory_seq::apply_sort_cnstr(enode* n, sort* s) {
    mk_var(n);
}

void theory_seq::display(std::ostream & out) const {
    if (m_eqs.size() == 0 &&
        m_nqs.size() == 0 &&
        m_rep.empty() &&
        m_exclude.empty()) {
        return;
    }
    out << "Theory seq\n";
    if (m_eqs.size() > 0) {
        out << "Equations:\n";
        display_equations(out);
    }
    if (m_nqs.size() > 0) {
        display_disequations(out);
    }
    if (!m_re2aut.empty()) {
        out << "Regex\n";
        obj_map<expr, eautomaton*>::iterator it = m_re2aut.begin(), end = m_re2aut.end();
        for (; it != end; ++it) {
            out << mk_pp(it->m_key, m) << "\n";
            display_expr disp(m);
            if (it->m_value) {
                it->m_value->display(out, disp);
            }
        }
    }
    if (!m_rep.empty()) {
        out << "Solved equations:\n";
        m_rep.display(out);
    }
    if (!m_exclude.empty()) {
        out << "Exclusions:\n";
        m_exclude.display(out);
    }
}

void theory_seq::display_equations(std::ostream& out) const {
    for (unsigned i = 0; i < m_eqs.size(); ++i) {
        eq const& e = m_eqs[i];
        out << e.ls() << " = " << e.rs() << " <- \n";
        display_deps(out, e.dep());
    }
}

void theory_seq::display_disequations(std::ostream& out) const {
    bool first = true;
    for (unsigned i = 0; i < m_nqs.size(); ++i) {
        if (first) out << "Disequations:\n";
        first = false;
        display_disequation(out, m_nqs[i]);
    }
}

void theory_seq::display_disequation(std::ostream& out, ne const& e) const {
    for (unsigned j = 0; j < e.lits().size(); ++j) {
        out << e.lits(j) << " ";
    }
    if (e.lits().size() > 0) {
        out << "\n";
    }
    for (unsigned j = 0; j < e.ls().size(); ++j) {
        out << e.ls(j) << " != " << e.rs(j) << "\n";
    }
    if (e.dep()) {
        display_deps(out, e.dep());
    }
}

void theory_seq::display_deps(std::ostream& out, literal_vector const& lits, enode_pair_vector const& eqs) const {
    context& ctx = get_context();
    smt2_pp_environment_dbg env(m);
    params_ref p;
    for (unsigned i = 0; i < eqs.size(); ++i) {
        out << "  (= ";
        ast_smt2_pp(out, eqs[i].first->get_owner(), env, p, 5);
        out << "\n     ";
        ast_smt2_pp(out, eqs[i].second->get_owner(), env, p, 5);
        out << ")\n";
    }
    for (unsigned i = 0; i < lits.size(); ++i) {
        literal l = lits[i];        
        if (l == true_literal) {
            out << "   true";
        }
        else if (l == false_literal) {
            out << "   false";
        }
        else {
            expr* e = ctx.bool_var2expr(l.var());
            if (l.sign()) {
                ast_smt2_pp(out << "  (not ", e, env, p, 7);
                out << ")";
            }
            else {
                ast_smt2_pp(out << "  ", e, env, p, 2);
            }
        }
        out << "\n";
    }
}

void theory_seq::display_deps(std::ostream& out, dependency* dep) const {
    literal_vector lits;
    enode_pair_vector eqs;
    linearize(dep, eqs, lits);
    display_deps(out, lits, eqs);
}

void theory_seq::collect_statistics(::statistics & st) const {
    st.update("seq num splits", m_stats.m_num_splits);
    st.update("seq num reductions", m_stats.m_num_reductions);
    st.update("seq unfold def", m_stats.m_propagate_automata);
    st.update("seq length coherence", m_stats.m_check_length_coherence);
    st.update("seq branch", m_stats.m_branch_variable);
    st.update("seq solve !=", m_stats.m_solve_nqs);
    st.update("seq solve =", m_stats.m_solve_eqs);
    st.update("seq add axiom", m_stats.m_add_axiom);
    st.update("seq extensionality", m_stats.m_extensionality);
}

void theory_seq::init_model(expr_ref_vector const& es) {
    expr_ref new_s(m);
    for (unsigned i = 0; i < es.size(); ++i) {
        dependency* eqs = 0;
        expr_ref s = canonize(es[i], eqs);
        if (is_var(s)) {
            new_s = m_factory->get_fresh_value(m.get_sort(s));
            m_rep.update(s, new_s, eqs);
        }
    }
}

void theory_seq::init_model(model_generator & mg) {
    m_factory = alloc(seq_factory, get_manager(), get_family_id());
    mg.register_factory(m_factory);
    for (unsigned j = 0; j < m_nqs.size(); ++j) {
        ne const& n = m_nqs[j];
        for (unsigned i = 0; i < n.ls().size(); ++i) {
            init_model(n.ls(i));
            init_model(n.rs(i));
        }
    }
}


class theory_seq::seq_value_proc : public model_value_proc {
    theory_seq&                     th;
    sort*                           m_sort;
    svector<model_value_dependency> m_dependencies;
    ptr_vector<expr>                m_strings;
    svector<bool>                   m_source;
public:
    seq_value_proc(theory_seq& th, sort* s): th(th), m_sort(s) {
    }
    virtual ~seq_value_proc() {}
    void add_dependency(enode* n) { 
        m_dependencies.push_back(model_value_dependency(n)); 
        m_source.push_back(true);
    }
    void add_string(expr* n) {
        m_strings.push_back(n);
        m_source.push_back(false);
    }
    virtual void get_dependencies(buffer<model_value_dependency> & result) {
        result.append(m_dependencies.size(), m_dependencies.c_ptr());
    }
    virtual app * mk_value(model_generator & mg, ptr_vector<expr> & values) {
        SASSERT(values.size() == m_dependencies.size());
        expr_ref_vector args(th.m);
        unsigned j = 0, k = 0;
        bool is_string = th.m_util.is_string(m_sort);
        for (unsigned i = 0; i < m_source.size(); ++i) {
            if (m_source[i] && is_string) {
                bv_util bv(th.m);
                rational val;
                unsigned sz;
                VERIFY(bv.is_numeral(values[j++], val, sz));
                svector<bool> val_as_bits;
                unsigned v = val.get_unsigned();
                for (unsigned i = 0; i < sz; ++i) {
                    val_as_bits.push_back(1 == v % 2);
                    v = v / 2;
                }
                args.push_back(th.m_util.str.mk_string(zstring(sz, val_as_bits.c_ptr())));
            }
            else if (m_source[i]) {
                args.push_back(th.m_util.str.mk_unit(values[j++]));
            }
            else {
                args.push_back(m_strings[k++]);
            }
        }
        expr_ref result = th.mk_concat(args, m_sort);
        th.m_rewrite(result);
        th.m_factory->add_trail(result);
        return to_app(result);
    }
};


model_value_proc * theory_seq::mk_value(enode * n, model_generator & mg) {
    if (m_util.is_seq(n->get_owner())) {
        ptr_vector<expr> concats;
        get_concat(n->get_owner(), concats);
        context& ctx = get_context();
        sort* srt = m.get_sort(n->get_owner());
        seq_value_proc* sv = alloc(seq_value_proc, *this, srt);
        
        for (unsigned i = 0; i < concats.size(); ++i) {
            expr* c = concats[i], *c1;
            if (m_util.str.is_unit(c, c1)) {
                sv->add_dependency(ctx.get_enode(c1));
            }
            else if (m_util.str.is_string(c)) {
                sv->add_string(c);
            }
            else {
                sv->add_string(mk_value(to_app(c)));
            }
        }
        return sv;
    }
    else {
        return alloc(expr_wrapper_proc, mk_value(n->get_owner()));
    }
}


app* theory_seq::mk_value(app* e) {
    expr_ref result(m);
    result = m_rep.find(e);
    if (is_var(result)) {
        SASSERT(m_factory);
        expr_ref val(m);
        val = m_factory->get_some_value(m.get_sort(result));
        if (val) {
            result = val;
        }
    }
    else {
        m_rewrite(result);
    }
    m_factory->add_trail(result);
    TRACE("seq", tout << mk_pp(e, m) << " -> " << result << "\n";);
    m_rep.update(e, result, 0);
    return to_app(result);
}


theory_var theory_seq::mk_var(enode* n) {
    if (!m_util.is_seq(n->get_owner()) &&
        !m_util.is_re(n->get_owner())) {
        return null_theory_var;
    }
    if (is_attached_to_var(n)) {
        return n->get_th_var(get_id());
    }
    else {
        theory_var v = theory::mk_var(n);
        get_context().attach_th_var(n, this, v);
        get_context().mark_as_relevant(n);
        return v;
    }
}

bool theory_seq::can_propagate() {
    return m_axioms_head < m_axioms.size() || !m_replay.empty() || m_new_solution;
}

expr_ref theory_seq::canonize(expr* e, dependency*& eqs) {
    expr_ref result = expand(e, eqs);
    m_rewrite(result);
    return result;
}

bool theory_seq::canonize(expr* e, expr_ref_vector& es, dependency*& eqs) {
    expr* e1, *e2;
    expr_ref e3(e, m);
    bool change = false;
    while (true) {
        if (m_util.str.is_concat(e3, e1, e2)) {
            canonize(e1, es, eqs);
            e3 = e2;
            change = true;
        }
        else if (m_util.str.is_empty(e3)) {
            return true;
        }
        else {
            expr_ref e4 = expand(e3, eqs);
            change |= e4 != e3;
            m_util.str.get_concat(e4, es);
            break;
        }
    }
    return change;
}

bool theory_seq::canonize(expr_ref_vector const& es, expr_ref_vector& result, dependency*& eqs) {
    bool change = false;
    for (unsigned i = 0; i < es.size(); ++i) {
        change = canonize(es[i], result, eqs) || change;
        SASSERT(!m_util.str.is_concat(es[i]) || change);
    }
    return change;
}


expr_ref theory_seq::expand(expr* e0, dependency*& eqs) {
    expr_ref result(m);
    dependency* deps = 0;
    expr_dep ed;
    if (m_rep.find_cache(e0, ed)) {
        eqs = m_dm.mk_join(eqs, ed.second);
        result = ed.first;
        return result;
    }
    expr* e = m_rep.find(e0, deps);
    expr* e1, *e2;
    if (m_util.str.is_concat(e, e1, e2)) {
        result = mk_concat(expand(e1, deps), expand(e2, deps));
    }
    else if (m_util.str.is_empty(e) || m_util.str.is_string(e)) {
        result = e;
    }
    else if (m_util.str.is_prefix(e, e1, e2)) {
        result = m_util.str.mk_prefix(expand(e1, deps), expand(e2, deps));
    }
    else if (m_util.str.is_suffix(e, e1, e2)) {
        result = m_util.str.mk_suffix(expand(e1, deps), expand(e2, deps));
    }
    else if (m_util.str.is_contains(e, e1, e2)) {
        result = m_util.str.mk_contains(expand(e1, deps), expand(e2, deps));
    }
    else if (m_util.str.is_unit(e, e1)) {
        result = m_util.str.mk_unit(expand(e1, deps));
    }
    else {
        result = e;
    }
    if (result == e0) {
        deps = 0;
    }
    expr_dep edr(result, deps);
    m_rep.add_cache(e0, edr);
    eqs = m_dm.mk_join(eqs, deps);
    TRACE("seq_verbose", tout << mk_pp(e0, m) << " |--> " << result << "\n";
          if (eqs) display_deps(tout, eqs););
    return result;
}

void theory_seq::add_dependency(dependency*& dep, enode* a, enode* b) {
    if (a != b) {
        dep = m_dm.mk_join(dep, m_dm.mk_leaf(assumption(a, b)));
    }
}


void theory_seq::propagate() {
    context & ctx = get_context();
    while (m_axioms_head < m_axioms.size() && !ctx.inconsistent()) {
        expr_ref e(m);
        e = m_axioms[m_axioms_head].get();
        deque_axiom(e);
        ++m_axioms_head;
    }
    while (!m_replay.empty() && !ctx.inconsistent()) {
        TRACE("seq", tout << "replay at level: " << ctx.get_scope_level() << "\n";);
        apply* app = m_replay[m_replay.size() - 1];
        (*app)(*this);
        m_replay.pop_back();
    }
    if (m_new_solution) {
        simplify_and_solve_eqs();
        m_new_solution = false;
    }
}

void theory_seq::enque_axiom(expr* e) {
    TRACE("seq", tout << "add axioms for: " << mk_pp(e, m) << "\n";);
    if (!m_axiom_set.contains(e)) {
        m_axioms.push_back(e);
        m_axiom_set.insert(e);
        m_trail_stack.push(push_back_vector<theory_seq, expr_ref_vector>(m_axioms));
        m_trail_stack.push(insert_obj_trail<theory_seq, expr>(m_axiom_set, e));;
    }
}

void theory_seq::deque_axiom(expr* n) {
    if (m_util.str.is_length(n)) {
        add_length_axiom(n);
    }
    else if (m_util.str.is_empty(n) && !has_length(n) && !m_length.empty()) {
        enforce_length(get_context().get_enode(n));
    }
    else if (m_util.str.is_index(n)) {
        add_indexof_axiom(n);
    }
    else if (m_util.str.is_replace(n)) {
        add_replace_axiom(n);
    }
    else if (m_util.str.is_extract(n)) {
        add_extract_axiom(n);
    }
    else if (m_util.str.is_at(n)) {
        add_at_axiom(n);
    }
    else if (m_util.str.is_string(n)) {
        add_elim_string_axiom(n);
    }
}


/*
  encode that s is not contained in of xs1
  where s1 is all of s, except the last element.

  lit or s = "" or s = s1*(unit c)
  lit or s = "" or !contains(x*s1, s)
*/
void theory_seq::tightest_prefix(expr* s, expr* x, literal lit1, literal lit2) {
    expr_ref s1 = mk_first(s);
    expr_ref c  = mk_last(s);
    expr_ref s1c = mk_concat(s1, m_util.str.mk_unit(c));
    literal s_eq_emp = mk_eq_empty(s);
    add_axiom(s_eq_emp, mk_seq_eq(s, s1c));
    add_axiom(lit1, lit2, s_eq_emp, ~mk_literal(m_util.str.mk_contains(mk_concat(x, s1), s)));
}

/*
  // index of s in t starting at offset.

  let i = Index(t, s, offset):

  offset >= len(t) => i = -1

  offset fixed to 0:

  len(t) != 0 & !contains(t, s) => i = -1
  len(t) != 0 & contains(t, s) => t = xsy & i = len(x)
  len(t) != 0 & contains(t, s) & s != emp => tightest_prefix(x, s)

  offset not fixed:

  0 <= offset < len(t) => xy = t &
                          len(x) = offset &
                          (-1 = indexof(y, s, 0) => -1 = i) &
                          (indexof(y, s, 0) >= 0 => indexof(t, s, 0) + offset = i)

  if offset < 0
     under specified

  optional lemmas:
   (len(s) > len(t)  -> i = -1)
   (len(s) <= len(t) -> i <= len(t)-len(s))
*/
void theory_seq::add_indexof_axiom(expr* i) {
    expr* s, *t, *offset = 0;
    rational r;
    VERIFY(m_util.str.is_index(i, t, s) ||
           m_util.str.is_index(i, t, s, offset));
    expr_ref minus_one(m_autil.mk_int(-1), m);
    expr_ref zero(m_autil.mk_int(0), m);
    expr_ref xsy(m);


    if (!offset || (m_autil.is_numeral(offset, r) && r.is_zero())) {
        expr_ref x  = mk_skolem(m_indexof_left, t, s);
        expr_ref y  = mk_skolem(m_indexof_right, t, s);
        xsy         = mk_concat(x, s, y);
        expr_ref lenx(m_util.str.mk_length(x), m);
        literal cnt = mk_literal(m_util.str.mk_contains(t, s));
        literal s_eq_empty = mk_eq_empty(s);
        add_axiom(cnt,  mk_eq(i, minus_one, false));
        add_axiom(~s_eq_empty, mk_eq(i, zero, false));
        add_axiom(s_eq_empty, ~mk_eq_empty(t), mk_eq(i, minus_one, false));
        add_axiom(~cnt, s_eq_empty, mk_seq_eq(t, xsy));
        add_axiom(~cnt, s_eq_empty, mk_eq(i, lenx, false));
        tightest_prefix(s, x, ~cnt);
    }
    else {
        // offset >= len(t) => indexof(s, t, offset) = -1

        expr_ref len_t(m_util.str.mk_length(t), m);
        literal offset_ge_len = mk_literal(m_autil.mk_ge(mk_sub(offset, len_t), zero));
        add_axiom(offset_ge_len, mk_eq(i, minus_one, false));

        expr_ref x = mk_skolem(m_indexof_left, t, s, offset);
        expr_ref y = mk_skolem(m_indexof_right, t, s, offset);
        expr_ref indexof0(m_util.str.mk_index(y, s, zero), m);
        expr_ref offset_p_indexof0(m_autil.mk_add(offset, indexof0), m);
        literal offset_ge_0 = mk_literal(m_autil.mk_ge(offset, zero));

        // 0 <= offset & offset < len(t) => t = xy
        // 0 <= offset & offset < len(t) => len(x) = offset
        // 0 <= offset & offset < len(t) & -1 = indexof(y,s,0) = -1 => -1 = i
        // 0 <= offset & offset < len(t) & indexof(y,s,0) >= 0 = -1 =>
        //                  -1 = indexof(y,s,0) + offset = indexof(t, s, offset)

        add_axiom(~offset_ge_0, offset_ge_len, mk_seq_eq(t, mk_concat(x, y)));
        add_axiom(~offset_ge_0, offset_ge_len, mk_eq(m_util.str.mk_length(x), offset, false));
        add_axiom(~offset_ge_0, offset_ge_len,
                  ~mk_eq(indexof0, minus_one, false), mk_eq(i, minus_one, false));
        add_axiom(~offset_ge_0, offset_ge_len,
                  ~mk_literal(m_autil.mk_ge(indexof0, zero)),
                  mk_eq(offset_p_indexof0, i, false));
    }
}

/*
  let r = replace(a, s, t)

  (contains(a, s) -> tightest_prefix(s,xs))
  (contains(a, s) -> r = xty & a = xsy) &
  (!contains(a, s) -> r = a)

*/
void theory_seq::add_replace_axiom(expr* r) {
    expr* a, *s, *t;
    VERIFY(m_util.str.is_replace(r, a, s, t));
    expr_ref x  = mk_skolem(m_indexof_left, a, s);
    expr_ref y  = mk_skolem(m_indexof_right, a, s);
    expr_ref xty = mk_concat(x, t, y);
    expr_ref xsy = mk_concat(x, s, y);
    literal cnt = mk_literal(m_util.str.mk_contains(a ,s));
    add_axiom(cnt,  mk_seq_eq(r, a));
    add_axiom(~cnt, mk_seq_eq(a, xsy));
    add_axiom(~cnt, mk_seq_eq(r, xty));
    tightest_prefix(s, x, ~cnt);
}

void theory_seq::add_elim_string_axiom(expr* n) {
    zstring s;
    VERIFY(m_util.str.is_string(n, s));
    if (s.length() == 0) {
        return;
    }
    expr_ref result(m_util.str.mk_unit(m_util.str.mk_char(s, s.length()-1)), m);
    for (unsigned i = s.length()-1; i > 0; ) {
        --i;
        result = mk_concat(m_util.str.mk_unit(m_util.str.mk_char(s, i)), result);
    }
    add_axiom(mk_eq(n, result, false));
    m_rep.update(n, result, 0);
    m_new_solution = true;
}


/*
    let n = len(x)
    - len(a ++ b) = len(a) + len(b) if x = a ++ b
    - len(unit(u)) = 1              if x = unit(u)
    - len(str) = str.length()       if x = str
    - len(empty) = 0                if x = empty
    - len(x) >= 0                   otherwise
 */
void theory_seq::add_length_axiom(expr* n) {
    context& ctx = get_context();
    expr* x;
    VERIFY(m_util.str.is_length(n, x));
    if (m_util.str.is_concat(x) ||
        m_util.str.is_unit(x) ||
        m_util.str.is_empty(x) ||
        m_util.str.is_string(x)) {
        expr_ref len(n, m);
        m_rewrite(len);
        SASSERT(n != len);
        add_axiom(mk_eq(len, n, false));
        if (!ctx.at_base_level()) {
            m_trail_stack.push(push_replay(alloc(replay_axiom, m, n)));
        }
    }
    else {
        add_axiom(mk_literal(m_autil.mk_ge(n, m_autil.mk_int(0))));
        if (!ctx.at_base_level()) {
            m_trail_stack.push(push_replay(alloc(replay_axiom, m, n)));
        }
    }
}



void theory_seq::propagate_in_re(expr* n, bool is_true) {
    TRACE("seq", tout << mk_pp(n, m) << " <- " << (is_true?"true":"false") << "\n";);
    expr* e1, *e2;
    VERIFY(m_util.str.is_in_re(n, e1, e2));

    expr_ref tmp(n, m);
    m_rewrite(tmp);
    if (m.is_true(tmp)) {
        if (!is_true) {
            literal_vector lits;
            lits.push_back(mk_literal(n));
            set_conflict(0, lits);
        }
        return;
    }
    else if (m.is_false(tmp)) {
        if (is_true) {
            literal_vector lits;
            lits.push_back(~mk_literal(n));
            set_conflict(0, lits);
        }
        return;
    }

    eautomaton* a = get_automaton(e2);
    if (!a) return;

    context& ctx = get_context();

    expr_ref len(m_util.str.mk_length(e1), m);
    for (unsigned i = 0; i < a->num_states(); ++i) {
        literal acc = mk_accept(e1, len, e2, i);
        literal rej = mk_reject(e1, len, e2, i);
        add_axiom(a->is_final_state(i)?acc:~acc);
        add_axiom(a->is_final_state(i)?~rej:rej);
    }

    expr_ref zero(m_autil.mk_int(0), m);
    unsigned_vector states;
    a->get_epsilon_closure(a->init(), states);
    literal_vector lits;
    literal lit = ctx.get_literal(n);
    if (is_true) {
        lits.push_back(~lit);
    }
    for (unsigned i = 0; i < states.size(); ++i) {
        if (is_true) {
            lits.push_back(mk_accept(e1, zero, e2, states[i]));
        }
        else {
            literal nlit = ~lit;
            propagate_lit(0, 1, &nlit, mk_reject(e1, zero, e2, states[i]));
        }
    }
    if (is_true) {
        if (lits.size() == 2) {
            propagate_lit(0, 1, &lit, lits[1]);
        }
        else {
            TRACE("seq", ctx.display_literals_verbose(tout, lits.size(), lits.c_ptr()); tout << "\n";);
            ctx.mk_th_axiom(get_id(), lits.size(), lits.c_ptr());
        }
    }
}


expr_ref theory_seq::mk_sub(expr* a, expr* b) {
    expr_ref result(m_autil.mk_sub(a, b), m);
    m_rewrite(result);
    return result;
}

enode* theory_seq::ensure_enode(expr* e) {
    context& ctx = get_context();
    if (!ctx.e_internalized(e)) {
        ctx.internalize(e, false);
    }
    enode* n = ctx.get_enode(e);
    ctx.mark_as_relevant(n);
    return n;
}

static theory_mi_arith* get_th_arith(context& ctx, theory_id afid, expr* e) {
    theory* th = ctx.get_theory(afid);
    if (th && ctx.e_internalized(e)) {
        return dynamic_cast<theory_mi_arith*>(th);
    }
    else {
        return 0;
    }
}

bool theory_seq::lower_bound(expr* _e, rational& lo) {
    context& ctx = get_context();
    expr_ref e(m_util.str.mk_length(_e), m);
    theory_mi_arith* tha = get_th_arith(ctx, m_autil.get_family_id(), e);
    expr_ref _lo(m);
    if (!tha || !tha->get_lower(ctx.get_enode(e), _lo)) return false;
    return m_autil.is_numeral(_lo, lo) && lo.is_int();
}

bool theory_seq::upper_bound(expr* _e, rational& hi) {
    context& ctx = get_context();
    expr_ref e(m_util.str.mk_length(_e), m);
    theory_mi_arith* tha = get_th_arith(ctx, m_autil.get_family_id(), e);
    expr_ref _hi(m);
    if (!tha || !tha->get_upper(ctx.get_enode(e), _hi)) return false;
    return m_autil.is_numeral(_hi, hi) && hi.is_int();
}

bool theory_seq::get_length(expr* e, rational& val) {
    context& ctx = get_context();
    theory* th = ctx.get_theory(m_autil.get_family_id());
    if (!th) return false;
    theory_mi_arith* tha = dynamic_cast<theory_mi_arith*>(th);
    if (!tha) return false;
    rational val1;
    expr_ref len(m), len_val(m);
    expr* e1, *e2;
    ptr_vector<expr> todo;
    todo.push_back(e);
    val.reset();
    zstring s;
    while (!todo.empty()) {
        expr* c = todo.back();
        todo.pop_back();
        if (m_util.str.is_concat(c, e1, e2)) {
            todo.push_back(e1);
            todo.push_back(e2);
        }
        else if (m_util.str.is_unit(c)) {
            val += rational(1);
        }
        else if (m_util.str.is_empty(c)) {
            continue;
        }
        else if (m_util.str.is_string(c, s)) {
            val += rational(s.length());
        }
        else {
            len = m_util.str.mk_length(c);
            if (ctx.e_internalized(len) &&
                tha->get_value(ctx.get_enode(len), len_val) &&
                m_autil.is_numeral(len_val, val1)) {
                val += val1;
            }
            else {
                TRACE("seq", tout << "No length provided for " << len << "\n";);
                return false;
            }
        }
    }
    return val.is_int();
}

/*
  TBD: check semantics of extract.

  let e = extract(s, i, l)

  0 <= i <= len(s) -> prefix(xe,s) 
  0 <= i <= len(s) -> len(x) = i
  0 <= i <= len(s) & 0 <= l <= len(s) - i -> len(e) = l
  0 <= i <= len(s) & len(s) < l + i  -> len(e) = len(s) - i
  0 <= i <= len(s) & l < 0 -> len(e) = 0
  *  i < 0 -> e = empty
  *  i > len(s) -> e = empty

 

*/

void theory_seq::add_extract_axiom(expr* e) {
    expr* s, *i, *l;
    VERIFY(m_util.str.is_extract(e, s, i, l));
    if (is_tail(s, i, l)) {
        add_tail_axiom(e, s);
        return;
    }
    if (is_drop_last(s, i, l)) {
        add_drop_last_axiom(e, s);
        return;
    }
    if (is_extract_prefix0(s, i, l)) {
        add_extract_prefix_axiom(e, s, l);
        return;
    }
    if (is_extract_suffix(s, i, l)) {
        add_extract_suffix_axiom(e, s, i);
        return;
    }
    expr_ref x(mk_skolem(m_pre, s, i), m);
    expr_ref ls(m_util.str.mk_length(s), m);
    expr_ref lx(m_util.str.mk_length(x), m);
    expr_ref le(m_util.str.mk_length(e), m);
    expr_ref ls_minus_i_l(mk_sub(mk_sub(ls, i), l), m);
    expr_ref y(mk_skolem(m_post, s, ls_minus_i_l), m);
    expr_ref xe = mk_concat(x, e);
    expr_ref xey = mk_concat(x, e, y);
    expr_ref zero(m_autil.mk_int(0), m);

    literal i_ge_0    = mk_literal(m_autil.mk_ge(i, zero));
    literal i_le_ls   = mk_literal(m_autil.mk_le(mk_sub(i, ls), zero));
    literal li_ge_ls  = mk_literal(m_autil.mk_ge(ls_minus_i_l, zero));
    literal l_ge_zero = mk_literal(m_autil.mk_ge(l, zero));

//    add_axiom(~i_ge_0, ~i_le_ls, mk_literal(m_util.str.mk_prefix(xe, s)));
    add_axiom(~i_ge_0, ~i_le_ls, mk_seq_eq(xey, s));
    add_axiom(~i_ge_0, ~i_le_ls, mk_eq(lx, i, false));
    add_axiom(~i_ge_0, ~i_le_ls, ~l_ge_zero, ~li_ge_ls, mk_eq(le, l, false));
    add_axiom(~i_ge_0, ~i_le_ls, li_ge_ls, mk_eq(le, mk_sub(ls, i), false));
    add_axiom(~i_ge_0, ~i_le_ls, l_ge_zero, mk_eq(le, zero, false));
}

void theory_seq::add_tail_axiom(expr* e, expr* s) {
    expr_ref head(m), tail(m);
    mk_decompose(s, head, tail);
    add_axiom(mk_eq_empty(s), mk_seq_eq(s, mk_concat(head, e)));
}

void theory_seq::add_drop_last_axiom(expr* e, expr* s) {
    add_axiom(mk_eq_empty(s), mk_seq_eq(s, mk_concat(e, m_util.str.mk_unit(mk_last(s)))));
}

bool theory_seq::is_drop_last(expr* s, expr* i, expr* l) {
    rational i1;
    if (!m_autil.is_numeral(i, i1) || !i1.is_zero()) {
        return false;
    }
    expr_ref l2(m), l1(l, m);
    l2 = m_autil.mk_sub(m_util.str.mk_length(s), m_autil.mk_int(1));
    m_rewrite(l1);
    m_rewrite(l2);
    return l1 == l2;
}

bool theory_seq::is_tail(expr* s, expr* i, expr* l) {
    rational i1;
    if (!m_autil.is_numeral(i, i1) || !i1.is_one()) {
        return false;
    }
    expr_ref l2(m), l1(l, m);
    l2 = m_autil.mk_sub(m_util.str.mk_length(s), m_autil.mk_int(1));
    m_rewrite(l1);
    m_rewrite(l2);
    return l1 == l2;
}

bool theory_seq::is_extract_prefix0(expr* s, expr* i, expr* l) {
    rational i1;
    return m_autil.is_numeral(i, i1) && i1.is_zero();    
}

bool theory_seq::is_extract_suffix(expr* s, expr* i, expr* l) {
    expr_ref len(m_autil.mk_add(l, i), m);
    m_rewrite(len);
    return m_util.str.is_length(len, l) && l == s;
}

/*
  0 <= l <= len(s) => s = ey & l = len(e)
 */
void theory_seq::add_extract_prefix_axiom(expr* e, expr* s, expr* l) {
    expr_ref le(m_util.str.mk_length(e), m);
    expr_ref ls(m_util.str.mk_length(s), m);
    expr_ref ls_minus_l(mk_sub(ls, l), m);
    expr_ref y(mk_skolem(m_post, s, ls_minus_l), m);
    expr_ref zero(m_autil.mk_int(0), m);
    expr_ref ey = mk_concat(e, y);
    literal l_ge_0 = mk_literal(m_autil.mk_ge(l, zero));
    literal l_le_s = mk_literal(m_autil.mk_le(mk_sub(l, ls), zero));
    add_axiom(~l_ge_0, ~l_le_s, mk_seq_eq(s, ey));
    add_axiom(~l_ge_0, ~l_le_s, mk_eq(l, le, false));
    add_axiom(~l_ge_0, ~l_le_s, mk_eq(ls_minus_l, m_util.str.mk_length(y), false));
}

/*
  0 <= i <= len(s) => s = xe & i = len(x)    
 */
void theory_seq::add_extract_suffix_axiom(expr* e, expr* s, expr* i) {
    expr_ref x(mk_skolem(m_pre, s, i), m);
    expr_ref lx(m_util.str.mk_length(x), m);
    expr_ref ls(m_util.str.mk_length(s), m);
    expr_ref zero(m_autil.mk_int(0), m);
    expr_ref xe = mk_concat(x, e);
    literal i_ge_0 = mk_literal(m_autil.mk_ge(i, zero));
    literal i_le_s = mk_literal(m_autil.mk_le(mk_sub(i, ls), zero));
    add_axiom(~i_ge_0, ~i_le_s, mk_seq_eq(s, xe));
    add_axiom(~i_ge_0, ~i_le_s, mk_eq(i, lx, false));
}


/*
   let e = at(s, i)

   0 <= i < len(s) -> s = xey & len(x) = i & len(e) = 1

*/
void theory_seq::add_at_axiom(expr* e) {
    expr* s, *i;
    VERIFY(m_util.str.is_at(e, s, i));
    expr_ref len_e(m_util.str.mk_length(e), m);
    expr_ref len_s(m_util.str.mk_length(s), m);
    expr_ref zero(m_autil.mk_int(0), m);
    expr_ref one(m_autil.mk_int(1), m);
    expr_ref x = mk_skolem(m_pre, s, i);
    expr_ref y = mk_skolem(m_post, s, mk_sub(mk_sub(len_s, i), one));
    expr_ref xey   = mk_concat(x, e, y);
    expr_ref len_x(m_util.str.mk_length(x), m);

    literal i_ge_0 = mk_literal(m_autil.mk_ge(i, zero));
    literal i_ge_len_s = mk_literal(m_autil.mk_ge(mk_sub(i, m_util.str.mk_length(s)), zero));

    add_axiom(~i_ge_0, i_ge_len_s, mk_seq_eq(s, xey));
    add_axiom(~i_ge_0, i_ge_len_s, mk_eq(one, len_e, false));
    add_axiom(~i_ge_0, i_ge_len_s, mk_eq(i, len_x, false));
}

/**
   step(s, idx, re, i, j, t) -> nth(s, idx) == t & len(s) > idx
*/
void theory_seq::propagate_step(literal lit, expr* step) {
    context& ctx = get_context();
    SASSERT(ctx.get_assignment(lit) == l_true);
    expr* re, *acc, *s, *idx, *i, *j;
    VERIFY(is_step(step, s, idx, re, i, j, acc));
    TRACE("seq", tout << mk_pp(step, m) << " -> " << mk_pp(acc, m) << "\n";);
    propagate_lit(0, 1, &lit, mk_literal(acc));
    rational lo;
    rational _idx;
    if (lower_bound(s, lo) && lo.is_unsigned() && m_autil.is_numeral(idx, _idx) && lo >= _idx) {
        // skip
    }
    else {
        propagate_lit(0, 1, &lit, ~mk_literal(m_autil.mk_le(m_util.str.mk_length(s), idx)));
    }
    ensure_nth(lit, s, idx);
}

/*
    lit => s = (nth s 0) ++ (nth s 1) ++ ... ++ (nth s idx) ++ (tail s idx)
*/
void theory_seq::ensure_nth(literal lit, expr* s, expr* idx) {
    context& ctx = get_context();
    rational r;
    SASSERT(ctx.get_assignment(lit) == l_true);
    VERIFY(m_autil.is_numeral(idx, r) && r.is_unsigned());
    unsigned _idx = r.get_unsigned();
    expr_ref head(m), tail(m), conc(m), len1(m), len2(m);
    expr_ref_vector elems(m);

    expr* s2 = s;
    for (unsigned j = 0; j <= _idx; ++j) {
        mk_decompose(s2, head, tail);
        elems.push_back(head);
        len1 = m_util.str.mk_length(s2);
        len2 = m_autil.mk_add(m_autil.mk_int(1), m_util.str.mk_length(tail));
        propagate_eq(lit, len1, len2, false);
        s2 = tail;
    }
    elems.push_back(s2);
    conc = mk_concat(elems, m.get_sort(s));
    propagate_eq(lit, s, conc, true);
}

literal theory_seq::mk_literal(expr* _e) {
    expr_ref e(_e, m);
    context& ctx = get_context();
    ensure_enode(e);
    return ctx.get_literal(e);
}


literal theory_seq::mk_seq_eq(expr* a, expr* b) {
    SASSERT(m_util.is_seq(a));
    return mk_literal(mk_skolem(m_eq, a, b, 0, m.mk_bool_sort()));
}

literal theory_seq::mk_eq_empty(expr* _e) {
    expr_ref e(_e, m);
    SASSERT(m_util.is_seq(e));
    expr_ref emp(m);
    zstring s;
    if (m_util.str.is_empty(e)) {
        return true_literal;
    }
    expr_ref_vector concats(m);
    m_util.str.get_concat(e, concats);
    for (unsigned i = 0; i < concats.size(); ++i) {
        if (m_util.str.is_unit(concats[i].get())) {
            return false_literal;
        }
        if (m_util.str.is_string(concats[i].get(), s) && s.length() > 0) {
            return false_literal;
        }
    }
    emp = m_util.str.mk_empty(m.get_sort(e));


    literal lit = mk_eq(e, emp, false);
    get_context().force_phase(lit);
    return lit;
}

void theory_seq::add_axiom(literal l1, literal l2, literal l3, literal l4, literal l5) {
    context& ctx = get_context();
    literal_vector lits;
    if (l1 == true_literal || l2 == true_literal || l3 == true_literal || l4 == true_literal || l5 == true_literal) return;
    if (l1 != null_literal && l1 != false_literal) { ctx.mark_as_relevant(l1); lits.push_back(l1); }
    if (l2 != null_literal && l2 != false_literal) { ctx.mark_as_relevant(l2); lits.push_back(l2); }
    if (l3 != null_literal && l3 != false_literal) { ctx.mark_as_relevant(l3); lits.push_back(l3); }
    if (l4 != null_literal && l4 != false_literal) { ctx.mark_as_relevant(l4); lits.push_back(l4); }
    if (l5 != null_literal && l5 != false_literal) { ctx.mark_as_relevant(l5); lits.push_back(l5); }
    TRACE("seq", ctx.display_literals_verbose(tout << "axiom: ", lits.size(), lits.c_ptr()); tout << "\n";);
    m_new_propagation = true;
    ++m_stats.m_add_axiom;
    ctx.mk_th_axiom(get_id(), lits.size(), lits.c_ptr());
}


expr_ref theory_seq::mk_skolem(symbol const& name, expr* e1,
                               expr* e2, expr* e3, sort* range) {
    expr* es[3] = { e1, e2, e3 };
    unsigned len = e3?3:(e2?2:1);
    if (!range) {
        range = m.get_sort(e1);
    }
    return expr_ref(m_util.mk_skolem(name, len, es, range), m);
}

bool theory_seq::is_skolem(symbol const& s, expr* e) const {
    return m_util.is_skolem(e) && to_app(e)->get_decl()->get_parameter(0).get_symbol() == s;
}

void theory_seq::propagate_eq(literal lit, expr* e1, expr* e2, bool add_to_eqs) {
    literal_vector lits;
    lits.push_back(lit);
    propagate_eq(0, lits, e1, e2, add_to_eqs);
}

void theory_seq::propagate_eq(dependency* deps, literal_vector const& _lits, expr* e1, expr* e2, bool add_to_eqs) {
    context& ctx = get_context();

    enode* n1 = ensure_enode(e1);
    enode* n2 = ensure_enode(e2);
    if (n1->get_root() == n2->get_root()) {
        return;
    }
    ctx.mark_as_relevant(n1);
    ctx.mark_as_relevant(n2);
    
    literal_vector lits(_lits);
    enode_pair_vector eqs;
    linearize(deps, eqs, lits);

    if (add_to_eqs) {
        for (unsigned i = 0; i < _lits.size(); ++i) {
            literal lit = _lits[i];
            SASSERT(l_true == ctx.get_assignment(lit));
            deps = m_dm.mk_join(deps, m_dm.mk_leaf(assumption(lit)));
        }
        new_eq_eh(deps, n1, n2);
    }
    TRACE("seq",
          ctx.display_literals_verbose(tout, lits.size(), lits.c_ptr());
          tout << " => " << mk_pp(e1, m) << " = " << mk_pp(e2, m) << "\n";);
    justification* js =
        ctx.mk_justification(
            ext_theory_eq_propagation_justification(
                get_id(), ctx.get_region(), lits.size(), lits.c_ptr(), eqs.size(), eqs.c_ptr(), n1, n2));

    m_new_propagation = true;
    ctx.assign_eq(n1, n2, eq_justification(js));
}


void theory_seq::assign_eh(bool_var v, bool is_true) {
    context & ctx = get_context();
    expr* e = ctx.bool_var2expr(v);
    expr* e1, *e2;
    expr_ref f(m);
    bool change = false;
    literal lit(v, !is_true);

    if (m_util.str.is_prefix(e, e1, e2)) {
        if (is_true) {
            f = mk_skolem(m_prefix, e1, e2);
            f = mk_concat(e1, f);
            propagate_eq(lit, f, e2, true);
        }
        else {
#if 0
            propagate_not_prefix(e);
#else
            propagate_non_empty(lit, e1);
            if (add_prefix2prefix(e, change)) {
                add_atom(e);
            }
#endif
        }
    }
    else if (m_util.str.is_suffix(e, e1, e2)) {
        if (is_true) {
            f = mk_skolem(m_suffix, e1, e2);
            f = mk_concat(f, e1);
            propagate_eq(lit, f, e2, true);
        }
        else {
#if 1
            propagate_not_suffix(e);

#else
            // lit => e1 != empty
            propagate_non_empty(lit, e1);

            // lit => e1 = first ++ (unit last)
            expr_ref f1 = mk_first(e1);
            expr_ref f2 = mk_last(e1);
            f = mk_concat(f1, m_util.str.mk_unit(f2));
            propagate_eq(lit, e1, f, true);

            TRACE("seq", tout << "suffix: " << f << " = " << mk_pp(e1, m) << "\n";);
            if (add_suffix2suffix(e, change)) {
                add_atom(e);
            }
#endif
        }
    }
    else if (m_util.str.is_contains(e, e1, e2)) {
        if (is_true) {
            expr_ref f1 = mk_skolem(m_contains_left, e1, e2);
            expr_ref f2 = mk_skolem(m_contains_right, e1, e2);
            f = mk_concat(f1, e2, f2);
            propagate_eq(lit, f, e1, true);
        }
        else if (!canonizes(false, e)) {
            propagate_non_empty(lit, e2);
            propagate_lit(0, 1, &lit, ~mk_literal(m_util.str.mk_prefix(e2, e1)));
            if (add_contains2contains(e, change)) {
                add_atom(e);
            }
        }
    }
    else if (is_accept(e)) {
        if (is_true) {
            propagate_acc_rej_length(lit, e);
            if (add_accept2step(e, change)) {
                add_atom(e);
            }
        }
    }
    else if (is_reject(e)) {
        if (is_true) {
            propagate_acc_rej_length(lit, e);
            add_atom(e);
        }
    }
    else if (is_step(e)) {
        if (is_true) {
            propagate_step(lit, e);
            if (add_step2accept(e, change)) {
                add_atom(e);
            }
        }
    }
    else if (is_eq(e, e1, e2)) {
        if (is_true) {
            propagate_eq(lit, e1, e2, true);
        }
    }
    else if (m_util.str.is_in_re(e)) {
        propagate_in_re(e, is_true);
    }
    else {
        UNREACHABLE();
    }
}

void theory_seq::add_atom(expr* e) {
    m_trail_stack.push(push_back_vector<theory_seq, ptr_vector<expr> >(m_atoms));
    m_atoms.push_back(e);
}

void theory_seq::new_eq_eh(theory_var v1, theory_var v2) {
    enode* n1 = get_enode(v1);
    enode* n2 = get_enode(v2);
    dependency* deps = m_dm.mk_leaf(assumption(n1, n2));
    new_eq_eh(deps, n1, n2);
}

void theory_seq::new_eq_eh(dependency* deps, enode* n1, enode* n2) {
    if (n1 != n2 && m_util.is_seq(n1->get_owner())) {
        expr_ref o1(n1->get_owner(), m);
        expr_ref o2(n2->get_owner(), m);
        TRACE("seq", tout << o1 << " = " << o2 << "\n";);
        m_eqs.push_back(mk_eqdep(o1, o2, deps));
        solve_eqs(m_eqs.size()-1);
        enforce_length_coherence(n1, n2);
    }
}

void theory_seq::new_diseq_eh(theory_var v1, theory_var v2) {
    enode* n1 = get_enode(v1);
    enode* n2 = get_enode(v2);
    expr_ref e1(n1->get_owner(), m);
    expr_ref e2(n2->get_owner(), m);
    m_exclude.update(e1, e2);
    expr_ref eq(m.mk_eq(e1, e2), m);
    m_rewrite(eq);
    if (!m.is_false(eq)) {
        TRACE("seq", tout << "new disequality: " << eq << "\n";);

        literal lit = mk_eq(e1, e2, false);

        // propagate x != "" into x = (++ (unit (nth x 0) (tail x 0)))
        if (m_util.str.is_empty(e2)) {
            std::swap(e1, e2);
        }
        if (false && m_util.str.is_empty(e1)) {
            expr_ref head(m), tail(m), conc(m);
            mk_decompose(e2, head, tail);
            conc = mk_concat(head, tail);
            propagate_eq(~lit, e2, conc, true);
        }
#if 0
        // (e1 = "" & e2 = xdz) or (e2 = "" & e1 = xcy) or (e1 = xcy & e2 = xdz & c != d) or (e1 = x & e2 = xdz) or (e2 = x & e1 = xcy)
        // e1 = "" or e1 = xcy or e1 = x
        // e2 = "" or e2 = xdz or e2 = x
        // e1 = xcy or e2 = xdz
        // c != d

        literal lit = mk_seq_eq(e1, e2);
        sort* char_sort = 0;
        expr_ref emp(m);
        VERIFY(m_util.is_seq(m.get_sort(e1), char_sort));
        emp = m_util.str.mk_empty(m.get_sort(e1));

        expr_ref x = mk_skolem(symbol("seq.ne.x"), e1, e2);
        expr_ref y = mk_skolem(symbol("seq.ne.y"), e1, e2);
        expr_ref z = mk_skolem(symbol("seq.ne.z"), e1, e2);
        expr_ref c = mk_skolem(symbol("seq.ne.c"), e1, e2, 0, char_sort);
        expr_ref d = mk_skolem(symbol("seq.ne.d"), e1, e2, 0, char_sort);
        literal e1_is_emp = mk_seq_eq(e1, emp);
        literal e2_is_emp = mk_seq_eq(e2, emp);
        literal e1_is_xcy = mk_seq_eq(e1, mk_concat(x, m_util.str.mk_unit(c), y));
        literal e2_is_xdz = mk_seq_eq(e2, mk_concat(x, m_util.str.mk_unit(d), z));
        add_axiom(lit, e1_is_emp, e1_is_xcy, mk_seq_eq(e1, x));
        add_axiom(lit, e2_is_emp, e2_is_xdz, mk_seq_eq(e2, x));
        add_axiom(lit, e1_is_xcy, e2_is_xdz);
        add_axiom(lit, ~mk_eq(c, d, false));
#else
        else {
            dependency* dep = m_dm.mk_leaf(assumption(~lit));
            m_nqs.push_back(ne(e1, e2, dep));
            solve_nqs(m_nqs.size() - 1);
        }
#endif
    }
}

void theory_seq::push_scope_eh() {
    theory::push_scope_eh();
    m_rep.push_scope();
    m_exclude.push_scope();
    m_dm.push_scope();
    m_trail_stack.push_scope();
    m_trail_stack.push(value_trail<theory_seq, unsigned>(m_axioms_head));
    m_eqs.push_scope();
    m_nqs.push_scope();
    m_atoms_lim.push_back(m_atoms.size());
}

void theory_seq::pop_scope_eh(unsigned num_scopes) {
    context& ctx = get_context();
    m_trail_stack.pop_scope(num_scopes);
    theory::pop_scope_eh(num_scopes);
    m_dm.pop_scope(num_scopes);
    m_rep.pop_scope(num_scopes);
    m_exclude.pop_scope(num_scopes);
    m_eqs.pop_scope(num_scopes);
    m_nqs.pop_scope(num_scopes);
    m_atoms.resize(m_atoms_lim[m_atoms_lim.size()-num_scopes]);
    m_atoms_lim.shrink(m_atoms_lim.size()-num_scopes);
    m_rewrite.reset();    
    if (ctx.get_base_level() > ctx.get_scope_level() - num_scopes) {
        m_replay.reset();
    }
}

void theory_seq::restart_eh() {
}

void theory_seq::relevant_eh(app* n) {
    if (m_util.str.is_index(n)   ||
        m_util.str.is_replace(n) ||
        m_util.str.is_extract(n) ||
        m_util.str.is_at(n) ||
        m_util.str.is_empty(n) ||
        m_util.str.is_string(n)) {
        enque_axiom(n);
    }

    expr* arg;
    if (m_util.str.is_length(n, arg) && !has_length(arg)) {
        enforce_length(get_context().get_enode(arg));
    }
}


eautomaton* theory_seq::get_automaton(expr* re) {
    eautomaton* result = 0;
    if (m_re2aut.find(re, result)) {
        return result;
    }
    result = m_mk_aut(re);
    if (result) {
        display_expr disp(m);
        TRACE("seq", result->display(tout, disp););
    }
    m_automata.push_back(result);
    m_trail_stack.push(push_back_vector<theory_seq, scoped_ptr_vector<eautomaton> >(m_automata));

    m_re2aut.insert(re, result);
    m_trail_stack.push(insert_obj_map<theory_seq, expr, eautomaton*>(m_re2aut, re));
    return result;
}

literal theory_seq::mk_accept(expr* s, expr* idx, expr* re, expr* state) {
    expr_ref_vector args(m);
    args.push_back(s).push_back(idx).push_back(re).push_back(state);
    return mk_literal(m_util.mk_skolem(m_accept, args.size(), args.c_ptr(), m.mk_bool_sort()));
}
literal theory_seq::mk_reject(expr* s, expr* idx, expr* re, expr* state) {
    expr_ref_vector args(m);
    args.push_back(s).push_back(idx).push_back(re).push_back(state);
    return mk_literal(m_util.mk_skolem(m_reject, args.size(), args.c_ptr(), m.mk_bool_sort()));
}

bool theory_seq::is_acc_rej(symbol const& ar, expr* e, expr*& s, expr*& idx, expr*& re, unsigned& i, eautomaton*& aut) {
    if (is_skolem(ar, e)) {
        rational r;
        s  = to_app(e)->get_arg(0);
        idx = to_app(e)->get_arg(1);
        re = to_app(e)->get_arg(2);
        TRACE("seq", tout << mk_pp(re, m) << "\n";);
        VERIFY(m_autil.is_numeral(to_app(e)->get_arg(3), r));
        SASSERT(r.is_unsigned());
        i = r.get_unsigned();
        aut = get_automaton(re);
        return true;
    }
    else {
        return false;
    }
}

bool theory_seq::is_step(expr* e) const {
    return is_skolem(m_aut_step, e);
}

bool theory_seq::is_step(expr* e, expr*& s, expr*& idx, expr*& re, expr*& i, expr*& j, expr*& t) const {
    if (is_step(e)) {
        s    = to_app(e)->get_arg(0);
        idx  = to_app(e)->get_arg(1);
        re   = to_app(e)->get_arg(2);
        i    = to_app(e)->get_arg(3);
        j    = to_app(e)->get_arg(4);
        t    = to_app(e)->get_arg(5);
        return true;
    }
    else {
        return false;
    }
}

expr_ref theory_seq::mk_step(expr* s, expr* idx, expr* re, unsigned i, unsigned j, expr* acc) {
    SASSERT(m.is_bool(acc));
    expr_ref_vector args(m);
    args.push_back(s).push_back(idx).push_back(re);
    args.push_back(m_autil.mk_int(i));
    args.push_back(m_autil.mk_int(j));
    args.push_back(acc);
    return expr_ref(m_util.mk_skolem(m_aut_step, args.size(), args.c_ptr(), m.mk_bool_sort()), m);
}

/*
   acc(s, idx, re, i) -> len(s) >= idx    if i is final
   rej(s, idx, re, i) -> len(s) >= idx    if i is non-final

   acc(s, idx, re, i) -> len(s) > idx     if i is non-final
   rej(s, idx, re, i) -> len(s) > idx     if i is final
*/
void theory_seq::propagate_acc_rej_length(literal lit, expr* e) {
    context& ctx = get_context();
    expr *s, * idx, *re;
    unsigned src;
    eautomaton* aut = 0;
    bool is_acc;
    is_acc = is_accept(e, s, idx, re, src, aut);
    if (!is_acc) {
        VERIFY(is_reject(e, s, idx, re, src, aut));
    }
    if (m_util.str.is_length(idx)) return;
    SASSERT(m_autil.is_numeral(idx));
    SASSERT(ctx.get_assignment(lit) == l_true);
    bool is_final = aut->is_final_state(src);
    if (is_final == is_acc) {
        propagate_lit(0, 1, &lit, mk_literal(m_autil.mk_ge(m_util.str.mk_length(s), idx)));
    }
    else {
        propagate_lit(0, 1, &lit, ~mk_literal(m_autil.mk_le(m_util.str.mk_length(s), idx)));
    }
}

/**
   acc(s, idx, re, i) ->  \/ step(s, idx, re, i, j, t)                if i is non-final
   acc(s, idx, re, i) -> len(s) <= idx \/ step(s, idx, re, i, j, t)   if i is final
*/
bool theory_seq::add_accept2step(expr* acc, bool& change) {
    context& ctx = get_context();

    TRACE("seq", tout << mk_pp(acc, m) << "\n";);
    SASSERT(ctx.get_assignment(acc) == l_true);
    expr *e, * idx, *re;
    expr_ref step(m);
    unsigned src;
    eautomaton* aut = 0;
    VERIFY(is_accept(acc, e, idx, re, src, aut));
    if (!aut || m_util.str.is_length(idx)) {
        return false;
    }
    SASSERT(m_autil.is_numeral(idx));
    eautomaton::moves mvs;
    aut->get_moves_from(src, mvs);

    expr_ref len(m_util.str.mk_length(e), m);
    literal_vector lits;
    lits.push_back(~ctx.get_literal(acc));
    if (aut->is_final_state(src)) {
        lits.push_back(mk_literal(m_autil.mk_le(len, idx)));
        switch (ctx.get_assignment(lits.back())) {
        case l_true:            
            return false;
        case l_undef:
            change = true;
            ctx.force_phase(lits.back());
            return true;
        default:
            break;
        }
    }
    bool has_undef = false;
    int start = ctx.get_random_value();
    for (unsigned i = 0; i < mvs.size(); ++i) {
        unsigned j = (i + start) % mvs.size();
        eautomaton::move mv = mvs[j];
        expr_ref nth = mk_nth(e, idx);
        expr_ref acc = mv.t()->accept(nth);
        step = mk_step(e, idx, re, src, mv.dst(), acc);
        lits.push_back(mk_literal(step));
        switch (ctx.get_assignment(lits.back())) {
        case l_true:
            return false;
        case l_undef:
            //ctx.force_phase(lits.back());
            //return true;
            has_undef = true;
            break;
        default:
            break;
        }
    }
    change = true;
    if (has_undef && mvs.size() == 1) {
        literal lit = lits.back();
        lits.pop_back();
        for (unsigned i = 0; i < lits.size(); ++i) {
            lits[i].neg();
        }
        propagate_lit(0, lits.size(), lits.c_ptr(), lit);
        return false;
    }
    if (has_undef) {
        return true;
    }
    TRACE("seq", ctx.display_literals_verbose(tout, lits.size(), lits.c_ptr()); tout << "\n";);
    for (unsigned i = 0; i < lits.size(); ++i) {
        SASSERT(ctx.get_assignment(lits[i]) == l_false);
        lits[i].neg();
    }
    set_conflict(0, lits);
    return false;
}


/**
   acc(s, idx, re, i) & step(s, idx, re, i, j, t) => acc(s, idx + 1, re, j)
*/

bool theory_seq::add_step2accept(expr* step, bool& change) {
    context& ctx = get_context();
    SASSERT(ctx.get_assignment(step) == l_true);
    expr* re, *_acc, *s, *idx, *i, *j;
    VERIFY(is_step(step, s, idx, re, i, j, _acc));
    literal acc1 = mk_accept(s, idx,  re, i);
    switch (ctx.get_assignment(acc1)) {
    case l_false:
        break;
    case l_undef:
        change = true;
        return true;
    case l_true: {
        change = true;
        rational r;
        VERIFY(m_autil.is_numeral(idx, r) && r.is_unsigned());
        expr_ref idx1(m_autil.mk_int(r.get_unsigned() + 1), m);
        literal acc2 = mk_accept(s, idx1, re, j);
        literal_vector lits;
        lits.push_back(acc1);
        lits.push_back(ctx.get_literal(step));
        lits.push_back(~acc2);
        switch (ctx.get_assignment(acc2)) {
        case l_undef:
            propagate_lit(0, 2, lits.c_ptr(), acc2);
            break;
        case l_true:
            break;
        case l_false:
            set_conflict(0, lits);
            break;
        }
        break;
    }
    }
    return false;
}


/*
   rej(s, idx, re, i) & nth(s, idx) = t & idx < len(s) => rej(s, idx + 1, re, j)

   len(s) > idx -> s = (nth 0 s) ++ .. ++ (nth idx s) ++ (tail idx s)

Recall we also have:
   rej(s, idx, re, i) -> len(s) >= idx    if i is non-final
   rej(s, idx, re, i) -> len(s) > idx     if i is final

*/
bool theory_seq::add_reject2reject(expr* rej, bool& change) {
    context& ctx = get_context();
    SASSERT(ctx.get_assignment(rej) == l_true);
    expr* s, *idx, *re;
    unsigned src;
    rational r;
    eautomaton* aut = 0;
    VERIFY(is_reject(rej, s, idx, re, src, aut));
    if (!aut || m_util.str.is_length(idx)) return false;
    VERIFY(m_autil.is_numeral(idx, r) && r.is_unsigned());
    expr_ref idx1(m_autil.mk_int(r.get_unsigned() + 1), m);
    eautomaton::moves mvs;
    aut->get_moves_from(src, mvs);
    literal rej1 = ctx.get_literal(rej);
    expr_ref len(m_util.str.mk_length(s), m);
    literal len_le_idx = mk_literal(m_autil.mk_le(len, idx));
    switch (ctx.get_assignment(len_le_idx)) {
    case l_true:
        return false;
    case l_undef:
        ctx.force_phase(len_le_idx);       
        return true;
    default:
        break;
    }
    expr_ref nth = mk_nth(s, idx);
    ensure_nth(~len_le_idx, s, idx);
    literal_vector eqs;
    bool has_undef = false;
    for (unsigned i = 0; i < mvs.size(); ++i) {
        eautomaton::move const& mv = mvs[i];
        literal eq = mk_literal(mv.t()->accept(nth));
        switch (ctx.get_assignment(eq)) {
        case l_false:
        case l_true:
            break;
        case l_undef:
            ctx.force_phase(~eq);
            has_undef = true;
            break;
        }
        eqs.push_back(eq);
    }
    change = true;
    if (has_undef) {
        return true;
    }
    for (unsigned i = 0; i < mvs.size(); ++i) {
        eautomaton::move const& mv = mvs[i];
        literal eq = eqs[i];
        if (ctx.get_assignment(eq) == l_true) {
            literal rej2 = mk_reject(s, idx1, re, m_autil.mk_int(mv.dst()));
            add_axiom(~rej1, ~eq, len_le_idx, rej2);
        }
    }
    return false;
}

/*
  !prefix(e1,e2) => e1 != ""
  !prefix(e1,e2) => e2 = "" or e1 = xcy & (e2 = xdz & c != d or x = e2)
*/

void theory_seq::propagate_not_prefix(expr* e) {
    context& ctx = get_context();
    expr* e1, *e2;
    VERIFY(m_util.str.is_prefix(e, e1, e2));
    literal lit = ctx.get_literal(e);
    SASSERT(ctx.get_assignment(lit) == l_false);
    if (canonizes(false, e)) {
        return;
    }
    propagate_non_empty(~lit, e1);
    expr_ref emp(m_util.str.mk_empty(m.get_sort(e1)), m);
    literal e2_is_emp = mk_seq_eq(e2, emp);
    sort* char_sort = 0;
    VERIFY(m_util.is_seq(m.get_sort(e1), char_sort));
    expr_ref x = mk_skolem(symbol("seq.prefix.x"), e1, e2);
    expr_ref y = mk_skolem(symbol("seq.prefix.y"), e1, e2);
    expr_ref z = mk_skolem(symbol("seq.prefix.z"), e1, e2);
    expr_ref c = mk_skolem(symbol("seq.prefix.c"), e1, e2, 0, char_sort);
    expr_ref d = mk_skolem(symbol("seq.prefix.d"), e1, e2, 0, char_sort);
    add_axiom(lit, e2_is_emp, mk_seq_eq(e1, mk_concat(x, m_util.str.mk_unit(c), y)));
    add_axiom(lit, e2_is_emp, mk_seq_eq(e2, mk_concat(x, m_util.str.mk_unit(d), z)), mk_seq_eq(e2, x));
    add_axiom(lit, e2_is_emp, ~mk_eq(c, d, false), mk_seq_eq(e2, x));
}

/*
  !suffix(e1,e2) => e1 != ""
  !suffix(e1,e2) => e2 = "" or e1 = ycx & (e2 = zdx & c != d or x = e2)
 */


void theory_seq::propagate_not_suffix(expr* e) {
    context& ctx = get_context();
    expr* e1, *e2;
    VERIFY(m_util.str.is_suffix(e, e1, e2));
    literal lit = ctx.get_literal(e);
    SASSERT(ctx.get_assignment(lit) == l_false);
    if (canonizes(false, e)) {
        return;
    }
    propagate_non_empty(~lit, e1);
    
    expr_ref emp(m_util.str.mk_empty(m.get_sort(e1)), m);
    literal e2_is_emp = mk_seq_eq(e2, emp);
    sort* char_sort = 0;
    VERIFY(m_util.is_seq(m.get_sort(e1), char_sort));
    expr_ref x = mk_skolem(symbol("seq.suffix.x"), e1, e2);
    expr_ref y = mk_skolem(symbol("seq.suffix.y"), e1, e2);
    expr_ref z = mk_skolem(symbol("seq.suffix.z"), e1, e2);
    expr_ref c = mk_skolem(symbol("seq.suffix.c"), e1, e2, 0, char_sort);
    expr_ref d = mk_skolem(symbol("seq.suffix.d"), e1, e2, 0, char_sort);
    add_axiom(lit, e2_is_emp, mk_seq_eq(e1, mk_concat(y, m_util.str.mk_unit(c), x)));
    add_axiom(lit, e2_is_emp, mk_seq_eq(e2, mk_concat(z, m_util.str.mk_unit(d), x)), mk_seq_eq(e2, x));
    add_axiom(lit, e2_is_emp, ~mk_eq(c, d, false), mk_seq_eq(e2, x));
}


/*
  !prefix -> e2 = emp \/ nth(e1,0) != nth(e2,0) \/ !prefix(tail(e1),tail(e2))
*/
bool theory_seq::add_prefix2prefix(expr* e, bool& change) {
    context& ctx = get_context();
    expr* e1, *e2;
    VERIFY(m_util.str.is_prefix(e, e1, e2));
    SASSERT(ctx.get_assignment(e) == l_false);
    if (canonizes(false, e)) {
        return false;
    }
    expr_ref head1(m), tail1(m), head2(m), tail2(m), conc(m);

    TRACE("seq", tout << mk_pp(e, m) << "\n";);

    literal e2_is_emp = mk_eq_empty(e2);
    switch (ctx.get_assignment(e2_is_emp)) {
    case l_true:
        TRACE("seq", tout << mk_pp(e2, m) << " = empty\n";);
        return false; // done
    case l_undef:
        // ctx.force_phase(e2_is_emp);
        TRACE("seq", tout << mk_pp(e2, m) << " ~ empty\n";);
        return true;  // retry
    default:
        break;
    }

    mk_decompose(e2, head2, tail2);
    conc = mk_concat(head2, tail2);
    propagate_eq(~e2_is_emp, e2, conc, true);

    literal e1_is_emp = mk_eq_empty(e1);
    switch (ctx.get_assignment(e1_is_emp)) {
    case l_true:        
        TRACE("seq", tout << mk_pp(e1, m) << " = empty\n";);
        return false; // done
    case l_undef:        
        TRACE("seq", tout << mk_pp(e1, m) << " ~ empty\n";);
        return true;  // retry
    default:
        break;
    }

    mk_decompose(e1, head1, tail1);
    conc = mk_concat(head1, tail1);
    propagate_eq(~e1_is_emp, e1, conc, true);


    literal lit = mk_eq(head1, head2, false);
    switch (ctx.get_assignment(lit)) {
    case l_true: 
        break;
    case l_false:
        TRACE("seq", tout << head1 << " = " << head2 << "\n";);
        return false;
    case l_undef:
        ctx.force_phase(~lit);
        TRACE("seq", tout << head1 << " ~ " << head2 << "\n";);
        return true;
    }
    change = true;
    literal_vector lits;
    lits.push_back(~ctx.get_literal(e));
    lits.push_back(~e2_is_emp);
    lits.push_back(lit);
    propagate_lit(0, lits.size(), lits.c_ptr(), ~mk_literal(m_util.str.mk_prefix(tail1, tail2)));
    TRACE("seq", tout << "saturate: " << tail1 << " = " << tail2 << "\n";);
    return false;
}

/*
  !suffix(e1, e2) -> e2 = emp \/ last(e1) != last(e2) \/ !suffix(first(e1), first(e2))
 */
bool theory_seq::add_suffix2suffix(expr* e, bool& change) {
    context& ctx = get_context();
    expr* e1, *e2;
    VERIFY(m_util.str.is_suffix(e, e1, e2));
    SASSERT(ctx.get_assignment(e) == l_false);
    if (canonizes(false, e)) {
        return false;
    }

    literal e2_is_emp = mk_eq_empty(e2);
    switch (ctx.get_assignment(e2_is_emp)) {
    case l_true:
        return false; // done
    case l_undef:        
        ctx.force_phase(e2_is_emp);
        return true;  // retry
    case l_false:
        break;
    }
    expr_ref first2 = mk_first(e2);
    expr_ref last2  = mk_last(e2);
    expr_ref conc2 = mk_concat(first2, m_util.str.mk_unit(last2));
    propagate_eq(~e2_is_emp, e2, conc2, true);

    literal e1_is_emp = mk_eq_empty(e1);
    switch (ctx.get_assignment(e1_is_emp)) {
    case l_true:
        return false; // done
    case l_undef:
        ctx.force_phase(e1_is_emp);
        return true;  // retry
    case l_false:
        break;
    }
    expr_ref first1 = mk_first(e1);
    expr_ref last1  = mk_last(e1);
    expr_ref conc1 = mk_concat(first1, m_util.str.mk_unit(last1));
    propagate_eq(~e1_is_emp, e1, conc1, true);


    literal last_eq = mk_eq(last1, last2, false);
    switch (ctx.get_assignment(last_eq)) {
    case l_false:
        return false; // done
    case l_undef:
        ctx.force_phase(~last_eq);
        return true;
    case l_true:
        break;
    }

    change = true;
    literal_vector lits;
    lits.push_back(~ctx.get_literal(e));
    lits.push_back(~e2_is_emp);
    lits.push_back(last_eq);
    propagate_lit(0, lits.size(), lits.c_ptr(), ~mk_literal(m_util.str.mk_suffix(first1, first2)));
    TRACE("seq", tout << mk_pp(e, m) << " saturate\n";);
    return false;
}

bool theory_seq::canonizes(bool sign, expr* e) {
    context& ctx = get_context();
    dependency* deps = 0;
    expr_ref cont = canonize(e, deps);
    TRACE("seq", tout << mk_pp(e, m) << " -> " << cont << "\n";);
    if ((m.is_true(cont) && !sign) ||
        (m.is_false(cont) && sign)) {
        propagate_lit(deps, 0, 0, ctx.get_literal(e));
        return true;
    }
    if ((m.is_false(cont) && !sign) ||
        (m.is_true(cont) && sign)) {
        return true;
    }
    return false;
}

/*
   !contains(e1, e2) -> !prefix(e2, e1)
   !contains(e1, e2) -> e1 = emp \/ !contains(tail(e1), e2)
 */

bool theory_seq::add_contains2contains(expr* e, bool& change) {
    context& ctx = get_context();
    expr* e1, *e2;
    VERIFY(m_util.str.is_contains(e, e1, e2));
    SASSERT(ctx.get_assignment(e) == l_false);
    if (canonizes(false, e)) {
        return false;
    }
    
    literal e1_is_emp = mk_eq_empty(e1);
    switch (ctx.get_assignment(e1_is_emp)) {
    case l_true:
        return false; // done
    case l_undef:
        ctx.force_phase(e1_is_emp);
        return true;  // retry
    default:
        break;
    }
    change = true;
    expr_ref head(m), tail(m), conc(m);
    mk_decompose(e1, head, tail);
    
    conc = mk_concat(head, tail);
    propagate_eq(~e1_is_emp, e1, conc, true);

    literal lits[2] = { ~ctx.get_literal(e), ~e1_is_emp };
    propagate_lit(0, 2, lits, ~mk_literal(m_util.str.mk_contains(tail, e2)));
    return false;
}

bool theory_seq::propagate_automata() {
    context& ctx = get_context();
    if (m_atoms_qhead == m_atoms.size()) {
        return false;
    }
    m_trail_stack.push(value_trail<theory_seq, unsigned>(m_atoms_qhead));
    ptr_vector<expr> re_add;
    bool change = false;
    while (m_atoms_qhead < m_atoms.size() && !ctx.inconsistent()) {
        expr* e = m_atoms[m_atoms_qhead];
        TRACE("seq", tout << mk_pp(e, m) << "\n";);
        bool reQ = false;
        if (is_accept(e)) {
            reQ = add_accept2step(e, change);
        }
        else if (is_reject(e)) {
            reQ = add_reject2reject(e, change);
        }
        else if (is_step(e)) {
            reQ = add_step2accept(e, change);
        }
        else if (m_util.str.is_prefix(e)) {
            reQ = add_prefix2prefix(e, change);
        }
        else if (m_util.str.is_suffix(e)) {
            reQ = add_suffix2suffix(e, change);
        }
        else if (m_util.str.is_contains(e)) {
            reQ = add_contains2contains(e, change);
        }
        if (reQ) {
            re_add.push_back(e);
            change = true;
        }
        ++m_atoms_qhead;
    }
    m_atoms.append(re_add);
    return change || get_context().inconsistent();
}

void theory_seq::get_concat(expr* e, ptr_vector<expr>& concats) {
    expr* e1, *e2;
    while (true) {
        e = m_rep.find(e);
        if (m_util.str.is_concat(e, e1, e2)) {
            get_concat(e1, concats);
            e = e2;
            continue;
        }
        concats.push_back(e);        
        return;
    }
}