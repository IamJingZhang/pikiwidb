// Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory

/*
  Defined a set of feedback codes, messages, and functions
  related to handling client commands.
 */

#pragma once

#include <chrono>
#include <set>
#include <span>
#include <unordered_map>
#include <unordered_set>

#include "common.h"
// #include "net/tcp_connection.h"
#include "net/socket_addr.h"
#include "proto_parser.h"
#include "replication.h"
#include "storage/storage.h"

namespace pikiwidb {

struct CommandStatistics {
  CommandStatistics() = default;
  CommandStatistics(const CommandStatistics& other)
      : cmd_count_(other.cmd_count_.load()), cmd_time_consuming_(other.cmd_time_consuming_.load()) {}

  std::atomic<uint64_t> cmd_count_ = 0;
  std::atomic<uint64_t> cmd_time_consuming_ = 0;
};

struct TimeStat {
  using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

  TimeStat() = default;

  void Reset() {
    enqueue_ts_ = TimePoint::min();
    dequeue_ts_ = TimePoint::min();
    process_done_ts_ = TimePoint::min();
  }

  uint64_t GetTotalTime() const {
    return (process_done_ts_ > enqueue_ts_)
               ? std::chrono::duration_cast<std::chrono::milliseconds>(process_done_ts_ - enqueue_ts_).count()
               : 0;
  }

  void SetEnqueueTs(TimePoint now_time) { enqueue_ts_ = now_time; }
  void SetDequeueTs(TimePoint now_time) { dequeue_ts_ = now_time; }
  void SetProcessDoneTs(TimePoint now_time) { process_done_ts_ = now_time; }

  TimePoint enqueue_ts_ = TimePoint::min();
  TimePoint dequeue_ts_ = TimePoint::min();
  TimePoint process_done_ts_ = TimePoint::min();
};

class CmdRes {
 public:
  enum CmdRet {
    kNone = 0,
    kOK,
    kPong,
    kSyntaxErr,
    kInvalidInt,
    kInvalidBitInt,
    kInvalidBitOffsetInt,
    kInvalidBitPosArgument,
    kWrongBitOpNotNum,
    kInvalidFloat,
    kOverFlow,
    kNotFound,
    kOutOfRange,
    kInvalidPwd,
    kNoneBgsave,
    kPurgeExist,
    kInvalidParameter,
    kWrongNum,
    kInvalidIndex,
    kInvalidDbType,
    kInvalidDB,
    kInconsistentHashTag,
    kErrOther,
    kUnknownCmd,
    kUnknownSubCmd,
    KIncrByOverFlow,
    kInvalidCursor,
    kWrongLeader,
    kMultiKey,
  };

  CmdRes() = default;
  virtual ~CmdRes();

  bool None() const { return ret_ == kNone && message_.empty(); }

  bool Ok() const { return ret_ == kOK || ret_ == kNone; }

  void Clear() {
    message_.clear();
    ret_ = kNone;
  }

  inline const std::string& Message() const { return message_; };

  inline void Message(std::string* str) { str->swap(message_); };

  // Inline functions for Create Redis protocol
  inline void AppendStringLen(int64_t ori) { RedisAppendLen(message_, ori, "$"); }
  inline void AppendStringLenUint64(uint64_t ori) { RedisAppendLenUint64(message_, ori, "$"); }
  inline void AppendArrayLen(int64_t ori) { RedisAppendLen(message_, ori, "*"); }
  inline void AppendArrayLenUint64(uint64_t ori) { RedisAppendLenUint64(message_, ori, "*"); }
  inline void AppendInteger(int64_t ori) { RedisAppendLen(message_, ori, ":"); }
  inline void AppendContent(const std::string& value) { RedisAppendContent(message_, value); }
  inline void AppendStringRaw(const std::string& value) { message_.append(value); }
  inline void SetLineString(const std::string& value) { message_ = value + CRLF; }

  void AppendString(const std::string& value);
  void AppendStringVector(const std::vector<std::string>& strArray);
  void RedisAppendLenUint64(std::string& str, uint64_t ori, const std::string& prefix) {
    RedisAppendLen(str, static_cast<int64_t>(ori), prefix);
  }

  void SetRes(CmdRet _ret, const std::string& content = "");

  inline void RedisAppendContent(std::string& str, const std::string& value) {
    str.append(value.data(), value.size());
    str.append(CRLF);
  }

  void RedisAppendLen(std::string& str, int64_t ori, const std::string& prefix);

 protected:
  std::string message_;

 private:
  CmdRet ret_ = kNone;
};

enum ClientFlag {
  kClientFlagMulti = (1 << 0),
  kClientFlagDirty = (1 << 1),
  kClientFlagWrongExec = (1 << 2),
  kClientFlagMaster = (1 << 3),
};

enum class ClientState {
  kOK,
  kClosed,
};

class DB;
struct PSlaveInfo;

class PClient : public std::enable_shared_from_this<PClient>, public CmdRes {
 public:
  //  PClient() = delete;
  explicit PClient();

  //  int HandlePackets(pikiwidb::TcpConnection*, const char*, int);

  void OnConnect();

  std::string PeerIP() const;
  int PeerPort() const;

  //  bool SendPacket(const std::string& buf);
  //  bool SendPacket(const void* data, size_t size);
  bool SendPacket();
  bool SendPacket(std::string&& msg);
  bool SendPacket(UnboundedBuffer& data);
  inline void SendOver() {
    Clear();
    reset();
  }

  // active close
  void Close();

  // on close callback
  void OnClose();

  // dbno
  void SetCurrentDB(int dbno) { dbno_ = dbno; }

  int GetCurrentDB() { return dbno_; }

  static PClient* Current();

