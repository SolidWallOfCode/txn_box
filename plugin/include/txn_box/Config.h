/** @file
 * Configuration classes.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <array>
#include <vector>

#include <swoc/TextView.h>
#include <swoc/MemArena.h>
#include <swoc/swoc_file.h>

#include "txn_box/common.h"
#include "txn_box/Extractor.h"
#include "txn_box/Expr.h"
#include "txn_box/FeatureGroup.h"
#include "txn_box/Directive.h"
#include "txn_box/yaml_util.h"

using TSCont = struct tsapi_cont *;
using TSHttpTxn = struct tsapi_httptxn *;

/// Result from looking at node structure for a value node.
enum class FeatureNodeStyle {
  INVALID, ///< The nodes are not structured as a valie feature.
  SINGLE, ///< Structure is suitable for a single feature.
  TUPLE, ///< Structure is suitable for a list of features.
};

/// Contains a configuration and configuration helper methods.
/// This is also used to pass information between node parsing during configuration loading.
class Config {
  using self_type = Config; ///< Self reference type.
  using Errata = swoc::Errata;
public:

  /// Full name of the plugin.
  static constexpr swoc::TextView PLUGIN_NAME { "Transaction Tool Box" };
  /// Tag name of the plugin.
  static constexpr swoc::TextView PLUGIN_TAG { "txn_box" };

  static const std::string ROOT_KEY; ///< Root key for plugin configuration.

  /// Track the state of provided features.
  struct ActiveFeatureState {
    ActiveType _type; ///< Type of active feature.
    bool _ref_p = false; ///< Feature has been referenced / used.
  };

  class ActiveFeatureScope {
    using self_type = ActiveFeatureScope;
    friend class Config;

    Config * _cfg = nullptr;
    ActiveFeatureState _state;

  public:
    ActiveFeatureScope(Config& cfg) : _cfg(&cfg), _state(cfg._active_feature) {}

    ActiveFeatureScope(self_type && that) : _cfg(that._cfg), _state(that._state) {
      that._cfg = nullptr;
    }

    // No copying.
    ActiveFeatureScope(self_type const& that) = delete;
    self_type & operator = (self_type const& that) = delete;

    ~ActiveFeatureScope() {
      if (_cfg) {
        _cfg->_active_feature = _state;
      }
    }
  };
  friend ActiveFeatureScope;
  ActiveFeatureScope feature_scope(ActiveType const& ex_type) {
    ActiveFeatureScope scope(*this);
    _active_feature._ref_p = false;
    _active_feature._type = ex_type;
    return scope;
  }

  /// Track the state of the active capture groups.
  struct ActiveCaptureState {
    unsigned _count = 0; ///< Number of active capture groups - 0 => not active.
    int _line = -1; ///< Line of the active regular expression.
    bool _ref_p = false; ///< Regular expression capture groups referenced / used.
  };

  class ActiveCaptureScope {
    using self_type = ActiveCaptureScope;
    friend class Config;

    Config * _cfg = nullptr;
    ActiveCaptureState _state;

  public:
    ActiveCaptureScope(Config& cfg) : _cfg(&cfg), _state(cfg._active_capture) {}

    ActiveCaptureScope(self_type && that);

    // No copying.
    ActiveCaptureScope(self_type const& that) = delete;
    self_type & operator = (self_type const& that) = delete;

    ~ActiveCaptureScope() {
      if (_cfg) {
        _cfg->_active_capture = _state;
      }
    }
  };
  friend ActiveCaptureScope;
  ActiveCaptureScope capture_scope(unsigned count, unsigned line_no) {
    ActiveCaptureScope scope(*this);
    _active_capture._count = count;
    _active_capture._line = line_no;
    _active_capture._ref_p = false;
    return scope;
  }

  /// Global and session variable map.
  using Variables = std::map<swoc::TextView, unsigned>;

  /// Cache of parsed YAML for files.
  /// @note Used only for remap.
  using YamlCache = std::unordered_map<swoc::file::path, YAML::Node>;

  /// Default constructor, makes an empty instance.
  Config();

  ~Config();

  Errata load_args(std::vector<std::string> const& args, int arg_offset = 0, YamlCache * cache = nullptr);
  Errata load_args(swoc::MemSpan<char const*> argv, int arg_offset = 0, YamlCache * cache = nullptr);

  /** Load file(s) in to @a this configuation.
   *
   * @param pattern File path pattern (standard glob format)
   * @param cfg_key Root key for configuration data.
   * @return Errors, if any.
   *
   * All files matching the @a pattern are loaded in to this configuration, using @a CfgKey as
   * the root key.
   */
  swoc::Errata load_file_glob(swoc::TextView pattern, swoc::TextView cfg_key, YamlCache* cache = nullptr);

  /** Load a file into @a this.
   *
   * @param cfg_path Path to configuration file.
   * @param cfg_key Root key in configuration.
   * @return Errors, if any.
   *
   * The content of @a cfg_path is loaded in to @a this configuration instance.
   */
  swoc::Errata load_file(swoc::file::path const& cfg_path, swoc::TextView cfg_key, YamlCache * cache = nullptr);

  /** Parse YAML from @a node to initialize @a this configuration.
   *
   * @param root Root node.
   * @param path Path from root node to the configuration based node.
   * @return Errors, if any.
   *
   * The @a path is an @c ARG_SEP separate list of keys. The value of the last key is the
   * node that is parsed. If the path is a single @c ARG_SEP the root node is parsed.
   *
   * @note Currently only @c Hook::REMAP is used for @a hook to handle the special needs of
   * a remap based configuration.
   *
   */
  Errata parse_yaml(YAML::Node const& root, swoc::TextView path);

  void mark_as_remap() { _hook = Hook::REMAP; }

  /** Load directives at the top level.
   *
   * @param node Base plugin configuation node.
   * @return Errors, if any.
   *
   * Processing of directives directly in the base node value is handled specially
   * by this method.
   */
  Errata load_top_level_directive(YAML::Node node);

  Errata load_remap_directive(YAML::Node node);

  /** Load / create a directive from a node.
   *
   * @param drtv_node Directive node.
   * @return A new directive instance, or errors if loading failed.
   */
  swoc::Rv<Directive::Handle> parse_directive(YAML::Node const& drtv_node);

  /** Check the node structure of a value.
   *
   * @param value Value node to check.
   * @return The type of feature represented by @a value.
   *
   * Primarily this checks @a value to see if it's a valid feature, and whether it's single or
   * multiple.
   */
  FeatureNodeStyle feature_node_style(YAML::Node value);

  /** Parse a feature expression.
   *
   * @param fmt_node The node with the expression.
   * @return The expression or errors.
   *
   * This does extensive work for handle the various feature extraction capabilities. This should
   * be bypassed only in extreme cases where very specialized handling is needed. The result of
   * this can be passed to @c Context::extract to get the actual value at runtime.
   *
   * @see Context::extract
   */
  swoc::Rv<Expr> parse_expr(YAML::Node fmt_node);

  /** Copy @a text to local storage in this instance.
   *
   * @param text Text to copy.
   * @return The localized copy.
   *
   * Strings in the YAML configuration are transient. If the content needs to be available at
   * run time it must be first localized.
   */
  std::string_view& localize(std::string_view & text);
  swoc::TextView localize(std::string_view const& text) { swoc::TextView tv { text }; return this->localize(tv); }

  self_type& localize(Feature & feature);

  template < typename T > auto localize(T &) -> EnableForFeatureTypes<T, self_type&> { return *this; }

  /** Allocate config space for an array of @a T.
   *
   * @tparam T Element type.
   * @param count # of elements.
   * @return A span covering the allocated array.
   *
   * This allocates in the config storage. No destructors are called when the config is destructed.
   * If that is required use @c mark_for_cleanup
   *
   * @see mark_for_cleanup
   */
  template < typename T > swoc::MemSpan<T> span(unsigned count) {
    return _arena.alloc(sizeof(T) * count).rebind<T>();
  }

  /** Hook for which the directives are being loaded.
   *
   * @return The current hook.
   */
  Hook current_hook() const;

  ActiveType active_type() const { return _active_feature._type; }

  /** Require regular expression capture vectors to support at least @a n groups.
   *
   * @param n Number of capture groups.
   * @return @a this
   */
  self_type &require_rxp_group_count(unsigned n);

  /** Indicate a directive may be scheduled on a @a hook at runtime.
   *
   * @param hook Runtime dispatch hook.
   * @return @a this
   */
  self_type &reserve_slot(Hook hook) { ++_directive_count[IndexFor(hook)]; return *this; }

  /// Check for top level directives.
  /// @return @a true if there are any top level directives, @c false if not.
  bool has_top_level_directive() const;

  /** Get the top level directives for a @a hook.
   *
   * @param hook The hook identifier.
   * @return A reference to the vector of top level directives for @a hook.
   */
  std::vector<Directive::Handle> const& hook_directives(Hook hook) const;

  /** Mark @a ptr for cleanup when @a this is destroyed.
   *
   * @tparam T Type of @a ptr
   * @param ptr Object to clean up.
   * @return @a this
   *
   * @a ptr is cleaned up by calling
   */
  template <typename T> self_type & mark_for_cleanup(T* ptr);

  template < typename D > static swoc::Errata define() {
    return self_type::define(D::KEY, D::HOOKS, &D::load, &D::cfg_init);
  }

  /** Define a directive.
   *
   * @param name Directive name.
   * @param hooks Mask of valid hooks.
   * @param worker Functor to load / construct the directive from YAML.
   * @param cfg_init_cb Config time initialization if needed.
   * @return Errors, if any.
   */
  static swoc::Errata define(swoc::TextView name, HookMask const& hooks
                             , Directive::InstanceLoader && worker
                             , Directive::CfgInitializer && cfg_init_cb = [](Config&) -> swoc::Errata { return {}; });

  /** Allocate / reserve storage space in @a this.
   *
   * @param n Size in bytes.
   * @return The allocated span.
   *
   * This also stores the span in the RTTI / TypeInfo for the directive so it is accessible when
   * the directive is invoked at run time.
   */
  swoc::MemSpan<void> allocate_cfg_storage(size_t n);

  /** Prepare for context storage.
   *
   * @param n Number of bytes.
   * @return Errors, if any.
   *
   * This storage is not immediately allocated (in contrast to @c allocate_cfg_storage. Instead it
   * is allocated when a @c Context is created, for each @c Context instance. This is shared among
   * instances of the directive, similarly to class static storage. This should be invoked during
   * directive type setup or object loading. Per instance context storage should be allocated during
   * invocation.
   *
   * Access to the storage is via @c Context::storage_for
   *
   * @see Context::storage_for
   */
  Errata reserve_ctx_storage(size_t n);

  /** Get current directive info.
   *
   * @return Current directive configuration information or @c nullptr if no current directive.
   *
   * This is useful only during configuration loading. Just before a directive is loaded
   * this is set for that directive, and cleared after the directive is loaded.
   */
  Directive::CfgInfo const * drtv_info() const { return _rtti; }

  /** Get directive info for directive @a name.
   *
   * @param name Name of directive.
   * @return Directive configuration information, or @c nullptr if @a name is not found.
   */
  Directive::CfgInfo const* drtv_info(swoc::TextView name);

  size_t file_count() const { return _cfg_file_count; }

