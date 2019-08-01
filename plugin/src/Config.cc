/*
   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.

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
/* ------------------------------------------------------------------------------------ */
swoc::Lexicon<Hook> HookName {{ {Hook::CREQ, {"read-request", "creq"}}
                              , {Hook::PREQ, {"send-request", "preq"}}
                              , {Hook::URSP, {"read-response", "ursp"}}
                              , {Hook::PRSP, {"send-response", "prsp"}}
                              , {Hook::PRE_REMAP, {"pre-remap"}}
                              , {Hook::POST_REMAP, {"post-remap"}}
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
/* ------------------------------------------------------------------------------------ */
Config::Config() {
  _drtv_info.resize(Directive::StaticInfo::_counter + 1);

}

Rv<Extractor::Format> Config::parse_feature(YAML::Node fmt_node, StrType str_type) {
  if (0 == strcasecmp(fmt_node.Tag(), LITERAL_TAG)) {
    if (! fmt_node.IsScalar()) {
      return { {}, Errata().error(R"("!{}" tag used on value at {} but is not a string as required.)", LITERAL_TAG, fmt_node.Mark()) };
    }
    auto exfmt { Extractor::literal(fmt_node.Scalar()) };
    exfmt._force_c_string_p = StrType::C == str_type;
    return std::move(exfmt);
  }

  // Handle simple string.
  if (fmt_node.IsScalar()) {

    auto &&[exfmt, errata]{Extractor::parse(fmt_node.Scalar())};
    if (errata.is_ok()) {

      if (exfmt._max_arg_idx >= 0) {
        if (!_rxp_group_state || _rxp_group_state->_rxp_group_count == 0) {
          return { {}, Errata().error(R"(Extracting capture group at {} but no regular expression is active.)", fmt_node.Mark()) };
        } else if (exfmt._max_arg_idx >= _rxp_group_state->_rxp_group_count) {
          return { {}, Errata().error(R"(Extracting capture group {} at {} but the maximum capture group is {} in the active regular expression from line {}.)", exfmt._max_arg_idx, fmt_node.Mark(), _rxp_group_state->_rxp_group_count-1, _rxp_group_state->_rxp_line) };
        }
      }

      if (exfmt._ctx_ref_p && _feature_state && _feature_state->_feature_ref_p) {
        _feature_state->_feature_ref_p = true;
      }

      exfmt._force_c_string_p = StrType::C == str_type;
      this->localize(exfmt);
    }
    return {std::move(exfmt), std::move(errata)};
  } else if (fmt_node.IsSequence()) {
    // empty list is treated as an empty string.
    if (fmt_node.size() < 1) {
      auto exfmt { Extractor::literal(TextView{}) };
      exfmt._force_c_string_p = StrType::C == str_type;
      return std::move(exfmt);
    }

    auto str_node { fmt_node[0] };
    if (! str_node.IsScalar()) {
      return { {}, Errata().error(R"(Value at {} in list at {} is not a string as required.)", str_node.Mark(), fmt_node.Mark()) };
    }

    auto &&[fmt, errata]{Extractor::parse(str_node.Scalar())};
    if (! errata.is_ok()) {
      errata.info(R"(While parsing extractor format at {} in modified string at {}.)", str_node.Mark(), fmt_node.Mark());
      return { {}, std::move(errata) };
    }

    fmt._force_c_string_p = StrType::C == str_type;
    this->localize(fmt);

    for ( unsigned idx = 1 ; idx < fmt_node.size() ; ++idx ) {
      auto child { fmt_node[idx] };
      auto && [ mod, mod_errata ] { FeatureMod::load(*this, child, fmt._feature_type) };
      if (! mod_errata.is_ok()) {
        mod_errata.info(R"(While parsing modifier {} in modified string at {}.)", child.Mark(), fmt_node.Mark());
        return { {}, std::move(mod_errata) };
      }
      if (_feature_state) {
        _feature_state->_type = mod->output_type();
      }
      fmt._mods.emplace_back(std::move(mod));
    }
    return { std::move(fmt), {} };
  }

  return { {}, Errata().error(R"(Value at {} is not a string or list as required.)", fmt_node.Mark()) };
}

Rv<Directive::Handle> Config::load_directive(YAML::Node const& drtv_node)
{
  YAML::Node key_node;
  for ( auto const&  [ key_name, key_value ] : drtv_node ) {
    TextView key { key_name.Scalar() };
    // Ignorable keys in the directive. Currently just one, so hand code it. Make this better
    // if there is ever more than one.
    if (key == Directive::DO_KEY) {
      continue;
    }
    // See if this is in the factory. It's not an error if it's not, to enable adding extra
    // keys to directives. First key that is in the factory determines the directive type.
    // If none of the keys are in the factory, that's an error and is reported after the loop.
    if ( auto spot { _factory.find(key) } ; spot != _factory.end()) {
      auto const& [ hooks, worker, static_info ] { spot->second };
      if (! hooks[IndexFor(this->current_hook())]) {
        return { {}, Errata().error(R"(Directive "{}" at {} is not allowed on hook "{}".)", key, drtv_node.Mark(), this->current_hook()) };
      }
      auto && [ drtv, drtv_errata ] { worker(*this, drtv_node, key_value) };
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
  return { {}, Errata().error(R"(Directive at {} has no recognized tag.)", drtv_node.Mark()) };
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
  return { {}, Errata().error(R"(Directive at {} is not an object or a sequence as required.)",
      drtv_node.Mark()) };
}

// Basically a wrapper for @c load_directive to handle stacking feature provisioning. During load,
// all paths must be explored and so the active feature needs to be stacked up so it can be restored
// after a tree descent. During runtime, only one path is followed and therefore this isn't
// required.
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
  Errata zret;
  if (drtv_node.IsMap()) {
    YAML::Node key_node { drtv_node[When::KEY] };
    if (key_node) {
      auto &&[handle, errata]{When::load(*this, drtv_node, key_node)};
      if (errata.is_ok()) {
        auto hook = static_cast<When*>(handle.get())->get_hook();
        _roots[IndexFor(hook)].emplace_back(std::move(handle));
        _has_top_level_directive_p = true;
      } else {
        zret.note(errata);
      }
    } else {
      zret.error(R"(Top level directive at {} is not a "when" directive as required.)", drtv_node.Mark());
    }
  } else {
    zret.error(R"(Top level directive at {} is not an object as required.)", drtv_node.Mark());
  }
  return std::move(zret);
}

TextView Config::localize(swoc::TextView text) {
  auto span { _arena.alloc(text.size()).rebind<char>() };
  memcpy(span, text);
  return span.view();
};

Config& Config::localize(Extractor::Format &fmt) {
  // Special case a "pure" literal - it's a format but all of the specifiers are literals.
  // This can be consolidated into a single specifier with a single literal.
  if (fmt._literal_p) {
    if (fmt.size() == 1) {
      TextView src{fmt[0]._ext}, parsed;
      auto n = swoc::svtoi(src, &parsed);
      if (parsed.size() == src.size()) {
        fmt._feature_type = INTEGER;
        fmt._number = n;
      }
    }
    size_t n = std::accumulate(fmt._specs.begin(), fmt._specs.end(), size_t{0}, [](size_t sum
                                                                                   , Extractor::Spec const &spec) -> size_t { return sum += spec._ext.size(); });
    if (fmt._force_c_string_p) {
      ++n;
    }

    auto span{_arena.alloc(n).rebind<char>()};
    Extractor::Spec literal_spec;
    literal_spec._type = swoc::bwf::Spec::LITERAL_TYPE;
    literal_spec._ext = {span.data(), span.size()};
    for (auto const &spec : fmt._specs) {
      memcpy(span.data(), spec._ext.data(), spec._ext.size());
      span.remove_prefix(spec._ext.size());
    }
    if (fmt._force_c_string_p) {
      span[0] = '\0';
    }
    fmt._force_c_string_p = false; // Already took care of this, don't do it again.
    fmt._specs.resize(1);
    fmt._specs[0] = literal_spec;
  } else {
    // Localize and update the names and extensions.
    for (auto &spec : fmt._specs) {
      if (! spec._name.empty()) {
        spec._name = this->localize(spec._name);
      }
      if (! spec._ext.empty()) {
        spec._ext = this->localize(spec._ext);
      }
    }
  }
  return *this;
}

Errata Config::load_file(swoc::file::path const& file_path) {
  Errata zret;
  std::error_code ec;
  std::string content = swoc::file::load(file_path, ec);

  if (ec) {
    return zret.error(R"(Unable to load file "{}" - {}.)", file_path, ec);
  }

  YAML::Node root;
  try {
    root = YAML::Load(content);
  } catch (std::exception &ex) {
    return zret.error(R"(YAML parsing of "{}" failed - {}.)", file_path, ex.what());
  }

  YAML::Node base_node { root[ROOT_KEY] };
  if (! base_node) {
    return zret.error(R"(Base key "{}" for plugin "{}" not found in "{}".)", ROOT_KEY,
        PLUGIN_NAME, file_path);
  }

  yaml_merge(root);

  if (base_node.IsSequence()) {
    for ( auto const& child : base_node ) {
      zret.note(this->load_top_level_directive(child));
    }
    if (! zret.is_ok()) {
      zret.error(R"(Failure while loading list of top level directives for "{}" at {}.)",
      ROOT_KEY, base_node.Mark());
    }
  } else if (base_node.IsMap()) {
    zret = this->load_top_level_directive(base_node);
  } else {
  }
  return std::move(zret);
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
