/** @file
   Base plugin logic.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include <string>
#include <map>
#include <numeric>
#include <getopt.h>

#include <openssl/ssl.h>

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

Global G;

const std::string Config::ROOT_KEY { "txn_box" };

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

namespace {
 std::shared_ptr<Config> Plugin_Config;
} // namespace
/* ------------------------------------------------------------------------------------ */
YAML::Node yaml_merge(YAML::Node & root) {
  static constexpr auto flatten = [] (YAML::Node & dst, YAML::Node & src) -> void {
    if (src.IsMap()) {
      for ( auto const& [ key, value ] : src ) {
        // don't need to check for nested merge key, because this function is called only if
        // that's already set in @a dst therefore it won't be copied up from @a src.
        if (!dst[key]) {
          dst[key] = value;
        }
      }
    }
  };

  if (root.IsSequence()) {
    for ( auto && child : root ) {
      yaml_merge(child);
    }
  } else if (root.IsMap()) {
    // Do all nested merges first, so the result is iteration order independent.
    for ( auto && [ key, value ] : root ) {
      value = yaml_merge(value);
    }
    // If there's a merge key, merge it in.
    if ( auto merge_node { root[YAML_MERGE_KEY] } ; merge_node ) {
      if (merge_node.IsMap()) {
        flatten(root, merge_node);
      } else if (merge_node.IsSequence()) {
        for (auto &&src : merge_node) {
          flatten(root, src);
        }
      }
      root.remove(YAML_MERGE_KEY);
    }
  }
  return root;
}
/* ------------------------------------------------------------------------------------ */
void Global::reserve_txn_arg() {
  if (G.TxnArgIdx < 0) {
    auto && [ idx, errata ] { ts::HttpTxn::reserve_arg(Config::ROOT_KEY, "Transaction Box") };
    if (! errata.is_ok()) {
      _preload_errata.note(errata);
    } else {
      TxnArgIdx = idx;
    }
  }
}
/* ------------------------------------------------------------------------------------ */
// Global callback, thread safe.
// This sets up local context for a transaction and spins up a per TXN Continuation which is
// protected by a mutex. This hook isn't set if there are no top level directives.
int CB_Txn_Start(TSCont cont, TSEvent ev, void * payload) {
  auto txn {reinterpret_cast<TSHttpTxn>(payload) };
  Context* ctx = new Context(Plugin_Config);
  ctx->enable_hooks(txn);
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

Errata
TxnBoxInit(int argc, char const *argv[]) {
  static constexpr std::array<option, 3> Options = {
      {{"config", 1, nullptr, 'c'}
          , { "key", 1, nullptr, 'k' }
          , {nullptr, 0, nullptr, 0}}};

  Errata errata;

  TSPluginRegistrationInfo info{Config::PLUGIN_TAG.data(), "Verizon Media"
                                , "solidwallofcode@verizonmedia.com"};

  Plugin_Config.reset(new Config);
  TextView cfg_path { "txn_box.yaml" };
  TextView cfg_key { Config::ROOT_KEY };
  int opt;
  int idx;
  optind = 0; // Reset options in case of other plugins.
  while (-1 != (opt = getopt_long(argc, const_cast<char **>(argv), ":", Options.data(), &idx))) {
    switch (opt) {
      case ':':errata.error("'{}' requires a value", argv[optind - 1]);
        break;
      case 'c': cfg_path.assign(argv[optind-1], strlen(argv[optind-1]));
        break;
      case 'k': cfg_key.assign(argv[optind-1], strlen(argv[optind-1]));
        break;
      default:errata.warn("Unknown option [{}] '{}' - ignored", char(opt), argv[optind - 1]);
        break;
    }
  }

  if (!errata.is_ok()) {
    return std::move(errata);
  }

  // Try loading and parsing the file.
  auto &&[root, yaml_errata ]{yaml_load(cfg_path)};
  if (!yaml_errata.is_ok()) {
    yaml_errata.info(R"(While loading file "{}".)", cfg_path);
    return std::move(yaml_errata);
  }

  // Process the YAML data.
  errata = Plugin_Config->parse_yaml(root, cfg_key);
  if (!errata.is_ok()) {
    errata.info(R"(While parsing key "{}" in configuration file "{}".)", cfg_key, cfg_path);
    return std::move(errata);
  }

  if (TSPluginRegister(&info) == TS_SUCCESS) {
    if (Plugin_Config->has_top_level_directive()) {
      TSCont cont{TSContCreate(CB_Txn_Start, nullptr)};
      TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, cont);
      G.reserve_txn_arg();
    }
  } else {
    errata.error(R"({}: plugin registration failed.)", Config::PLUGIN_TAG);
    return std::move(errata);
  }
  return {};
}

void
TSPluginInit(int argc, char const *argv[]) {
  auto errata { TxnBoxInit(argc, argv) };
  std::string err_str;
  if (! G._preload_errata.is_ok()) {
    swoc::bwprint(err_str, "{}: startup issues.\n{}", Config::PLUGIN_NAME, G._preload_errata);
    G._preload_errata.clear();
    TSError("%s", err_str.c_str());
  }
  if (! errata.is_ok()) {
    swoc::bwprint(err_str, "{}: initialization failure.\n{}", Config::PLUGIN_NAME, errata);
    TSError("%s", err_str.c_str());
  }
};
/* ------------------------------------------------------------------------ */

namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  return true;
} ();
} // namespace
