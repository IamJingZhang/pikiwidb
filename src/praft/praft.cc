/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

//
//  praft.cc

#include "praft.h"
#include <cassert>
#include <memory>
#include <string>
#include "client.h"
#include "config.h"
#include "pstd_string.h"
#include "braft/configuration.h"
#include "event_loop.h"
#include "pikiwidb.h"

namespace pikiwidb {

PRaft& PRaft::Instance() {
  static PRaft store;
  return store;
}

butil::Status PRaft::Init(std::string& group_id, bool initial_conf_is_null) {
  if (node_ && server_) {
    return {0, "OK"};
  }

  server_ = std::make_unique<brpc::Server>();
  DummyServiceImpl service(&PRAFT);
  auto port = g_config.port + pikiwidb::g_config.raft_port_offset;
  // Add your service into RPC server
  if (server_->AddService(&service, 
                        brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
      LOG(ERROR) << "Fail to add service";
      return {EINVAL, "Fail to add service"};
  }
  // raft can share the same RPC server. Notice the second parameter, because
  // adding services into a running server is not allowed and the listen
  // address of this server is impossible to get before the server starts. You
  // have to specify the address of the server.
  if (braft::add_service(server_.get(), port) != 0) {
      LOG(ERROR) << "Fail to add raft service";
      return {EINVAL, "Fail to add raft service"};
  }

  // It's recommended to start the server before Counter is started to avoid
  // the case that it becomes the leader while the service is unreacheable by
  // clients.
  // Notice the default options of server is used here. Check out details from
  // the doc of brpc if you would like change some options;
  if (server_->Start(port, nullptr) != 0) {
      LOG(ERROR) << "Fail to start Server";
      return {EINVAL, "Fail to start Server"};
  }

  // It's ok to start PRaft;
  assert(group_id.size() == RAFT_DBID_LEN);
  this->dbid_ = group_id;

  // FIXME: g_config.ip is default to 127.0.0.0, which may not work in cluster.
  raw_addr_ = g_config.ip + ":" + std::to_string(port);
  butil::ip_t ip;
  auto ret = butil::str2ip(g_config.ip.c_str(), &ip);
  if (ret != 0) {
    LOG(ERROR) << "Fail to covert str_ip to butil::ip_t";
    return {EINVAL, "Fail to covert str_ip to butil::ip_t"};
  }
  butil::EndPoint addr(ip, port);

  // Default init in one node.
  /*
  initial_conf takes effect only when the replication group is started from an empty node. 
  The Configuration is restored from the snapshot and log files when the data in the replication group is not empty. 
  initial_conf is used only to create replication groups. 
  The first node adds itself to initial_conf and then calls add_peer to add other nodes. 
  Set initial_conf to empty for other nodes. 
  You can also start empty nodes simultaneously by setting the same inital_conf(ip:port of multiple nodes) for multiple nodes.
  */
  std::string initial_conf("");
  if (!initial_conf_is_null) {
    initial_conf = raw_addr_ + ":0,";
  }
  if (node_options_.initial_conf.parse_from(initial_conf) != 0) {
    LOG(ERROR) << "Fail to parse configuration, address: " << raw_addr_;
    return {EINVAL, "Fail to parse address."};
  }

  // node_options_.election_timeout_ms = FLAGS_election_timeout_ms;
  node_options_.fsm = this;
  node_options_.node_owns_fsm = false;
  // node_options_.snapshot_interval_s = FLAGS_snapshot_interval;
  std::string prefix = "local://" + g_config.dbpath + "_praft";
  node_options_.log_uri = prefix + "/log";
  node_options_.raft_meta_uri = prefix + "/raft_meta";
  node_options_.snapshot_uri = prefix + "/snapshot";
  // node_options_.disable_cli = FLAGS_disable_cli;
  node_ = std::make_unique<braft::Node>("pikiwidb", braft::PeerId(addr)); // group_id
  if (node_->init(node_options_) != 0) {
    node_.reset();
    LOG(ERROR) << "Fail to init raft node";
    return {EINVAL, "Fail to init raft node"};
  }

  return {0, "OK"};
}

bool PRaft::IsLeader() const {
  if (!node_) {
    LOG(ERROR) << "Node is not initialized";
    return false;
  }
  return node_->is_leader();
}

std::string PRaft::GetLeaderId() const {
  if (!node_) {
    LOG(ERROR) << "Node is not initialized";
    return std::string("Fail to get leader id");
  }
  return node_->leader_id().to_string();
}

std::string PRaft::GetNodeId() const {
  if (!node_) {
    LOG(ERROR) << "Node is not initialized";
    return std::string("Fail to get node id");
  }
  return node_->node_id().to_string();
}

std::string PRaft::GetGroupId() const {
  if (!node_) {
    LOG(ERROR) << "Node is not initialized";
    return std::string("Fail to get cluster id");
  }
  return dbid_;
}

braft::NodeStatus PRaft::GetNodeStatus() const {
  braft::NodeStatus node_status;
  if (!node_) {
    LOG(ERROR) << "Node is not initialized";
  } else {
    node_->get_status(&node_status);
  }

  return node_status;
}

butil::Status PRaft::GetListPeers(std::vector<braft::PeerId>* peers) {
  if (!node_) {
    LOG(ERROR) << "Node is not initialized";
    return {EINVAL, "Node is not initialized"};
  }
  return node_->list_peers(peers);
}

// Gets the cluster id, which is used to initialize node
void PRaft::SendNodeInfoRequest(PClient *client) {
  assert(client);

  UnboundedBuffer req;
  req.PushData("INFO raft", 9);
  req.PushData("\r\n", 2);
  client->SendPacket(req);
}

void PRaft::SendNodeAddRequest(PClient *client) {
  assert(client);

  // Node id in braft are ip:port, the node id param in RAFT.NODE ADD cmd will be ignored.
  int unused_node_id = 0;
  auto port = g_config.port + pikiwidb::g_config.raft_port_offset;
  auto raw_addr = g_config.ip + ":" + std::to_string(port);
  UnboundedBuffer req;
  req.PushData("RAFT.NODE ADD ", 14);
  req.PushData(std::to_string(unused_node_id).c_str(), std::to_string(unused_node_id).size());
  req.PushData(" ", 1);
  req.PushData(raw_addr.data(), raw_addr.size());
  req.PushData("\r\n", 2);
  client->SendPacket(req);
}

std::tuple<int, bool> PRaft::ProcessClusterJoinCmdResponse(PClient* client, const char* start, int len) {
  assert(start);
  auto join_client = join_ctx_.GetClient();
  if (!join_client) {
    LOG(WARNING) << "No client when processing cluster join cmd response.";
    return std::make_tuple(0, true);
  }

  bool is_disconnect = true;
  std::string reply(start, len);
  if (reply.find("+OK") != std::string::npos) {
    LOG(INFO) << "Joined Raft cluster, node id:" << PRAFT.GetNodeId() << "dbid:" << PRAFT.dbid_;
    join_client->SetRes(CmdRes::kOK);
    join_client->SendPacket(join_client->Message());
    is_disconnect = false;
  } else if (reply.find("-ERR wrong leader") != std::string::npos) {
    // Resolve the ip address of the leader
    pstd::StringTrimLeft(reply, "-ERR wrong leader");
    pstd::StringTrim(reply);
    braft::PeerId peerId;
    peerId.parse(reply);

    // Establish a connection with the leader and send the add request
    auto on_new_conn = [](TcpConnection* obj) {
      if (g_pikiwidb) {
        g_pikiwidb->OnNewConnection(obj);
      }
    };
    auto fail_cb = [&](EventLoop* loop, const char* peer_ip, int port) {
      PRAFT.OnJoinCmdConnectionFailed(loop, peer_ip, port);
    };

    auto loop = EventLoop::Self();
    auto peer_ip = std::string(butil::ip2str(peerId.addr.ip).c_str());
    auto port = peerId.addr.port;
    // FIXME: The client here is not smart pointer, may cause undefined behavior.
    // should use shared_ptr in DoCmd() rather than raw pointer.
    PRAFT.GetJoinCtx().Set(join_client, peer_ip, port);
    loop->Connect(peer_ip.c_str(), port, on_new_conn, fail_cb);

    // Not reply any message here, we will reply after the connection is established.
    join_client->Clear();
  } else if (reply.find("raft_group_id") != std::string::npos) {
    std::string prefix = "raft_group_id:";
    std::string::size_type prefix_length = prefix.length();
    std::string::size_type group_id_start = reply.find(prefix);
    group_id_start += prefix_length;  // 定位到raft_group_id的起始位置
    std::string::size_type group_id_end = reply.find("\r\n", group_id_start);
    if (group_id_end != std::string::npos) {
      std::string raft_group_id = reply.substr(group_id_start, group_id_end - group_id_start);
      // initialize the slave node
      auto s = PRAFT.Init(raft_group_id, true);
      if (!s.ok()) {
        join_client->SetRes(CmdRes::kErrOther, s.error_str());
        join_client->SendPacket(join_client->Message());
        return std::make_tuple(len, is_disconnect);
      }

      PRAFT.SendNodeAddRequest(client);
      is_disconnect = false;
    } else {
      LOG(ERROR) << "Joined Raft cluster fail, because of invalid raft_group_id";
      join_client->SetRes(CmdRes::kErrOther, "Invalid raft_group_id");
      join_client->SendPacket(join_client->Message());
    }
  } else {
    LOG(ERROR) << "Joined Raft cluster fail, " << start;
    join_client->SetRes(CmdRes::kErrOther, std::string(start, len));
    join_client->SendPacket(join_client->Message());
  }

  return std::make_tuple(len, is_disconnect);
}

butil::Status PRaft::AddPeer(const std::string& peer) {
  if (!node_) {
    LOG(ERROR) << "Node is not initialized";
    return {EINVAL, "Node is not initialized"};
  }

  braft::SynchronizedClosure done;
  node_->add_peer(peer, &done);
  done.wait();

  if (!done.status().ok()) {
    LOG(WARNING) << "Fail to add peer " << peer << " to node " << node_->node_id() << ", status " << done.status();
    return done.status();
  }

  return {0, "OK"};
}

butil::Status PRaft::RemovePeer(const std::string& peer) {
  if (!node_) {
    LOG(ERROR) << "Node is not initialized";
    return {EINVAL, "Node is not initialized"};
  }

  braft::SynchronizedClosure done;
  node_->remove_peer(peer, &done);
  done.wait();

  if (!done.status().ok()) {
    LOG(WARNING) << "Fail to remove peer " << peer << " from node " << node_->node_id() << ", status " << done.status();
    return done.status();
  }

  return {0, "OK"};
}

void PRaft::OnJoinCmdConnectionFailed([[maybe_unused]] EventLoop* loop, const char* peer_ip, int port) {
  auto cli = join_ctx_.GetClient();
  if (cli) {
    cli->SetRes(CmdRes::kErrOther,
                "ERR failed to connect to cluster for join, please check logs " + std::string(peer_ip) + ":" + std::to_string(port));
    cli->SendPacket(cli->Message());
  }
}

// Shut this node and server down.
void PRaft::ShutDown() {
  if (node_) {
    node_->shutdown(nullptr);
  }

  if (server_) {
    server_->Stop(0);
  }
}

// Blocking this thread until the node is eventually down.
void PRaft::Join() {
  if (node_) {
    node_->join();
  }

  if (server_) {
    server_->Join();
  }
}

void PRaft::Apply(braft::Task& task) {
  if (node_) {
    node_->apply(task);
  }
}

// @braft::StateMachine
void PRaft::on_apply(braft::Iterator& iter) {
  // A batch of tasks are committed, which must be processed through
  // |iter|
  for (; iter.valid(); iter.next()) {
  }
}

void PRaft::on_snapshot_save(braft::SnapshotWriter* writer, braft::Closure* done) {}

int PRaft::on_snapshot_load(braft::SnapshotReader* reader) { return 0; }

void PRaft::on_leader_start(int64_t term) {
  LOG(WARNING) << "Node " << node_->node_id() << "start to be leader, term" << term;
}

void PRaft::on_leader_stop(const butil::Status& status) {}

void PRaft::on_shutdown() {}
void PRaft::on_error(const ::braft::Error& e) {}
void PRaft::on_configuration_committed(const ::braft::Configuration& conf) {}
void PRaft::on_stop_following(const ::braft::LeaderChangeContext& ctx) {}
void PRaft::on_start_following(const ::braft::LeaderChangeContext& ctx) {}

}  // namespace pikiwidb