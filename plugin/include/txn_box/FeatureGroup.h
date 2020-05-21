/** @file
 * Support for groups of related features in a single directive.
 *
 * Copyright 2020, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "txn_box/common.h"
#include "txn_box/Expr.h"

/* ---------------------------------------------------------------------------------------------- */
/** Mixin for more convenient feature extraction.
 * This provides a general framework for feature extraction and potential cross dependencies.
 */
class FeatureGroup {
  using self_type = FeatureGroup; ///< Self reference type.
public:
  /// Index type for the various indices.
  using index_type = unsigned short;
  /// Value to mark uninitialized / invalid index.
  static constexpr index_type INVALID_IDX = std::numeric_limits<index_type>::max();

  /// Initialization flags.
  enum Flag : int8_t {
    NONE = -1, ///< No flags
    REQUIRED = 0, ///< Key must exist and have a valid format.
    MULTI = 1, ///< Key can be a list of formats.
  };

  /// Description of a key with a feature to extract.
  struct Descriptor {
    using self_type = Descriptor; ///< Self reference type.
    swoc::TextView _name; ///< Key name
    std::bitset<2> _flags; ///< Flags.

    // Convenience constructors.
    /// Construct with only a name, no flags.
    Descriptor(swoc::TextView const& name);
    /// Construct with a name and a single flag.
    Descriptor(swoc::TextView const& name, Flag flag);;
    /// Construct with a name and a list of flags.
    Descriptor(swoc::TextView const& name, std::initializer_list<Flag> const& flags);
  };

  /// Information about a specific extractor format.
  /// This is per configuration data.
  struct ExfInfo {
    /// Information for a feature with a single extraction format.
    struct Single {
      Expr _expr; ///< The format.
      Feature _feature; ///< Retrieved feature data.
    };

    /// Information for a feature with multiple extraction formats.
    struct Multi {
      std::vector<Expr> _fmt; ///< Extractor formats.
    };

    swoc::TextView _name; ///< Key name.
    /// Indices of immediate reference dependencies.
    swoc::MemSpan<index_type> _edge;

    /// Variant indices for the format data.
    enum {
      NIL, SINGLE, MULTI
    };
    /// Allow uninitialized, single, or multiple values.
    using Ex = std::variant<std::monostate, Single, Multi>;
    /// Extraction data, single or multiple.
    Ex _ex;
  };

  ~FeatureGroup();

  /** Load the extractor formats from @a node.
   *
   * @param cfg Configuration context.
   * @param node Node with keys that have extractors (must be a Map)
   * @param ex_keys Keys expected to have extractor formats.
   * @return Errors, if any.
   *
   * The @a ex_keys are loaded and if those refer to other keys, those other keys are transitively
   * loaded. The loading order is a linear ordering of the dependencies between keys. A circular
   * dependency is an error and reported. If a key is multi-valued then it creates a format
   * entry for each value. It is not allowed for a format to be dependent on a multi-valued key.
   *
   * @internal The restriction on dependencies on multi-valued keys is a performance issue. I
   * currently do not know how to support that and allow lazy extraction for multi-valued keys. It's
   * not even clear what that would mean in practice - is the entire multi-value extracted? Obscure
   * enough I'll leave that for when a use case becomes known.
   */
  swoc::Errata load(Config& cfg, YAML::Node const& node, std::initializer_list<Descriptor> const& ex_keys);

  /** Load the extractor formats from @a node
   *
   * @param cfg Configuration context.
   * @param node Node - must be a scalar or sequence.
   * @param ex_keys Elements to extract
   * @return Errors, if any.
   *
   * @a node is required to be a sequence, or a scalar which is treated as a sequence of length 1.
   * The formats are extracted in order. If any format is @c REQUIRED then all preceding ones are
   * also required, even if not marked as such.
   */
  swoc::Errata load_as_tuple(Config& cfg, YAML::Node const& node, std::initializer_list<Descriptor> const& ex_keys);

  /** Get the index of extraction information for @a name.
   *
   * @param name Name of the key.
   * @return The index for the extraction info for @a name or @c INVALID_IDX if not found.
   */
  index_type exf_index(swoc::TextView const& name);

  /** Get the extraction information for @a idx.
   *
   * @param idx Key index.
   * @return The extraction information.
   */
  ExfInfo & operator [] (index_type idx);

  /** Extract the feature.
   *
   * @param ctx Context for extraction.
   * @param name Name of the feature key.
   * @return The extracted data.
   */
  Feature extract(Context &ctx, swoc::TextView const &name);

  /** Extract the feature.
   *
   * @param ctx Context for extraction.
   * @param idx Index of the feature key.
   * @return The extracted data.
   */
  Feature extract(Context &ctx, index_type idx);

protected:
  static constexpr uint8_t DONE = 1; ///< All dependencies computed.
  static constexpr uint8_t IN_PLAY = 2; ///< Dependencies currently being computed.
  static constexpr uint8_t MULTI_VALUED = 3; ///< Multi-valued with all dependencies computed.

