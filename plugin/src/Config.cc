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
#include "txn_box/FeatureMod.h"
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

Config& Config::localize(Extractor::Format &fmt) {
  if (fmt._literal_p) {
    // Special case a "pure" literal - it's a format but all of the specifiers are literals.
    // This can be consolidated into a "normal" STRING literal for the format. This is only way
    // @a _literal_p can be set and specifiers in the @a fmt.
    if (fmt.size() >= 1) {
      size_t n = std::accumulate(fmt._specs.begin(), fmt._specs.end(), size_t{0}, [](size_t sum
                                                                                     , Extractor::Spec const &spec) -> size_t { return sum += spec._ext.size(); });
      if (fmt._force_c_string_p) {
        ++n;
      }

      auto span{_arena.alloc(n).rebind<char>()};
      fmt._literal = span.view();
      for (auto const &spec : fmt._specs) {
        memcpy(span.data(), spec._ext.data(), spec._ext.size());
        span.remove_prefix(spec._ext.size());
      }
      if (fmt._force_c_string_p) {
        span[0] = '\0';
      }
      fmt._force_c_string_p = false; // Already took care of this, don't do it again.
      fmt._specs.clear();
    } else {
      this->localize(fmt._literal);
    }
  } else {
    // Localize and update the names and extensions.
    for (auto &spec : fmt._specs) {
      if (! spec._name.empty()) {
        this->localize(spec._name);
      }
      if (! spec._ext.empty()) {
        this->localize(spec._ext);
      }
    }
  }
  return *this;
}

Rv<Extractor::Format> Config::parse_feature(YAML::Node fmt_node, StrType str_type) {
  // Unfortunately, lots of special cases here.

  // If explicitly marked a literal, then no further processing should be done.
  if (0 == strcasecmp(fmt_node.Tag(), LITERAL_TAG)) {
    if (!fmt_node.IsScalar()) {
      return Error(R"("!{}" tag used on value at {} which is not a string as required for a literal.)", LITERAL_TAG, fmt_node.Mark());
    }
    auto const& text = fmt_node.Scalar();
    if (StrType::C == str_type) {
      return Extractor::literal(TextView{text.c_str(), text.size() + 1});
    }
    return Extractor::literal(TextView{text.data(), text.size()});

  }

  if (fmt_node.IsNull()) {
    // Empty / missing feature is treated as the empty string.
    return Extractor::literal(feature_type_for<NIL>{}); // Treat as equivalent of the empty string.
  } else if (fmt_node.IsScalar()) {
    // Scalar case - effectively a string, primary issue is whether it's quoted.
    Rv<Extractor::Format> result;
    TextView text { fmt_node.Scalar() };
    if (text.empty()) { // an actually empty string
      result = Extractor::literal(""_tv);
    } else if (fmt_node.Tag() == "?"_tv) { // unquoted, must be extractor.
      result = Extractor::parse_raw(*this, text);
    } else {
      result = Extractor::parse(*this, text);
    }

    if (result.is_ok()) {
      auto & exfmt = result.result();
      if (exfmt._max_arg_idx >= 0) {
        if (!_rxp_group_state || _rxp_group_state->_rxp_group_count == 0) {
          return Error(R"(Extracting capture group at {} but no regular expression is active.)", fmt_node.Mark());
        } else if (exfmt._max_arg_idx >= _rxp_group_state->_rxp_group_count) {
          return Error(R"(Extracting capture group {} at {} but the maximum capture group is {} in the active regular expression from line {}.)", exfmt._max_arg_idx, fmt_node.Mark(), _rxp_group_state->_rxp_group_count-1, _rxp_group_state->_rxp_line);
        }
      }

      if (exfmt._ctx_ref_p && _feature_state && _feature_state->_feature_ref_p) {
        _feature_state->_feature_ref_p = true;
      }

      exfmt._force_c_string_p = StrType::C == str_type;
      this->localize(exfmt);
    }
    return std::move(result);
  } else if (fmt_node.IsSequence()) {
    // empty list is treated as an empty string.
    if (fmt_node.size() < 1) {
      auto exfmt { Extractor::literal(TextView{}) };
      exfmt._force_c_string_p = StrType::C == str_type;
      return std::move(exfmt);
    }

    auto str_node { fmt_node[0] };
    if (! str_node.IsScalar()) {
      return Error(R"(Value at {} in list at {} is not a string as required.)", str_node.Mark(), fmt_node.Mark());
    }

    auto &&[fmt, errata]{Extractor::parse(*this, str_node.Scalar())};
    if (! errata.is_ok()) {
      errata.info(R"(While parsing extractor format at {} in modified string at {}.)", str_node.Mark(), fmt_node.Mark());
      return { {}, std::move(errata) };
    }

    fmt._force_c_string_p = StrType::C == str_type;
    this->localize(fmt);

    for ( unsigned idx = 1 ; idx < fmt_node.size() ; ++idx ) {
      auto child { fmt_node[idx] };
      auto && [ mod, mod_errata ] { FeatureMod::load(*this, child, fmt._result_type) };
      if (! mod_errata.is_ok()) {
        mod_errata.info(R"(While parsing modifier {} in modified string at {}.)", child.Mark(), fmt_node.Mark());
        return { {}, std::move(mod_errata) };
      }
      if (_feature_state) {
        _feature_state->_type = mod->result_type();
      }
      fmt._mods.emplace_back(std::move(mod));
    }
    return { std::move(fmt), {} };
  }

  return Error(R"(Value at {} is not a string or list as required.)", fmt_node.Mark());
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
    if (name == Directive::DO_KEY) {
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
        drtv_errata.info(R"()");
        return { {}, std::move(drtv_errata) };
      }
      // Fill in config depending data and pass a pointer to it to the directive instance.
      auto & rtti = _drtv_info[static_info._idx];
      if (++rtti._count == 1) { // first time this directive type has been used.
        rtti._idx = static_info._idx;
        rtti._cfg_span = _arena.alloc(static_info._cfg_storage_required);
        rtti._ctx_storage_offset = _ctx_storage_required;
        _ctx_storage_required += static_info._ctx_storage_required;
      }
      drtv->_rtti = &rtti;

      return { std::move(drtv), {} };
    }
  }
  return Error(R"(Directive at {} has no recognized tag.)", drtv_node.Mark());
}

Rv<Directive::Handle> Config::parse_directive(YAML::Node const& drtv_node) {
  if (drtv_node.IsMap()) {
    return { this->load_directive(drtv_node) };
  } else if (drtv_node.IsSequence()) {
    Errata zret;
    auto list { new DirectiveList };
    Directive::Handle drtv_list{list};
    for (auto child : drtv_node) {
      auto && [handle, errata] {this->load_directive(child)};
      if (errata.is_ok()) {
        list->push_back(std::move(handle));
      } else {
        return { {}, std::move(errata.error(R"(Failed to load directives at {})", drtv_node.Mark())) };
      }
    }
    return {std::move(drtv_list), {}};
  } else if (drtv_node.IsNull()) {
    return {Directive::Handle(new NilDirective)};
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
      return Errata().error(R"(Key "{}" not found - no such key "{}".)", path, path.prefix(path.size() - p.size()).rtrim('.'));
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
      errata.error(R"(Failure while loading list of top level directives for "{}" at {}.)",
      path, base_node.Mark());
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
