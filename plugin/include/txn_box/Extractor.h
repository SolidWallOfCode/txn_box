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

class Context;

class Extractor {
  using self_type = Extractor; ///< Self reference type.
public:
  /// Preferred type, if any, for the extracted feature.
  enum Type {
    VIEW, ///< View of a string.
    INTEGER, ///< An integer.
    IP_ADDR, ///< IP Address
    BOOL ///< Boolean.
  };

  /** Data storage for a feature.
   * This is carefully arranged to have types in the same order as @c Type.
   */
  using Feature = std::variant<swoc::TextView, intmax_t, swoc::IPAddr, bool>;

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
    bool _literal_p = false; ///< @c true if the format is only literals, no extractors.
    bool _direct_p = true; ///< @c true if the format is a single view that can be accessed directly.
    /// @}

    /// Type of feature extracted by this format.
    Type _feature_type = VIEW;

    /// Specifiers / elements of the parsed format string.
    std::vector<Spec> _items;

    /// Add an format specifier item to the format.
    self_type push_back(Spec const& spec);

    /// The number of elements in the format.
    size_t size() const;

    /** Access a format element by index.
     *
     * @param idx Element index.
     * @return A reference to the element.
     */
    Spec& operator [] (size_t idx) { return _items[idx]; }
  };

  /// @defgroup Properties. Property methods for extractors.
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

  /** Whether the extactor uses data from the contest.
   *
   * This is important for @c DIRECT features - if there is a potential reference to that value
   * in another directive, it must be "upgraded" to a @c VIEW to avoid using changed or invalid data.
   * The default implementation returns @c false.
   *
   * @return @c true if the extractor uses the context, @c false otherwise.
   */
  virtual bool has_ctx_ref() const;

  /// @}

  /** Parse a format string.
   *
   * @param format_string Format string.
   * @param table
   * @return
   */
  static swoc::Rv<Format> parse(swoc::TextView format_string);

  static swoc::Errata define(swoc::TextView name, self_type * ex);

protected:
  static Table _ex_table;
};

/** A string expressed as a view.
 *
 */
class ViewFeature {
public:
  Extractor::Type feature_type() const;
};

/** A view of a transient string.
 * This is similar to @c VIEW. The difference is the view is of a string in non-plugin controlled
 * memory which may disappear or change outside of plugin control. It must therefore be treated
 * with a great deal more care than a @c VIEW type. This type can be converted to a @c VIEW by
 * localizing (making a copy of) the string in the arena.
 */
class DirectFeature {
public:
  Extractor::Type feature_type() const;
  virtual swoc::TextView direct_view(Context & ctx) const = 0;
};

class IPAddrFeature {
};

class IntegerFeature {
};

inline size_t Extractor::Format::size() const { return _items.size(); }
