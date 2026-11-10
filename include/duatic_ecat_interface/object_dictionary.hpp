/*
 * Copyright 2026 Duatic AG
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 * disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
 * following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The EtherCAT Technology, and the trade name "EtherCAT" are the intellectual property of, and protected by Beckhoff
 * Automation GmbH. You may review, edit, compile, link, debug the EtherCAT Master Code, provided that before
 * implementing into a product, using, selling or otherwise distributing an EtherCAT Master License is obtained from
 * Beckhoff Automation GmbH & Co.KG.
 *
 * To receive a copy of the EtherCAT Master License contact Beckhoff Automation GmbH & Co.KG, Huelshorstweg 20,
 * D-33415 Verl, Germany (www.beckhoff.com).
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

#include "duatic_ecat_interface/types.hpp"

namespace duatic::ecat_interface
{
struct SDOSubEntry
{
  SDOSubIndex index{};
  std::string name{};
  DataType data_type{};
  std::size_t size{};
};

struct SDOEntry
{
  SDOIndex index{};
  SDOObjectCode object_code{};
  DataType data_type{};
  std::size_t count_sub_indices{};
  std::string name{};

  std::vector<SDOSubEntry> sub_entries{};
};

/**
 * @brief ObjectDictionary - representation of an od description dump from a device
 */
class ObjectDictionary
{
public:
  ObjectDictionary() = default;

  /**
   * @brief construct from a set of entries
   * @param entries - taken by value so callers can move a temporary in
   */
  explicit ObjectDictionary(std::vector<SDOEntry> entries) : sdo_entries_(std::move(entries))
  {
    rebuild_lookup();
  }

  /**
   * @brief copy ctor - deep-copies the storage, then rebuilds the lookup maps
   */
  ObjectDictionary(const ObjectDictionary& other) : sdo_entries_(other.sdo_entries_)
  {
    rebuild_lookup();
  }

  /**
   * @brief copy assignment
   */
  ObjectDictionary& operator=(const ObjectDictionary& other)
  {
    if (this != &other) {
      sdo_entries_ = other.sdo_entries_;
      rebuild_lookup();
    }
    return *this;
  }

  /**
   * @brief move ctor - the vector move transfers the buffer, so the cached
   *        pointers remain valid and the maps can be moved as-is
   */
  ObjectDictionary(ObjectDictionary&& other) noexcept
    : sdo_entries_(std::move(other.sdo_entries_))
    , sdo_entries_by_index_(std::move(other.sdo_entries_by_index_))
    , sdo_entries_by_name_(std::move(other.sdo_entries_by_name_))
  {
    other.sdo_entries_by_index_.clear();
    other.sdo_entries_by_name_.clear();
  }

  /**
   * @brief move assignment
   */
  ObjectDictionary& operator=(ObjectDictionary&& other) noexcept
  {
    if (this != &other) {
      sdo_entries_ = std::move(other.sdo_entries_);
      sdo_entries_by_index_ = std::move(other.sdo_entries_by_index_);
      sdo_entries_by_name_ = std::move(other.sdo_entries_by_name_);

      other.sdo_entries_by_index_.clear();
      other.sdo_entries_by_name_.clear();
    }
    return *this;
  }

  ~ObjectDictionary() = default;

  /**
   * @brief has_index - check if the od contains the given index
   * @param index - sdo index to check
   * @return true if index exists, false if not
   */
  bool has_index(const SDOIndex index) const
  {
    return sdo_entries_by_index_.contains(index);
  }

  /**
   * @brief has_name - check if the od contains an entry with the given name
   * @param name - sdo entry name to check
   * @return true if the name exists, false if not
   */
  bool has_name(const std::string& name) const
  {
    return sdo_entries_by_name_.contains(name);
  }

  /**
   * @brief at - obtain entry based on the index
   * @param index - sdo index to obtain the description of
   * @throw std::out_of_range if index is not available
   */
  const SDOEntry& at(const SDOIndex index) const
  {
    return *sdo_entries_by_index_.at(index);
  }

  /**
   * @brief at - obtain entry based on its name
   * @param name - sdo index name
   * @throw std::out_of_range if name is not available
   */
  const SDOEntry& at(const std::string& name) const
  {
    return *sdo_entries_by_name_.at(name);
  }

  /**
   * @brief entries - obtain all entries
   */
  const auto& entries() const
  {
    return sdo_entries_;
  }

  /**
   * @brief size - number of entries in the dictionary
   */
  std::size_t size() const
  {
    return sdo_entries_.size();
  }

  /**
   * @brief empty - true if the dictionary holds no entries
   */
  bool empty() const
  {
    return sdo_entries_.empty();
  }

private:
  /**
   * @brief rebuild_lookup - repopulate both maps from the current storage
   */
  void rebuild_lookup()
  {
    sdo_entries_by_index_.clear();
    sdo_entries_by_name_.clear();

    sdo_entries_by_index_.reserve(sdo_entries_.size());
    sdo_entries_by_name_.reserve(sdo_entries_.size());

    for (const auto& e : sdo_entries_) {
      sdo_entries_by_index_.emplace(e.index, &e);
      sdo_entries_by_name_.emplace(e.name, &e);
    }
  }

  std::vector<SDOEntry> sdo_entries_;
  std::unordered_map<SDOIndex, const SDOEntry*> sdo_entries_by_index_;
  std::unordered_map<std::string, const SDOEntry*> sdo_entries_by_name_;
};

}  // namespace duatic::ecat_interface
