/** @file
   Configuration implementation.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include <string>
#include <map>
#include <numeric>
#include <getopt.h>

#include <swoc/TextView.h>
#include <swoc/swoc_file.h>
#include <swoc/bwf_std.h>
#include <yaml-cpp/yaml.h>

#include "txn_box/Directive.h"
#include "txn_box/Extractor.h"
#include "txn_box/Modifier.h"
#include "txn_box/Expr.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"
#include "txn_box/yaml_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
using namespace swoc::literals;

static constexpr char ARG_PREFIX = '<';
static constexpr char ARG_SUFFIX = '>';

/* ------------------------------------------------------------------------------------ */
swoc::Lexicon<Hook> HookName {{ {Hook::CREQ, {"read-request", "creq"}}
                              , {Hook::PREQ, {"send-request", "preq"}}
                              , {Hook::URSP, {"read-response", "ursp"}}
                              , {Hook::PRSP, {"send-response", "prsp"}}
                              , {Hook::PRE_REMAP, {"pre-remap"}}
                              , {Hook::POST_REMAP, {"post-remap"}}
                              , {Hook::REMAP, {"remap"}}
                              }};

std::array<TSHttpHookID, std::tuple_size<Hook>::value> TS_Hook;

BufferWriter& bwformat(BufferWriter& w, bwf::Spec const& spec, Hook hook) {
  if (spec.has_numeric_type()) {
    return bwformat(w, spec, IndexFor(hook));
  }
  return bwformat(w, spec, HookName[hook]);
}

namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  HookName.set_default(Hook::INVALID);

  TS_Hook[IndexFor(Hook::CREQ)] = TS_HTTP_READ_REQUEST_HDR_HOOK;
  TS_Hook[IndexFor(Hook::PREQ)] = TS_HTTP_SEND_REQUEST_HDR_HOOK;
  TS_Hook[IndexFor(Hook::URSP)] = TS_HTTP_READ_RESPONSE_HDR_HOOK;
  TS_Hook[IndexFor(Hook::PRSP)] = TS_HTTP_SEND_RESPONSE_HDR_HOOK;
  TS_Hook[IndexFor(Hook::PRE_REMAP)] = TS_HTTP_PRE_REMAP_HOOK;
  TS_Hook[IndexFor(Hook::POST_REMAP)] = TS_HTTP_POST_REMAP_HOOK;

  return true;
} ();
}; // namespace


Config::Factory Config::_factory;

template < typename F > struct scope_exit {
  F _f;
  explicit scope_exit(F &&f) : _f(std::move(f)) {}
  ~scope_exit() { _f(); }
};

swoc::Rv<swoc::TextView> parse_arg(TextView& key) {
  TextView arg{key};
  TextView name { arg.take_prefix_at(ARG_PREFIX) };
  if (name.size() == key.size()) { // no arg prefix, it's just the name.
    return {};
  }
  if (arg.empty() || arg.back() != ARG_SUFFIX) {
    return Error(R"(Argument for "{}" is not properly terminated with '{}'.)", name, ARG_SUFFIX);
  }
  key = name;
  return arg.remove_suffix(1);
}

/* ------------------------------------------------------------------------------------ */
Config::Config() {
  _drtv_info.resize(Directive::StaticInfo::_counter + 1);

}

Config::self_type &Config::localize(Feature &feature) {
  std::visit([&](auto & t) { this->localize(t); }, static_cast<Feature::variant_type&>(feature));
  return *this;
}

std::string_view & Config::localize(std::string_view & text) {
  auto span { _arena.alloc(text.size()).rebind<char>() };
  memcpy(span, text);
  return text = span.view();
};

FeatureNodeStyle Config::feature_node_style(YAML::Node value) {
  if (value.IsScalar()) {
    return FeatureNodeStyle::SINGLE;
  }
  if (value.IsSequence()) {
    if (value.size() == 0) {
      return FeatureNodeStyle::SINGLE;
    }
  }
  return FeatureNodeStyle::INVALID;
}