  // multi
  void SetFlag(uint32_t flag) { flag_ |= flag; }
  void ClearFlag(uint32_t flag) { flag_ &= ~flag; }
  bool IsFlagOn(uint32_t flag) { return flag_ & flag; }
  void FlagExecWrong() {
    if (IsFlagOn(kClientFlagMulti)) {
      SetFlag(kClientFlagWrongExec);
    }
  }

  bool Watch(int dbno, const std::string& key);
  bool NotifyDirty(int dbno, const std::string& key);
  bool Exec();
  void ClearMulti();
  void ClearWatch();

  // pubsub
  std::size_t Subscribe(const std::string& channel) { return channels_.insert(channel).second ? 1 : 0; }

  std::size_t UnSubscribe(const std::string& channel) { return channels_.erase(channel); }

  std::size_t PSubscribe(const std::string& channel) { return pattern_channels_.insert(channel).second ? 1 : 0; }

  std::size_t PUnSubscribe(const std::string& channel) { return pattern_channels_.erase(channel); }

  const std::unordered_set<std::string>& GetChannels() const { return channels_; }
  const std::unordered_set<std::string>& GetPatternChannels() const { return pattern_channels_; }
  std::size_t ChannelCount() const { return channels_.size(); }
  std::size_t PatternChannelCount() const { return pattern_channels_.size(); }

  bool WaitFor(const std::string& key, const std::string* target = nullptr);

  std::unordered_set<std::string> WaitingKeys() const { return waiting_keys_; }
  void ClearWaitingKeys() { waiting_keys_.clear(), target_.clear(); }
  const std::string& GetTarget() const { return target_; }

  void SetName(const std::string& name) { name_ = name; }
  const std::string& GetName() const { return name_; }
  void SetCmdName(const std::string& name) { cmdName_ = name; }
  const std::string& CmdName() const { return cmdName_; }
  void SetSubCmdName(const std::string& name);
  const std::string& SubCmdName() const { return subCmdName_; }
  std::string FullCmdName() const;  // the full name of the command, such as config set|get|rewrite
  void SetKey(const std::string& name) {
    keys_.clear();
    keys_.emplace_back(name);
  }
  void SetKey(std::vector<std::string>& names);
  const std::string& Key() const { return keys_.at(0); }
  const std::vector<std::string>& Keys() const { return keys_; }
  std::vector<storage::FieldValue>& Fvs() { return fvs_; }
  void ClearFvs() { fvs_.clear(); }
  std::vector<std::string>& Fields() { return fields_; }
  void ClearFields() { fields_.clear(); }

  void SetSlaveInfo();
  PSlaveInfo* GetSlaveInfo() const { return slave_info_.get(); }
  void TransferToSlaveThreads();
  void AddToMonitor();

  static void FeedMonitors(const std::vector<std::string>& params);

  void SetAuth() { auth_ = true; }
  bool GetAuth() const { return auth_; }
  void RewriteCmd(std::vector<std::string>& params) { parser_.SetParams(params); }
  void Reexecutecommand() { this->executeCommand(); }

  inline size_t ParamsSize() const { return params_.size(); }

  inline ClientState State() const { return state_; }

  inline void SetState(ClientState state) { state_ = state; }

  inline void SetConnId(uint64_t id) { net_id_ = id; }
  inline uint64_t GetConnId() const { return net_id_; }
  inline void SetThreadIndex(int8_t index) { net_thread_index_ = index; }
  inline int8_t GetThreadIndex() const { return net_thread_index_; }
  inline void SetSocketAddr(const net::SocketAddr& addr) { addr_ = addr; }

  // All parameters of this command (including the command itself)
  // e.g：["set","key","value"]
  std::span<std::string> argv_;

  // Info Commandstats used
  std::unordered_map<std::string, CommandStatistics>* GetCommandStatMap();
  std::shared_ptr<TimeStat> GetTimeStat();

  //  std::shared_ptr<TcpConnection> getTcpConnection() const { return tcp_connection_.lock(); }
  int handlePacket(const char*, int);

 private:
  void executeCommand();
  int processInlineCmd(const char*, size_t, std::vector<std::string>&);
  void reset();
  bool isPeerMaster() const;
  uint64_t uniqueID() const;

  bool isClusterCmdTarget() const;

  // TcpConnection's life is undetermined, so use weak ptr for safety.
  //  std::weak_ptr<TcpConnection> tcp_connection_;

  PProtoParser parser_;

  int dbno_ = 0;

  std::unordered_set<std::string> channels_;
  std::unordered_set<std::string> pattern_channels_;

  uint32_t flag_ = 0;
  std::unordered_map<int32_t, std::unordered_set<std::string> > watch_keys_;
  std::vector<std::vector<std::string> > queue_cmds_;

  // blocked list
  std::unordered_set<std::string> waiting_keys_;
  std::string target_;

  // slave info from master view
  std::unique_ptr<PSlaveInfo> slave_info_;

  // name
  std::string name_;        // client name
  std::string subCmdName_;  // suchAs config set|get|rewrite
  std::string cmdName_;     // suchAs config
  std::vector<std::string> keys_;
  std::vector<storage::FieldValue> fvs_;
  std::vector<std::string> fields_;

  // All parameters of this command (including the command itself)
  // e.g：["set","key","value"]
  std::vector<std::string> params_;
  // auth
  bool auth_ = false;
  time_t last_auth_ = 0;

  ClientState state_;

  uint64_t net_id_ = 0;
  int8_t net_thread_index_ = 0;
  net::SocketAddr addr_;

  static thread_local PClient* s_current;

  /*
   * Info Commandstats used
   */
  std::unordered_map<std::string, CommandStatistics> cmdstat_map_;
  std::shared_ptr<TimeStat> time_stat_;
};
}  // namespace pikiwidb
