//  Copyright (c) 2017-present, OpenAtom Foundation, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include "rocksdb/db.h"

#include "pstd/noncopyable.h"

namespace storage {
class ScopeSnapshot : public pstd::noncopyable {
 public:
  ScopeSnapshot(rocksdb::DB* db, const rocksdb::Snapshot** snapshot) : db_(db), snapshot_(snapshot) {
    *snapshot_ = db_->GetSnapshot();
  }
  ~ScopeSnapshot() { db_->ReleaseSnapshot(*snapshot_); }

 private:
  rocksdb::DB* const db_;
  const rocksdb::Snapshot** snapshot_;
};

}  // namespace storage
