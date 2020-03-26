/** @file
   TS interface for remap.

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
#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"
#include <ts/remap.h>
#include "txn_box/yaml_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
/** Data shared among all instances in a remap configuration.
 *
 * This class has some odd behaviors due to the way the remap plugin API works.
 *
 * A global instance is availabe while the rule instances are being loaded, but after all of them
 * have been loaded, the global instance is cleared so that a new instance is created on the next
 * configuration load.
 *
 * Each rule instance gets a pointer to the global instance for its configuration. The instances
 * reference count that instance and when the last rule is deleted, the instance is cleaned up. In
 * effect, each rule instance acts as a smart pointer. This can't use a normal smart pointer because
 * only a raw pointer can be stored in the rule instance context.
 */
class RemapConfig {
  using self_type = RemapConfig; ///< Self reference type.
public:
  /// Configurations from a single file.
  struct ConfigGroup {
    YAML::Node _root; ///< Root of YAMl in file.
    /// Configurations from file, indexed by base key of the configuration.
    std::unordered_map<std::string, Config> _cfgs;
  };

  /// Obtain the active instance.
  static self_type & acquire();

  /// Release @a this for potential cleanup.
  void release();

  /// Drop active instance, force new instance on next @c acquire.
  static void clear();

  /** Get the @c ConfigGroup for the file @a path.
   *
   * @param path Absolute path to configuration file.
   * @return The @c ConfigGroup or errors if the file cannot be loaded and parsed.
   *
   * If the file is not already in the table, it is loaded and parsed and the table updated.
   */
  Rv<ConfigGroup *> obtain(swoc::file::path const& path);

protected:
  using ConfigTable = std::unordered_map<std::string, ConfigGroup>;
  ConfigTable _cfg_table;

  unsigned _ref_count { 0 }; ///< Reference count.

  /// Active instance.
  static self_type * _instance;

  RemapConfig() = default;
  RemapConfig(self_type const& that) = delete;
  self_type & operator = (self_type const& that) = delete;
};

RemapConfig* RemapConfig::_instance = nullptr;

RemapConfig::self_type &RemapConfig::acquire() {
  if (nullptr == _instance) {
    _instance = new self_type;
  }
  ++(_instance->_ref_count);
  return *_instance;
}

void RemapConfig::release() { if (--_ref_count == 0) { delete this; } }

void RemapConfig::clear() { _instance = nullptr; }

Rv<RemapConfig::ConfigGroup *> RemapConfig::obtain(swoc::file::path const &path) {
  if ( auto spot = _cfg_table.find(path.string()) ; spot != _cfg_table.end()) {
    return &spot->second;
  }
  // Try loading and parsing the file.
  auto && [ root, errata ] { yaml_load(path) };
  if (! errata.is_ok()) {
    errata.info(R"(While loading file "{}".)", path);
    return std::move(errata);
  }
  auto & cg = _cfg_table[path.string()];
  cg._root = root; // looks good, stash it.
  return &cg;
}
/* ------------------------------------------------------------------------------------ */
class RemapContext {
  using self_type = RemapContext; ///< Self reference type.
public:
  RemapConfig& shared_cfg; ///< Shared remap configuration.
  Config* rule_cfg; ///< Configuration for a specific rule in @a r_cfg;
};
/* ------------------------------------------------------------------------------------ */
TSReturnCode
TSRemapInit(TSRemapInterface* rctx, char* errbuff, int errbuff_size) {
  G.reserve_txn_arg();
  if (! G._preload_errata.is_ok()) {
    std::string err_str;
    swoc::bwprint(err_str, "{}: startup issues.\n{}", Config::PLUGIN_NAME, G._preload_errata);
    G._preload_errata.clear();
    TSError("%s", err_str.c_str());
  }
  return TS_SUCCESS;
};

void TSRemapConfigReload() {
  RemapConfig::clear();
}

TSReturnCode TSRemapNewInstance(int argc, char *argv[], void ** ih, char * errbuff, int errbuff_size) {
  swoc::FixedBufferWriter w(errbuff, errbuff_size);
  auto & r_cfg = RemapConfig::acquire();

  if (argc < 3) {
    w.print("{} plugin requires a configuration file parameter.\0", Config::PLUGIN_NAME);
    return TS_ERROR;
  }

  swoc::file::path config_path { ts::make_absolute(swoc::file::path(argv[2])) };
  std::string key_path = ".";
  std::string text;

  if (argc > 3) {
    key_path.assign(argv[3], strlen(argv[3]));
  }

  auto && [ cg, cg_errata ] = r_cfg.obtain(config_path);
  if (! cg_errata.is_ok()) {
    cg_errata.info(R"(While parsing config for {})", Config::PLUGIN_TAG);
    TSError("%s", swoc::bwprint(text, "{}", cg_errata).c_str());
    w.print("Error while parsing configuration for {} - see diagnostic log for more detai0", Config::PLUGIN_TAG);
    return TS_ERROR;
  }

  Config& rule_cfg = cg->_cfgs[key_path];
  auto cfg_errata = rule_cfg.parse_yaml(cg->_root, key_path, Hook::REMAP);
  if (! cfg_errata.is_ok()) {
    cg->_cfgs.erase(key_path);
    cg_errata.info(R"(While parsing config "{}" for {})", config_path, Config::PLUGIN_TAG);
    TSError("%s", swoc::bwprint(text, "{}", cfg_errata).c_str());
    w.print("Error while parsing configuration for {} - see diagnostic log for more detail.\0", Config::PLUGIN_TAG);
    return TS_ERROR;
  }

  *ih = new RemapContext { r_cfg, &rule_cfg };
  return TS_SUCCESS;
}

TSRemapStatus TSRemapDoRemap(void* ih, TSHttpTxn txn, TSRemapRequestInfo* rri) {
  auto r_ctx = static_cast<RemapContext*>(ih);
  // This is a hack because errors reported during TSRemapNewInstance are ignored
  // leaving broken instances around. Gah. Need to fix remap loading to actually
  // check for new instance errors.
  if (nullptr == r_ctx) {
    return TSREMAP_NO_REMAP;
  }

  Context * ctx = static_cast<Context*>(TSHttpTxnArgGet(txn, G.TxnArgIdx));
  if (nullptr == ctx) {
    ctx = new Context({});
    ctx->enable_hooks(txn);
  }
  ctx->invoke_for_remap(*(r_ctx->rule_cfg), rri);

  return ctx->_remap_status;
}

void TSRemapDeleteInstance(void *ih) {
  auto r_ctx = static_cast<RemapContext*>(ih);
  r_ctx->shared_cfg.release();
  delete r_ctx;
}