Rv<ValueType> Config::validate(Extractor::Spec &spec) {
  if (spec._name.empty()) {
    return Error(R"(Extractor name required but not found.)");
  }

  if (spec._idx < 0) {
    auto name = TextView{spec._name};
    auto && [ arg, arg_errata ] { parse_arg(name) };
    if (!arg_errata.is_ok()) {
      return std::move(arg_errata);
    }

    if (auto ex{Extractor::find(name)}; nullptr != ex) {
      spec._exf = ex;
      spec._name = this->localize(name);
      spec._ext = this->localize(spec._ext);
      auto && [ vt, errata ] { ex->validate(*this, spec, arg) };
      if (! errata.is_ok()) {
        return std::move(errata);
      }
      return vt;
    }
    return Error(R"(Extractor "{}" not found.)", name);
  }
  return STRING; // non-negative index => capture group => always a string
}

Rv<Expr> Config::parse_unquoted_expr(swoc::TextView const& text) {
  // Integer?
  TextView parsed;
  auto n = swoc::svtoi(text, &parsed);
  if (parsed.size() == text.size()) {
    return Expr{FeatureView::Literal(text)};
  }

  // bool?
  auto b = BoolNames[text];
  if (b != BoolTag::INVALID) {
    return Expr{Feature{b == BoolTag::True}};
  }

  // IP Address?
  swoc::IPAddr addr;
  if (addr.load(text)) {
    return Expr{Feature{addr}};
  }

  // Presume an extractor.
  Extractor::Spec spec;
  bool valid_p = spec.parse(text);
  if (!valid_p) {
    return Error(R"(Invalid syntax for extractor "{}" - not a valid specifier.)", text);
  }
  auto && [ vt, errata ] = this->validate(spec);
  if (! errata.is_ok()) {
    return std::move(errata);
  }

  return Expr{spec, vt};
}

Rv<Expr> Config::parse_composite_expr(TextView const& text) {
  ValueType single_vt;
  auto parser { swoc::bwf::Format::bind(text) };
  std::vector<Extractor::Spec> specs;
  // Used to handle literals in @a format_string. Can't be const because it must be updated
  // for each literal.
  Extractor::Spec literal_spec;

  literal_spec._type = Extractor::Spec::LITERAL_TYPE;

  while (parser) {
    Extractor::Spec spec;
    std::string_view literal;
    bool spec_p = parser(literal, spec);

    if (!literal.empty()) {
      literal_spec._ext = this->localize(literal);
      specs.push_back(literal_spec);
    }

    if (spec_p) {
      if (spec._idx >= 0) {
        specs.push_back(spec);
      } else {
        auto && [vt, errata] = this->validate(spec);
        if (errata.is_ok()) {
          single_vt = vt; // Save for singleton case.
          specs.push_back(spec);
        } else {
          errata.info(R"(While parsing specifier at offset {}.)", text.size() - parser._fmt.size());
          return std::move(errata);
        }
      }
    }
  }

  // If it is a singleton, return it as one of the singleton types.
  if (specs.size() == 1) {
    if (specs[0]._exf) {
      return Expr{specs[0], single_vt};
    } else if (specs[0]._type == Extractor::Spec::LITERAL_TYPE) {
      return Expr{FeatureView(this->localize(specs[0]._ext))};
    } else {
      return Error("Internal consistency error - specifier is neither an extractor nor a literal.");
    }
  }
  // Multiple specifiers, check for overall properties.
  Expr expr;
  auto & cexpr = expr._expr.emplace<Expr::COMPOSITE>();
  cexpr._specs = std::move(specs);
  for ( auto const& s : specs ) {
    expr._max_arg_idx = std::max(expr._max_arg_idx, s._idx);
    if (s._exf) {
      expr._ctx_ref_p = expr._ctx_ref_p || s._exf->has_ctx_ref();
    }
  }

  return std::move(expr);
}

