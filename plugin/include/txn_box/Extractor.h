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

#pragma once

#include <unordered_map>
#include <string_view>
#include <variant>

#include <swoc/TextView.h>
#include <swoc/bwf_base.h>
#include <swoc/Errata.h>
#include <swoc/swoc_ip.h>

#include "txn_box/common.h"
#include "txn_box/FeatureMod.h"

class Context;

/** Feature extraction.
 *
 * Application of format strings to transactions to extract features. This also maintains a factory
 * which maps from names to instances of subclasses. In use, the extractor will be passed a
 * run time context which is expected to suffice to extract the appropriate information.
 */
class Extractor {
  using self_type = Extractor; ///< Self reference type.
public:
  using Type = FeatureType; ///< Import for convenience.

  /// Container for extractor factory.
  using Table = std::unordered_map<std::string_view, self_type *>;

  /** Format specifier.
   * This is a subclass of the base format specifier, in order to add a field that points at
   * the extractor, if any, for the specifier.
   */
  struct Spec : public swoc::bwf::Spec {
    /// Extractor used in the spec, if any.
    Extractor * _extractor = nullptr;
  };

  /// Parsed extractor string.
  class Format {
    using self_type = Format; ///< Self reference type.
  public:
    /// @defgroup Properties.
    /// @{
    bool _ctx_ref_p = false; /// @c true if any format element has a context reference.
    bool _literal_p = true; ///< @c true if the format is only literals, no extractors.
    bool _direct_p = true; ///< @c true if the format is a single view that can be accessed directly.
    int _max_arg_idx = -1; ///< Largest argument index. -1 => no numbered arguments.
    /// @}

    /// Type of feature extracted by this format.
    Type _feature_type = STRING;

    /// Condensed format string.
    using Specifiers = std::vector<Spec>;
    /// Modifiers
    using Modifers = std::vector<FeatureMod::Handle>;

    /// Specifiers / elements of the parsed format string.
    Specifiers _specs;

    /// Post extraction modifiers.
    Modifers _mods;

    /// Add an format specifier item to the format.
    self_type & push_back(Spec const& spec);

    /// The number of elements in the format.
    size_t size() const;

    /** Access a format element by index.
     *
     * @param idx Element index.
     * @return A reference to the element.
     */
    Spec& operator [] (size_t idx) { return _specs[idx]; }

    /** Access a format element by index.
     *
     * @param idx Element index.
     * @return A reference to the element.
     */
    Spec const& operator [] (size_t idx) const { return _specs[idx]; }
  };

  /** Format extractor for BWF.
   * Walk the @c Format and pull out the items for BWF.
   */
  class FmtEx {
  public:
    FmtEx(Format::Specifiers const& specs) : _specs(specs), _iter(specs.begin()) {}

    operator bool() const { return _iter != _specs.end(); }
    bool operator()(std::string_view& literal, Spec & spec);;
  protected:
    Format::Specifiers const& _specs;
    Format::Specifiers::const_iterator _iter;
  };

  /// @defgroup Properties.
  /// Property methods for extractors.
  /// @{

  /** The type of feature extracted.
   *
   * @return The extracted feature type.
   *
   * The default implementation returns @c VIEW.
   *
   * @note All features can be extracted as strings if needed. This type provides the ability to
   * do more specific type processing for singleton extractions.
   */
  virtual Type feature_type() const;

  /** Whether the extractor uses data from the contest.
   *
   * This is important for @c DIRECT features - if there is a potential reference to that value
   * in another directive, it must be "upgraded" to a @c VIEW to avoid using changed or invalid data.
   * The default implementation returns @c false.
   *
   * @return @c true if the extractor uses the context, @c false otherwise.
   */
  virtual bool has_ctx_ref() const;

  /// @}

  /** Generate string output for the feature.
   *
   * @param w Output writer.
   * @param spec Specifier data.
   * @param ctx Transaction context.
   * @return @a w.
   *
   * This is the generic entry point for generating string output for a feature, which is required
   * for all extractrors.
   */
  virtual swoc::BufferWriter & format(swoc::BufferWriter& w, Spec const& spec, Context & ctx) = 0;

  /** Parse a format string.
   *
   * @param format_string Format string.
   * @return The format instance or errors on failure.
   */
  static swoc::Rv<Format> parse(swoc::TextView format_string);

  /** Create a format string as a literal.
   *
   * @param format_string Format string.
   * @return The format instance or errors on failure.
   *
   * This does no parsing of @a format_string. It will return a format that outputs @a format_string
   * literally.
   */
  static swoc::Rv<Format> literal(swoc::TextView format_string);

  static swoc::Errata define(swoc::TextView name, self_type * ex);

protected:
  static Table _ex_table;
};

/** A string expressed as a view.
 *
 * @internal Is this still useful?
 */
class ViewFeature {
public:
  Extractor::Type feature_type() const;
};

/** A view of a transient string.
 * This is similar to @c STRING. The difference is the view is of a string in non-plugin controlled
 * memory which may disappear or change outside of plugin control. It must therefore be treated
 * with a great deal more care than a @c VIEW type. This type can be converted to a @c VIEW by
 * localizing (making a copy of) the string in the arena.
 */
class DirectFeature {
public:
  Extractor::Type feature_type() const;
  virtual FeatureView direct_view(Context & ctx) const = 0;
};

class IPAddrFeature {
};

class IntegerFeature {
};

inline size_t Extractor::Format::size() const { return _specs.size(); }