protected:
  friend class When;
  friend class Context;

  // Transient properties
  /// Current hook for directives being loaded.
  Hook _hook = Hook::INVALID;

  /// Stash for directive load type initializer callback.
  Directive::CfgInfo * _rtti = nullptr;

  /// Mark whether there are any top level directives.
  bool _has_top_level_directive_p { false };

  /// Maximum number of capture groups for regular expression matching.
  /// Always at least one because literal matches use that.
  unsigned _capture_groups = 1;

  /** @defgroup Feature reference tracking.
   * A bit obscure but necessary because the active feature and the active capture groups must
   * be tracked independently because either can be overwritten independent of the other. When
   * directives are processed, a state instance can be passed to track back references. This is
   * checked and the pointers updated to point to that iff the incoming state marks the corresponding
   * tracking as active. These can therefore point to different states at different levels of
   * recursion, or the same. This allows the tracking to operate in a simple way, updating the data
   * for the specific tracking without having to do value checks.
   */
  /// @{
  ActiveFeatureState _active_feature; ///< Feature.
  ActiveCaptureState _active_capture; ///< Regular expression capture groups.
  /// #}

  /// Current amount of shared config storage required.
  size_t _cfg_storage_required = 0;

  /// Current amount of shared context storage required.
  size_t _ctx_storage_required = 0;

  /// Array of config level information about directives in use.
  swoc::MemSpan<Directive::CfgInfo> _drtv_info;

  /// A factory that maps from directive names to generator functions (@c Loader instances).
  using Factory = std::unordered_map<std::string_view, Directive::FactoryInfo>;

  /// The set of defined directives..
  static Factory _factory;

  /// Top level directives for each hook. Always invoked.
  std::array<std::vector<Directive::Handle>, std::tuple_size<Hook>::value> _roots;

  /// Largest number of directives across the hooks. These are updated during
  /// directive load, if needed. This includes the top level directives.
  std::array<size_t, std::tuple_size<Hook>::value> _directive_count { 0 };

  /// For localizing data at a configuration level, primarily strings.
  swoc::MemArena _arena;

  /// Additional clean up to perform when @a this is destroyed.
  swoc::IntrusiveDList<Finalizer::Linkage> _finalizers;

  swoc::Rv<Directive::Handle> load_directive(YAML::Node const& drtv_node);

  /** Parse a scalar feature expression.
   *
   * @param fmt_node The node with the extractor. Must be a scalar.
   * @return The expression or errors.
   *
   * Used for scalar expressions that are not NULL nor explicitly literal.
   *
   */
  swoc::Rv<Expr> parse_scalar_expr(YAML::Node node);

  swoc::Rv<Expr> parse_composite_expr(swoc::TextView const& text);

  /** Parse an unquoted feature expression.
   *
   * @param text The unquoted text to parse. This must be non-empty.
   * @return The expression, or errors on failure.
   *
   */
  swoc::Rv<Expr> parse_unquoted_expr(swoc::TextView const& text);

  swoc::Rv<Expr> parse_expr_with_mods(YAML::Node node);

  /** Update the (possible) extractor reference in @a spec.
   *
   * @param spec Specifier to update.
   * @return Value type of the specifier / extractor, or errors if invalid.
   *
   * @a spec is updated in place. If it is an extractor the extractor pointer in @a spec is updated.
   * This also validates the extractor can handle the @a spec details and enables config based
   * storage allocation if needed by the extractor.
   *
   * @see Extractor::validate
   */
  swoc::Rv<ActiveType> validate(Extractor::Spec &spec);

  /// Tracking for configuration files loaded in to @a this.
  class FileInfo {
    using self_type = FileInfo; ///< Self reference type.
  public:
    /** Check if a specific @a key has be used as a root for this file.
     *
     * @param key Root key name to check.
     * @return @c true if already used, @c false if not.
     */
    bool has_cfg_key(swoc::TextView key) const;

    /** Mark a root @a key as used.
     *
     * @param key Name of the key.
     */
    void add_cfg_key(swoc::TextView key);

  protected:
    std::list<std::string> _keys; ///< Root keys loaded from this file.
  };

  /// Mapping of absolute paths to @c FileInfo to track used configuration files / keys.
  using FileInfoMap = std::unordered_map<swoc::file::path, FileInfo>;
  /// Configuration file tracking map.
  FileInfoMap _cfg_files;
  /// # of configuration files tracked.
  /// Used for diagnostics.
  size_t _cfg_file_count = 0;
};