Rv<Expr> Config::parse_scalar_expr(YAML::Node node) {
  Rv<Expr> zret;
  TextView text { node.Scalar() };
  if (text.empty()) { // no value at all
    return Expr{};
  } else if (node.Tag() == "?"_tv) { // unquoted, must be extractor.
    zret = this->parse_unquoted_expr(text);
  } else {
    zret = this->parse_composite_expr(text);
  }

  if (zret.is_ok()) {
    auto & expr = zret.result();
    if (expr._max_arg_idx >= 0) {
      if (!_rxp_group_state || _rxp_group_state->_rxp_group_count == 0) {
        return Error(R"(Regular expression capture group used at {} but no regular expression is active.)", node.Mark());
      } else if (expr._max_arg_idx >= _rxp_group_state->_rxp_group_count) {
        return Error(R"(Regular expression capture group {} used at {} but the maximum capture group is {} in the active regular expression from line {}.)"
            , expr._max_arg_idx, node.Mark(), _rxp_group_state->_rxp_group_count-1, _rxp_group_state->_rxp_line);
      }
    }

    if (expr._ctx_ref_p && _feature_state && _feature_state->_feature_ref_p) {
      _feature_state->_feature_ref_p = true;
    }
  }
  return std::move(zret);
}

Rv<Expr> Config::parse_expr_with_mods(YAML::Node node) {
  auto && [ expr, expr_errata ] { this->parse_expr(node[0])};
  if (! expr_errata.is_ok()) {
    expr_errata.info("While processing the expression at {}.", node.Mark());
    return std::move(expr_errata);
  }

  for ( unsigned idx = 1 ; idx < node.size() ; ++idx ) {
    auto child { node[idx] };
    auto && [ mod, mod_errata ] {Modifier::load(*this, child, expr.result_type()) };
    if (! mod_errata.is_ok()) {
      mod_errata.info(R"(While parsing feature expression at {}.)", child.Mark(), node.Mark());
      return std::move(mod_errata);
    }
    if (_feature_state) {
      _feature_state->_type = mod->result_type(_feature_state->_type);
    }
    expr._mods.emplace_back(std::move(mod));
  }

  return std::move(expr);
}

Rv<Expr> Config::parse_expr(YAML::Node expr_node) {
  std::string_view expr_tag(expr_node.Tag());

  // This is the base entry method, so it needs to handle all cases, although most of them
  // will be delegated. Handle the direct / simple special cases here.

  if (expr_node.IsNull()) { // explicit NULL
    return Expr{NIL_FEATURE};
  }

  // If explicitly marked a literal, then no further processing should be done.
  if (0 == strcasecmp(expr_tag, LITERAL_TAG)) {
    if (!expr_node.IsScalar()) {
      return Error(R"("!{}" tag used on value at {} which is not a string as required for a literal.)", LITERAL_TAG, expr_node.Mark());
    }
    return Expr{FeatureView::Literal(this->localize(expr_node.Scalar()))};
  } else if (0 != strcasecmp(expr_tag, "?"_sv) && 0 != strcasecmp(expr_tag, "!"_sv)) {
    return Error(R"("{}" tag for extractor expression is not supported.)", expr_tag);
  }

  if (expr_node.IsScalar()) {
    return this->parse_scalar_expr(expr_node);
  }
  if (! expr_node.IsSequence()) {
    return Error("Feature expression is not properly structured.");
  }

  // It's a sequence, handle the various cases.
  if (expr_node.size() == 0) {
    return Expr{NIL_FEATURE};
  }
  if (expr_node.size() == 1) {
    return this->parse_scalar_expr(expr_node[0]);
  }

  if (expr_node[1].IsMap()) { // base expression with modifiers.
    return this->parse_expr_with_mods(expr_node);
  }

  // Else, after all this, it's a tuple, treat each element as an expression.
  std::vector<Expr> xa;
  xa.reserve(expr_node.size());
  for ( auto const& child : expr_node ) {
    auto && [ expr , errata ] { this->parse_expr(child) };
    if (errata.is_ok()) {
      xa.emplace_back(std::move(expr));
    } else {
      errata.info("While parsing feature expression list at {}.", expr_node.Mark());
      return std::move(errata);
    }
  }

  Expr expr;
  auto & list = expr._expr.emplace<Expr::LIST>();
  list._exprs.reserve(xa.size());
  for ( auto && x : xa) {
    list._exprs.emplace_back(std::move(x));
  }
  return std::move(expr);
}

