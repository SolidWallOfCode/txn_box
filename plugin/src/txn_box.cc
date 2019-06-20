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

const std::string Config::ROOT_KEY { "txn_box" };

swoc::Lexicon<FeatureType> FeatureTypeName {{ {FeatureType::STRING, "string"}
                                            , {FeatureType::INTEGER, "integer"}
                                            , {FeatureType::BOOLEAN, "boolean"}
                                            , {FeatureType::IP_ADDR, "IP address"}
                                           }};

BufferWriter& bwformat(BufferWriter& w, bwf::Spec const& spec, FeatureType type) {
  if (spec.has_numeric_type()) {
    return bwformat(w, spec, static_cast<unsigned>(type));
  }
  return bwformat(w, spec, FeatureTypeName[type]);
}

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

Hook Convert_TS_Event_To_TxB_Hook(TSEvent ev) {
  static const std::map<TSEvent, Hook> table{
    {TS_EVENT_HTTP_READ_REQUEST_HDR,  Hook::CREQ}
  , {TS_EVENT_HTTP_SEND_REQUEST_HDR,  Hook::PREQ}
  , {TS_EVENT_HTTP_READ_RESPONSE_HDR, Hook::URSP}
  , {TS_EVENT_HTTP_SEND_RESPONSE_HDR, Hook::PRSP}
  , {TS_EVENT_HTTP_PRE_REMAP, Hook::PRE_REMAP}
  , {TS_EVENT_HTTP_POST_REMAP, Hook::POST_REMAP}
  };
  if (auto spot{table.find(ev)}; spot != table.end()) {
    return spot->second;
  }
  return Hook::INVALID;
}

Config Plugin_Config;

template < typename F > struct scope_exit {
  F _f;
  explicit scope_exit(F &&f) : _f(std::move(f)) {}
  ~scope_exit() { _f(); }
};
/* ------------------------------------------------------------------------------------ */
TextView ts::URL::host() {
  char const* text;
  int size;
  if (this->is_valid() && nullptr != (text = TSUrlHostGet(_buff, _loc, &size))) {
    return { text, static_cast<size_t>(size) };
  }
  return {};
}

TextView ts::HttpField::value() {
  int size;
  char const* text;
  if (this->is_valid() && nullptr != (text = TSMimeHdrFieldValueStringGet(_buff, _loc, _hdr, -1, &size))) {
    return { text, static_cast<size_t>(size) };
  }
  return {};
}

bool ts::HttpField::assign(swoc::TextView value) {
  return this->is_valid() &&
    TS_SUCCESS == TSMimeHdrFieldValueStringSet(_buff, _hdr, _loc, -1, value.data(), value.size());
}

ts::URL ts::HttpHeader::url() {
  TSMLoc url_loc;
  if (this->is_valid() && TS_SUCCESS == TSHttpHdrUrlGet(_buff, _loc, &url_loc)) {
    return {_buff, url_loc};
  }
  return {};
}

ts::HttpField ts::HttpHeader::field(TextView name) {
  TSMLoc field_loc;
  if (this->is_valid() && nullptr != (field_loc = TSMimeHdrFieldFind(_buff, _loc, name.data(), name.size()))) {
    return { _buff, _loc, field_loc};
  }
  return {};
}

ts::HttpHeader ts::HttpTxn::creq_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnClientReqGet(_txn, &buff, &loc)) {
    return { buff, loc };
  }
  return {};
}

ts::HttpHeader ts::HttpTxn::preq_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnServerReqGet(_txn, &buff, &loc)) {
    return { buff, loc };
  }
  return {};
}

ts::HttpField ts::HttpHeader::field_create(TextView name) {
  if (this->is_valid()) {
    TSMLoc field_loc;
    if (TS_SUCCESS ==
        TSMimeHdrFieldCreateNamed(_buff, _loc, name.data(), name.size(), &field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldAppend(_buff, _loc, field_loc)) {
        return HttpField{_buff, _loc, field_loc};
      }
      TSMimeHdrFieldDestroy(_buff, _loc, field_loc);
    }
  }
  return {};
}

ts::HttpField ts::HttpHeader::field_obtain(TextView name) {
  if (this->is_valid()) {
    if (HttpField field { this->field(name) } ; field.is_valid()) {
      return field;
    }
    return this->field_create(name);
  }
  return {};
}

