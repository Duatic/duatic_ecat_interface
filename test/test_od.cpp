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
#include <gtest/gtest.h>

#include "duatic_ecat_interface/object_dictionary.hpp"

using namespace duatic::ecat_interface;  // NOLINT(build/namespaces)

namespace
{
std::vector<SDOEntry> make_sample_entries()
{
  SDOEntry mode;
  mode.index = 0x6060;
  mode.obj_type = SDOObjectCode::Var;
  mode.data_type = DataType::INTEGER8;
  mode.count_sub_indices = 0;
  mode.name = "modes_of_operation";

  SDOEntry status;
  status.index = 0x6041;
  status.obj_type = SDOObjectCode::Var;
  status.data_type = DataType::UNSIGNED16;
  status.count_sub_indices = 0;
  status.name = "statusword";

  return { mode, status };
}
}  // namespace

TEST(ObjectDictionaryTest, DefaultConstructedIsEmpty)
{
  ObjectDictionary od;
  EXPECT_TRUE(od.empty());
  EXPECT_EQ(od.size(), 0u);
  EXPECT_FALSE(od.has_index(0x6060));
  EXPECT_FALSE(od.has_name("modes_of_operation"));
}

TEST(ObjectDictionaryTest, ConstructFromEntriesPopulatesLookup)
{
  ObjectDictionary od(make_sample_entries());

  EXPECT_FALSE(od.empty());
  EXPECT_EQ(od.size(), 2u);
  EXPECT_TRUE(od.has_index(0x6060));
  EXPECT_TRUE(od.has_name("statusword"));
  EXPECT_FALSE(od.has_index(0x1234));
  EXPECT_FALSE(od.has_name("does_not_exist"));
}

TEST(ObjectDictionaryTest, AtByIndexAndByNameReturnSameEntry)
{
  ObjectDictionary od(make_sample_entries());

  const SDOEntry& by_index = od.at(static_cast<SDOIndex>(0x6060));
  const SDOEntry& by_name = od.at(std::string("modes_of_operation"));

  EXPECT_EQ(by_index.name, "modes_of_operation");
  EXPECT_EQ(&by_index, &by_name);
  EXPECT_EQ(by_index.data_type, DataType::INTEGER8);
}

TEST(ObjectDictionaryTest, AtThrowsOnMissingIndex)
{
  ObjectDictionary od(make_sample_entries());
  EXPECT_THROW(od.at(static_cast<SDOIndex>(0xFFFF)), std::out_of_range);
}

TEST(ObjectDictionaryTest, AtThrowsOnMissingName)
{
  ObjectDictionary od(make_sample_entries());
  EXPECT_THROW(od.at(std::string("does_not_exist")), std::out_of_range);
}

TEST(ObjectDictionaryTest, EntriesReturnsAllStoredEntries)
{
  ObjectDictionary od(make_sample_entries());
  EXPECT_EQ(od.entries().size(), 2u);
}

// Regression test: the copy ctor must deep-copy the entry storage and rebuild
// its own lookup maps rather than caching pointers into the source object's
// vector. If it didn't, `copy` would hold dangling pointers once `original`
// goes out of scope below.
TEST(ObjectDictionaryTest, CopyConstructorSurvivesSourceDestruction)
{
  ObjectDictionary copy;
  {
    ObjectDictionary original(make_sample_entries());
    copy = ObjectDictionary(original);
  }

  ASSERT_TRUE(copy.has_index(0x6060));
  EXPECT_EQ(copy.at(static_cast<SDOIndex>(0x6060)).name, "modes_of_operation");
  EXPECT_EQ(copy.size(), 2u);
}

TEST(ObjectDictionaryTest, CopyAssignmentSurvivesSourceDestruction)
{
  ObjectDictionary copy;
  {
    ObjectDictionary original(make_sample_entries());
    copy = original;
  }

  ASSERT_TRUE(copy.has_name("statusword"));
  EXPECT_EQ(copy.at(std::string("statusword")).index, 0x6041);
}

TEST(ObjectDictionaryTest, MoveConstructorTransfersOwnership)
{
  ObjectDictionary original(make_sample_entries());
  ObjectDictionary moved(std::move(original));

  EXPECT_TRUE(moved.has_index(0x6060));
  EXPECT_EQ(moved.size(), 2u);

  // moved-from object must not leave stale lookup entries behind
  EXPECT_TRUE(original.empty() || !original.has_index(0x6060));
}

TEST(ObjectDictionaryTest, MoveAssignmentTransfersOwnership)
{
  ObjectDictionary original(make_sample_entries());
  ObjectDictionary target;
  target = std::move(original);

  EXPECT_TRUE(target.has_name("modes_of_operation"));
  EXPECT_EQ(target.size(), 2u);
}

TEST(ObjectDictionaryTest, SelfCopyAssignmentIsNoOp)
{
  ObjectDictionary od(make_sample_entries());
  const ObjectDictionary& alias = od;
  od = alias;

  EXPECT_EQ(od.size(), 2u);
  EXPECT_TRUE(od.has_index(0x6060));
}