Rv<Directive::Handle> Config::load_directive(YAML::Node const& drtv_node)
{
  YAML::Node key_node;
  for ( auto const&  [ key_name, key_value ] : drtv_node ) {
    TextView name { key_name.Scalar() };
    auto && [ arg, arg_errata ] { parse_arg(name) };
    if (!arg_errata.is_ok()) {
      return std::move(arg_errata);
    }

    // Ignorable keys in the directive. Currently just one, so hand code it. Make this better
    // if there is ever more than one.
    if (name == Global::DO_KEY) {
      continue;
    }
    // See if this is in the factory. It's not an error if it's not, to enable adding extra
    // keys to directives. First key that is in the factory determines the directive type.
    // If none of the keys are in the factory, that's an error and is reported after the loop.
    if ( auto spot { _factory.find(name) } ; spot != _factory.end()) {
      auto const& [ hooks, worker, static_info ] { spot->second };
      if (! hooks[IndexFor(this->current_hook())]) {
        return Error(R"(Directive "{}" at {} is not allowed on hook "{}".)", name, drtv_node.Mark(), this->current_hook());
      }
      auto && [ drtv, drtv_errata ] { worker(*this, drtv_node, name, arg, key_value) };
      if (! drtv_errata.is_ok()) {
        drtv_errata.info(R"(While parsing directive at {}.)", drtv_node.Mark());
        return std::move(drtv_errata);
      }
      // Fill in config dependent data and pass a pointer to it to the directive instance.
      auto & rtti = _drtv_info[static_info._idx];
      if (++rtti._count == 1) { // first time this directive type has been used.
        rtti._idx = static_info._idx;
        rtti._cfg_span = _arena.alloc(static_info._cfg_storage_required);
        rtti._ctx_storage_offset = _ctx_storage_required;
        _ctx_storage_required += static_info._ctx_storage_required;
      }
      drtv->_rtti = &rtti;

      return std::move(drtv);
    }
  }
  return Error(R"(Directive at {} has no recognized tag.)", drtv_node.Mark());
}

Rv<Directive::Handle> Config::parse_directive(YAML::Node const& drtv_node) {
  if (drtv_node.IsMap()) {
    return this->load_directive(drtv_node);
  } else if (drtv_node.IsSequence()) {
    Errata zret;
    auto list { new DirectiveList };
    Directive::Handle drtv_list{list};
    for (auto child : drtv_node) {
      auto && [handle, errata] {this->load_directive(child)};
      if (errata.is_ok()) {
        list->push_back(std::move(handle));
      } else {
        errata.info(R"(While loading directives at {}.)", drtv_node.Mark());
        return std::move(errata);
      }
    }
    return std::move(drtv_list);
  } else if (drtv_node.IsNull()) {
    return Directive::Handle(new NilDirective);
  }
  return Error(R"(Directive at {} is not an object or a sequence as required.)", drtv_node.Mark());
}

