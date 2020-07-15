/** @file
 * Feature Expression.
 *
 * Copyright 2020, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string_view>
#include <variant>

#include <swoc/bwf_base.h>

#include "txn_box/Modifier.h"
#include "txn_box/Extractor.h"

/// Parsed feature expression.
class Expr {
  using self_type = Expr; ///< Self reference type.
  using Spec = Extractor::Spec; ///< Import for convenience.
public:
  /// Output generator for BWF on an expression.
  class bwf_ex {
  public:
    /// Construct with specifier sequence.
    bwf_ex(std::vector<Spec> const& specs) : _specs(specs), _iter(specs.begin()) {}

    /// Validity check.
    explicit operator bool() const { return _iter != _specs.end(); }
    ///
    bool operator()(std::string_view& literal, Spec & spec);
  protected:
    std::vector<Spec> const& _specs; ///< Specifiers in format.
    std::vector<Spec>::const_iterator _iter; ///< Current specifier.
  };

  /// Single extractor that generates a direct view.
  struct Direct {
    Direct(Spec const& spec, ActiveType rtype) : _spec(spec), _result_type(rtype) {}
    Spec _spec; ///< Specifier with extractor.
    ActiveType _result_type = STRING; ///< Type of view, default is a string.
  };

  /// A composite of extractors and literals.
  /// Always a string.
  struct Composite {
    /// Specifiers / elements of the parsed format string.
    std::vector<Spec> _specs;
  };

  struct List {
    /// Expressions which are the elements of the tuple.
    std::vector<self_type> _exprs;
    ActiveType _types; ///< Types of the expressions.
  };

  /// Concrete types for a specific expression.
  std::variant<std::monostate, Feature, Direct, Composite, List> _expr;
  /// Enumerations for type indices.
  enum {
    /// No value, uninitialized.
    NO_EXPR = 0,
    /// Literal value, stored in a Feature. No extraction needed.
    LITERAL = 1,
    /// A single extractor, directly accessed to get a Feature.
    DIRECT = 2,
    /// String value composed of multiple literals and/or extractors.
    COMPOSITE = 3,
    /// Nested expression - this expression is a sequence of other expressions.
    LIST = 4
  };

  /// Contains an extractor that references context data.
  bool _ctx_ref_p = false;
  ///< Largest argument index. -1 => no numbered arguments.
  int _max_arg_idx = -1;

  /// Post extraction modifiers.
  std::vector<Modifier::Handle> _mods;

  Expr() = default;
  Expr(self_type const& that) = delete;
  Expr(self_type && that) = default;
  self_type & operator = (self_type const& that) = delete;
  self_type & operator = (self_type && that) = default;

  /** Construct from a Feature.
   *
   * @param f Feature that is the result of the expression.
   *
   * The constructed instance will always be the literal @a f.
   */
  Expr(Feature const& f) : _expr(f) {}
  Expr(Direct && d) : _expr(std::move(d)) {}
  Expr(Composite && comp) : _expr(std::move(comp)) {}

  Expr(Spec const& spec, ActiveType t) {
    _expr.emplace<DIRECT>(spec, t);
    _max_arg_idx = spec._idx;
  }

  ActiveType result_type() const {
    struct Visitor {
      ActiveType operator () (std::monostate const&) { return {}; }
      ActiveType operator () (Feature const& f) { return f.active_type(); }
      ActiveType operator () (Direct const& d) { return d._result_type; }
      ActiveType operator () (Composite const&) { return STRING; }
      ActiveType operator () (List const& l) { return ActiveType{ActiveType::TupleOf(l._types.base_types())}; }
    };
    ActiveType zret = std::visit(Visitor{}, _expr);
    for ( auto const& mod : _mods) {
      zret = mod->result_type(zret);
    }
    return zret;
  }

  bool is_literal() const { return _expr.index() == LITERAL; }

  struct bwf_visitor {
    bwf_visitor(Context & ctx) : _ctx(ctx) {}

    Feature operator () (std::monostate const&) { return NIL_FEATURE; }
    Feature operator () (Feature const& f) { return f; }
    Feature operator () (Direct const& d) { return d._spec._exf->extract(_ctx, d._spec); }
    Feature operator () (Composite const& comp);
    Feature operator () (List const& list);

    Context& _ctx;
  };
};