inline bool Config::FileInfo::has_cfg_key(swoc::TextView key) const {
  return _keys.end() != std::find_if(_keys.begin(), _keys.end(), [=] (std::string const& k) { return 0 == strcasecmp(k, key);});
}

inline void Config::FileInfo::add_cfg_key(swoc::TextView key) {
  _keys.emplace_back(key);
}

inline Config::ActiveCaptureScope::ActiveCaptureScope(Config::ActiveCaptureScope::self_type&& that) : _cfg(that._cfg), _state(that._state) {
  that._cfg = nullptr;
}

inline Hook Config::current_hook() const { return _hook; }
inline bool Config::has_top_level_directive() const { return _has_top_level_directive_p; }

inline std::vector<Directive::Handle> const &Config::hook_directives(Hook hook) const {
  return _roots[static_cast<unsigned>(hook)];
}

inline Config& Config::require_rxp_group_count(unsigned n) {
  _capture_groups = std::max(_capture_groups, n);
  return *this;
}

template < typename T > auto Config::mark_for_cleanup(T *ptr) -> self_type & {
  auto f = _arena.make<Finalizer>(ptr, [](void* ptr) { std::destroy_at(static_cast<T*>(ptr)); });
  _finalizers.append(f);
  return *this;
}

inline swoc::MemSpan<void> Config::allocate_cfg_storage(size_t n) {
  return _rtti->_cfg_store = _arena.alloc(n);
}
