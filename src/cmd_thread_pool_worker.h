// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Defined a set of functions for retrieving commands from
  the thread pool and executing them.
 */

#pragma once

#include <memory>
#include <utility>

#include "cmd_table_manager.h"
#include "cmd_thread_pool.h"

namespace pikiwidb {

class CmdWorkThreadPoolWorker {
 public:
  explicit CmdWorkThreadPoolWorker(CmdThreadPool *pool, int onceTask, std::string name)
      : pool_(pool), once_task_(onceTask), name_(std::move(name)) {
    cmd_table_manager_.InitCmdTable();
  }

  void Work();

  void Stop();

  // load the task from the thread pool
  virtual void LoadWork() = 0;

  virtual ~CmdWorkThreadPoolWorker() = default;

 protected:
  std::vector<std::shared_ptr<CmdThreadPoolTask>> self_task_;  // the task that the worker get from the thread pool
  CmdThreadPool *pool_ = nullptr;
  const int once_task_ = 0;  // the max task num that the worker can get from the thread pool
  const std::string name_;
  bool running_ = true;

  pikiwidb::CmdTableManager cmd_table_manager_;
};

// fast worker
class CmdFastWorker : public CmdWorkThreadPoolWorker {
 public:
  explicit CmdFastWorker(CmdThreadPool *pool, int onceTask, std::string name)
      : CmdWorkThreadPoolWorker(pool, onceTask, std::move(name)) {}

  void LoadWork() override;
};

// slow worker
class CmdSlowWorker : public CmdWorkThreadPoolWorker {
 public:
  explicit CmdSlowWorker(CmdThreadPool *pool, int onceTask, std::string name)
      : CmdWorkThreadPoolWorker(pool, onceTask, std::move(name)) {}

  // when the slow worker queue is empty, it will try to get the fast worker
  void LoadWork() override;

 private:
  bool loop_more_ = false;  // When the slow queue is empty, try to get the fast queue
  int wait_time_ = 200;     // When the slow queue is empty, wait 200 ms to check again
};

}  // namespace pikiwidb