bool ts::HttpTxn::is_internal() const {
  return static_cast<bool>(TSHttpTxnIsInternal(_txn));
}
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
    exfmt._c_string_p = StrType::C == str_type;
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

      exfmt._c_string_p = StrType::C == str_type;
      this->localize(exfmt);
    }
    return {std::move(exfmt), std::move(errata)};
  } else if (fmt_node.IsSequence()) {
    // empty list is treated as an empty string.
    if (fmt_node.size() < 1) {
      auto exfmt { Extractor::literal(TextView{}) };
      exfmt._c_string_p = StrType::C == str_type;
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

    fmt._c_string_p = StrType::C == str_type;
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
      try {
        _hook = {HookName[key_node.Scalar()]};
        auto && [ handle, errata ]{ When::load(*this, drtv_node, key_node) };
        if (errata.is_ok()) {
          _roots[IndexFor(_hook)].emplace_back(std::move(handle));
          _has_top_level_directive_p = true;
        } else {
          zret.note(errata);
        }
      } catch (std::exception& ex) {
        zret.error(R"(Invalid hook name "{}" in "{}" directive at {}.)", key_node.Scalar(),
            When::KEY, key_node.Mark());
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
    size_t n = std::accumulate(fmt._specs.begin(), fmt._specs.end(), size_t{0}, [](size_t sum
                                                                                   , Extractor::Spec const &spec) -> size_t { return sum += spec._ext.size(); });
    if (fmt._c_string_p) {
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
    if (fmt._c_string_p) {
      span[0] = '\0';
    }
    fmt._c_string_p = false; // Already took care of this, don't do it again.
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


/* ------------------------------------------------------------------------------------ */
int CB_Directive(TSCont cont, TSEvent ev, void * payload) {
  Context* ctx = static_cast<Context*>(TSContDataGet(cont));
  /// TXN Close is special
  if (ev == TS_EVENT_HTTP_TXN_CLOSE) {
      TSContDataSet(cont, nullptr);
      TSContDestroy(cont);
      delete ctx;
  } else {
    Hook hook { Convert_TS_Event_To_TxB_Hook(ev) };
    if (Hook::INVALID != hook) {
      ctx->_cur_hook = hook;
      // Run the top level directives first.
      for (auto const &handle : Plugin_Config.hook_directives(hook)) {
        handle->invoke(*ctx); // need to log errors here.
      }
      // Run any accumulated directives for the hook.
      ctx->invoke_for_hook(hook);
    }
  }
  TSHttpTxnReenable(ctx->_txn, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

// Global callback, not thread safe.
// This sets up local context for a transaction and spins up a per TXN Continuation which is
// protected by a mutex. This hook isn't set if there are no top level directives.
int CB_Txn_Start(TSCont cont, TSEvent ev, void * payload) {
  auto txn {reinterpret_cast<TSHttpTxn>(payload) };
  Context* ctx = new Context(Plugin_Config);
  TSCont txn_cont { TSContCreate(CB_Directive, TSMutexCreate()) };
  TSContDataSet(txn_cont, ctx);
  ctx->_cont = txn_cont;
  ctx->_txn = txn;

  // set hooks for top level directives.
  for ( unsigned idx = static_cast<unsigned>(Hook::BEGIN) ; idx < static_cast<unsigned>(Hook::END) ; ++idx ) {
    auto const& drtv_list { Plugin_Config.hook_directives(static_cast<Hook>(idx)) };
    if (! drtv_list.empty()) {
      TSHttpTxnHookAdd(txn, TS_Hook[idx], txn_cont);
      ctx->_directives[idx]._hook_set = true;
    }
  }
  // Always set a cleanup hook.
  TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, cont);
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
};

namespace {
std::array<option, 2> Options = {
    {{"config", 1, nullptr, 'c'}, {nullptr, 0, nullptr, 0}}};
}

void
TSPluginInit(int argc, char const *argv[]) {
  Errata errata;

  TSPluginRegistrationInfo info{Config::PLUGIN_TAG.data(), "Verizon Media"
                                , "solidwallofcode@verizonmedia.com"};

  int opt;
  int idx;
  optind = 0; // Reset options in case of other plugins.
  while (-1 != (opt = getopt_long(argc, const_cast<char **>(argv), ":", Options.data(), &idx))) {
    switch (opt) {
      case ':':errata.error("'{}' requires a value", argv[optind - 1]);
        break;
      case 'c':errata.note(Plugin_Config.load_file(swoc::file::path{argv[optind-1]}));
        break;
      default:errata.warn("Unknown option '{}' - ignored", char(opt), argv[optind - 1]);
        break;
    }
  }
  if (errata.is_ok()) {
    if (TSPluginRegister(&info) == TS_SUCCESS) {
      if (Plugin_Config.has_top_level_directive()) {
        TSCont cont{TSContCreate(CB_Txn_Start, nullptr)};
        TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, cont);
      }
    } else {
      TSError("%s: plugin registration failed.", Config::PLUGIN_TAG.data());
    }
  } else {
    std::string err_str;
    swoc::bwprint(err_str, "{}: initialization failure.\n{}", Config::PLUGIN_NAME, errata);
    TSError("%s", err_str.c_str());
  }
}