// Basically a wrapper for @c load_directive to handle stacking feature provisioning. During load,
// all paths must be explored and so the active feature needs to be stacked up so it can be restored
// after a tree descent. During runtime, only one path is followed and therefore this isn't
// required. This is used only when parsing directives in branching directives, such as
// @c with / @c select
Rv<Directive::Handle>
Config::parse_directive(YAML::Node const& drtv_node, FeatureRefState &state) {
  // Set up state preservation.
  auto saved_feature = _feature_state;
  auto saved_rxp = _rxp_group_state;
  if (state._feature_active_p) {
    _feature_state = &state;
  }
  if (state._rxp_group_count > 0) {
    _rxp_group_state = &state;
  }
  scope_exit cleanup([&]() {
    _feature_state = saved_feature;
    _rxp_group_state = saved_rxp;
  });
  // Now do normal parsing.
  return this->parse_directive(drtv_node);
}

Errata Config::load_top_level_directive(YAML::Node drtv_node) {
  if (drtv_node.IsMap()) {
    YAML::Node key_node { drtv_node[When::KEY] };
    if (key_node) {
      auto &&[handle, errata] {When::load(*this, drtv_node, When::KEY, {}, key_node)};
      if (errata.is_ok()) {
        auto when = static_cast<When*>(handle.get());
        // Steal the directive out of the When.
        _roots[IndexFor(when->_hook)].emplace_back(std::move(when->_directive));
        _has_top_level_directive_p = true;
      } else {
        return std::move(errata);
      }
    } else {
      return Error(R"(Top level directive at {} is not a "when" directive as required.)", drtv_node.Mark());
    }
  } else {
    return Error(R"(Top level directive at {} is not an object as required.)", drtv_node.Mark());
  }
  return {};
}

Errata Config::load_remap_directive(YAML::Node drtv_node) {
  if (drtv_node.IsMap()) {
    auto &&[drtv, errata]{this->parse_directive(drtv_node)};
    if (errata.is_ok()) {
      // Don't unpack @c when - should not be that common and therefore better to defer them to the
      // context callbacks, avoids having to cart around the remap rule config to call them later.
      _roots[IndexFor(Hook::REMAP)].emplace_back(std::move(drtv));
      _has_top_level_directive_p = true;
    } else {
      return std::move(errata);
    }
  } else {
    return Error(R"(Configuration at {} is not a directive object as required.)", drtv_node.Mark());
  }
  return {};
}

Errata Config::parse_yaml(YAML::Node const& root, TextView path, Hook hook) {
  YAML::Node base_node { root };
  static constexpr TextView ROOT_PATH { "." };
  // Walk the key path and find the target. If the path is the special marker for ROOT_PATH
  // do not walk at all.
  for ( auto p = (path == ROOT_PATH ? TextView{} : path) ; p ; ) {
    auto key { p.take_prefix_at('.') };
    if ( auto node { base_node[key] } ; node ) {
      base_node = node;
    } else {
      return Error(R"(Key "{}" not found - no such key "{}".)", path, path.prefix(path.size() - p.size()).rtrim('.'));
    }
  }

  Errata errata;

  // Special case remap loading.
  auto drtv_loader = &self_type::load_top_level_directive; // global loader.
  if (hook == Hook::REMAP) {
    _hook = Hook::REMAP;
    drtv_loader = &self_type::load_remap_directive;
  }

  if (base_node.IsSequence()) {
    for ( auto const& child : base_node ) {
      errata.note((this->*drtv_loader)(child));
    }
    if (! errata.is_ok()) {
      errata.info(R"(While loading list of top level directives for "{}" at {}.)", path, base_node.Mark());
    }
  } else if (base_node.IsMap()) {
    errata = (this->*drtv_loader)(base_node);
  } else {
  }
  return std::move(errata);
};

Errata Config::define(swoc::TextView name, HookMask const& hooks, Directive::Worker const &worker, Directive::Options const& opts) {
  auto & record { _factory[name] };
  std::get<0>(record) = hooks;
  std::get<1>(record) = worker;
  auto & info { std::get<2>(record) };
  info._idx = ++Directive::StaticInfo::_counter;
  info._cfg_storage_required = opts._cfg_size;
  info._ctx_storage_required = opts._ctx_size;
  return {};
}