  /** Wrapper for tracking array.
   * This wraps a stack allocated variable sized array, which is otherwise inconvenient to use.
   * @note It is assumed the total number of keys is small enough that linear searching is overall
   * faster compared to better search structures that require memory allocation.
   *
   * Essentially this serves as yet another context object, which tracks the reference context
   * as the dependency chains are traced during format loading, to avoid methods with massive
   * and identical parameter lists.
   *
   * This is a temporary data structure used only during configuration load. The data that needs
   * to be persisted is copied to member variables at the end of parsing when all the sizes and
   * info are known.
   *
   * @note - This is a specialized internal class and much care should be used by any sublcass
   * touching it.
   *
   * @internal Move @c Config in here as well?
   */
  struct Tracking {

    /// Per tracked item information.
    /// Vector data is kept as indices so it is stable over vector resizes.
    struct Info {
      swoc::TextView _name; ///< Name.
      /// Index in feature data array.
      /// Not valid if @c multi_found_p.
      index_type _feature_idx;
      index_type _fmt_idx; ///< Index in format vector, start.
      index_type _fmt_count = 0; ///< # of formats.
      index_type _edge_idx; ///< Index in reference dependency vector, start.
      index_type _edge_count = 0; ///< # of immediate dependent references.
      uint8_t _mark = NONE; ///< Ordering search march.
      uint8_t _required_p : 1; ///< Key must exist and have a valid format.
      uint8_t _multi_p : 1; ///< Expr can be a list of formats.

      /// Cross reference (dependency graph edge)
      /// @note THIS IS NOT PART OF THE NODE VALUE!
      /// This is in effect a member of a parallel array and is connected to the node info via
      /// the @a ref_idx and @a _ref_count members. It is a happy circumstance that the number of
      /// elements for this array happens to be just one less than required for the node array, so
      /// it can be overloaded without having to pass in a separate array. This abuses the fact
      /// that a POset can be modeled as a directed acyclic graph, which on N nodes has at most
      /// N-1 edges. It is the edges that are stored here, therefore at most N-1 elements are
      /// required.
      index_type _edge = 0;
    };

    /// External provided array used to track the keys.
    /// Generally stack allocated, it should be the number of keys in the node as this is an
    /// upper bound of the amount of elements needed.
    swoc::MemSpan<Info> _info;

    YAML::Node const& _node; ///< Node containing the keys.

    /// Shared vector of formats - each key has a span that covers part of this vector.
    /// @internal Not allocated in the config data because of clean up issues - these can contain
    /// other allocated data that needs destructors to be invoked.
    std::vector<Expr> _fmt_array;

    /// The number of valid elements in the array.
    index_type _count = 0;

    /// Number of single value features that need feature data.
    index_type _feature_count = 0;

    index_type _edge_count = 0; ///< number of edges (direct dependencies) stored in @a _info

    /** Construct a wrapper on a tracking array.
     *
     * @param node Node containing keys.
     * @param info Array.
     * @param n # of elements in @a info.
     */
    Tracking(YAML::Node const& node, Info * info, unsigned n) : _info(info, n), _node(node)  {
    }

    /// Allocate an entry and return a pointer to it.
    Info * alloc() { return &_info[_count++]; }

    /// Find the array element with @a name.
    /// @return A pointer to the element, or @c nullptr if not found.
    Info * find(swoc::TextView const& name);

    /// Obtain an array element for @a name.
    /// @return A pointer to the element.
    /// If @a name is not in the array, an element is allocated and set to @a name.
    Info * obtain(swoc::TextView const& name);
  };

  /// Immediate dependencies of the references.
  /// A representation of the edges in the dependency graph.
  swoc::MemSpan<index_type> _edge;

  /// Storage for keys to extract.
  swoc::MemSpan<ExfInfo> _exf_info;

  /// Extractor specialized for this feature group.
  Ex_this _ex_this{*this};

  /** Load an extractor format.
   *
   * @param cfg Configuration state.
   * @param info Tracking info.
   * @param node Node that has the format as a value.
   * @return Errors, if any.
   */
  swoc::Errata load_fmt(Config & cfg, Tracking & info, YAML::Node const& node);

  /** Load the format at key @a name from the tracking node.
   *
   * @param cfg Configuration state.
   * @param info Tracking info.
   * @param name Key name.
   * @return Errors, if any.
   *
   * The base node is contained in @a info. The key for @a name is selected and the
   * format there loaded.
   */
  swoc::Errata load_key(Config & cfg, Tracking& info, swoc::TextView name);

};

inline FeatureGroup::Descriptor::Descriptor(swoc::TextView const &name) : _name(name) {}

inline FeatureGroup::Descriptor::Descriptor(swoc::TextView const &name, FeatureGroup::Flag flag) : _name(name) { if (flag != NONE) { _flags[flag] = true ; } }

inline FeatureGroup::Descriptor::Descriptor(swoc::TextView const &name
                                     , std::initializer_list<FeatureGroup::Flag> const &flags) : _name(name) {
  for ( auto f : flags ) {
    if (f != NONE) { _flags[f] = true; }
  }
}

inline FeatureGroup::ExfInfo &FeatureGroup::operator[](FeatureGroup::index_type idx) { return _exf_info[idx]; }
/* ---------------------------------------------------------------------------------------------- */
