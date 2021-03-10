// Copyright 2021 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef XLS_NOC_SIMULATION_SIM_OBJECTS_H_
#define XLS_NOC_SIMULATION_SIM_OBJECTS_H_

#include <queue>
#include <vector>

#include "absl/status/statusor.h"
#include "xls/common/integral_types.h"
#include "xls/noc/simulation/common.h"
#include "xls/noc/simulation/global_routing_table.h"
#include "xls/noc/simulation/parameters.h"

// This file contains classes used to store, access, and define simulation
// objects.  Each network object (defined network_graph.h) is associated
// with a simulation object, depending on how said network object is
// configured via its parameters (see parameters.h).

namespace xls {
namespace noc {

// Represents a phit being sent from a source to a sink (forward).
struct DataPhit {
  // TODO(tedhong) : 2020-01-24 - Convert to use Bits/DSLX structs.
  // TODO(tedhong) : 2020-02-20 - Add fluent phit builder to initialize struct.
  bool valid;
  int16 destination_index;
  int16 vc;
  int64 data;
};

// Associates a phit with a time (cycle).
struct TimedDataPhit {
  int64 cycle;
  DataPhit phit;
};

// Represents a phit being used for metadata (i.e. credits).
struct MetadataPhit {
  // TODO(tedhong) : 2020-01-24 - Convert to use Bits/DSLX structs.
  bool valid;
  int64 data;
};

// Associates a metadata phit with a time (cycle).
struct TimedMetadataPhit {
  int64 cycle;
  MetadataPhit phit;
};

// Used to store the state of phits in-flight for a network.
// It is associated with a ConnectionId which connects two ports.
struct SimConnectionState {
  ConnectionId id;
  TimedDataPhit forward_channels;
  std::vector<TimedMetadataPhit> reverse_channels;
};

// Used to store the valid credit available at a certain time.
struct CreditState {
  int64 cycle;
  int64 credit;
};

// Represents a fifo/buffer used to store phits.
struct DataFlitQueue {
  std::queue<DataPhit> queue;
  int64 max_queue_size;
};

// Represents a fifo/buffer used to store metadata phits.
struct MetadataFlitQueue {
  std::queue<MetadataPhit> queue;
  int64 max_queue_size;
};

class NocSimulator;

// Common functionality and base class for all simulator objects.
class SimNetworkComponentBase {
 public:
  // Perform a single tick of simulator.
  // Returns true if the component has converged for the given cycle.
  //
  // A component has converged it both forward and reverse propgation
  // have completed.  This means that all SimConnectionState objects
  // attached to this component have state associated with the current.
  // cycle.
  //
  // See NocSimulator::Tick.
  bool Tick(NocSimulator& simulator);

  // Returns the associated NetworkComponentId.
  NetworkComponentId GetId() { return id_; }

  virtual ~SimNetworkComponentBase() = default;

 protected:
  SimNetworkComponentBase() = default;

  // Initialize this simulator object.
  //
  // After initialization, the simulator object will be set up to
  // simulate the specific component as described in the protos.
  //
  // For example, buffer sizes and the number of virtual channels will
  // be read from NOC config protos to properly size the simulation object.
  absl::Status Initialize(NetworkComponentId nc_id, NocSimulator& simulator) {
    id_ = nc_id;
    forward_propagated_cycle_ = -1;
    reverse_propagated_cycle_ = -1;

    return InitializeImpl(simulator);
  }

  // Component specific initialization of a SimNetworkComponent.
  virtual absl::Status InitializeImpl(NocSimulator& simulator) {
    return absl::OkStatus();
  }

  // Propagates simulation state from source connections to sink.
  // Returns true if ready and simulation state was propagated.
  //
  // True can be returned if 1) all input ports are ready for forward
  // propagation (input port's connection forward_channel time stamp
  // equals current cycle), and 2) all output port state have been updated
  // (output port connection's forward_channel time stamp equals the current
  // cycle).
  //
  // A simulation cycle is complete once all component's Forward and
  // Reverse propagation methods return true.
  virtual bool TryForwardPropagation(NocSimulator& simulator) { return true; }

  // Propagates simulation state from sink connections to source.
  // Returns true if ready and simulation state was propagated.
  //
  // True can be returned if 1) all output ports are ready for reverse
  // propagation (output port's connection reverse_channel time stamp
  // equals current cycle) , and 2) all input port state have been updated
  // (input port connection's reverse channel time stamp equals the current
  // cycle).
  virtual bool TryReversePropagation(NocSimulator& simulator) { return true; }

  NetworkComponentId id_;
  int64 forward_propagated_cycle_;
  int64 reverse_propagated_cycle_;
};

// A pair of pipeline stages connecting two ports/network components.
//
// DataPhits are propagated forward, while MetaDataPhits are propagated
// backwards.
class SimLink : public SimNetworkComponentBase {
 public:
  static absl::StatusOr<SimLink> Create(NetworkComponentId nc_id,
                                        NocSimulator& simulator) {
    SimLink ret;
    XLS_RETURN_IF_ERROR(ret.Initialize(nc_id, simulator));
    return ret;
  }

 private:
  SimLink() = default;

  absl::Status InitializeImpl(NocSimulator& simulator) override;

  bool TryForwardPropagation(NocSimulator& simulator) override;
  bool TryReversePropagation(NocSimulator& simulator) override;

  int64 forward_pipeline_stages_;
  int64 reverse_pipeline_stages_;

  // TODO(tedhong): 2020-01-25 support phit_width, currently unused.
  int64 phit_width_;

  int64 src_connection_index_;
  int64 sink_connection_index_;

  std::queue<DataPhit> forward_data_stages_;

  std::vector<std::queue<MetadataPhit>> reverse_credit_stages_;
};

// Source - injects traffic into the network.
class SimNetworkInterfaceSrc : public SimNetworkComponentBase {
 public:
  static absl::StatusOr<SimNetworkInterfaceSrc> Create(
      NetworkComponentId nc_id, NocSimulator& simulator) {
    SimNetworkInterfaceSrc ret;
    XLS_RETURN_IF_ERROR(ret.Initialize(nc_id, simulator));
    return ret;
  }

  // Register a phit to be sent at a specific time.
  absl::Status SendPhitAtTime(TimedDataPhit phit);

 private:
  SimNetworkInterfaceSrc() = default;

  absl::Status InitializeImpl(NocSimulator& simulator) override;
  bool TryForwardPropagation(NocSimulator& simulator) override;
  bool TryReversePropagation(NocSimulator& simulator) override;

  int64 sink_connection_index_;
  std::vector<int64> credit_;
  std::vector<CreditState> credit_update_;
  std::vector<std::queue<TimedDataPhit>> data_to_send_;
};

// Sink - traffic leaves the network via a sink.
class SimNetworkInterfaceSink : public SimNetworkComponentBase {
 public:
  static absl::StatusOr<SimNetworkInterfaceSink> Create(
      NetworkComponentId nc_id, NocSimulator& simulator) {
    SimNetworkInterfaceSink ret;
    XLS_RETURN_IF_ERROR(ret.Initialize(nc_id, simulator));
    return ret;
  }

  // Returns all traffic received by this sink from the beginning
  // of the simulation.
  absl::Span<const TimedDataPhit> GetReceivedTraffic() {
    return received_traffic_;
  }

 private:
  SimNetworkInterfaceSink() = default;

  absl::Status InitializeImpl(NocSimulator& simulator) override;

  bool TryForwardPropagation(NocSimulator& simulator) override;

  int64 src_connection_index_;
  std::vector<DataFlitQueue> input_buffers_;
  std::vector<TimedDataPhit> received_traffic_;
};

// Represents a input-buffered, fixed priority, credit-based, virtual-channel
// router.
//
// This router implements a spcific type of router used by the simulator.
// Additional routers are implemented ether as a separate class or
// by configuring this class.
//
// Specific features include
//   - Input buffered - phits are buffered at the input.
//   - Input bypass - a phit can enter the router and leave on the same cycle.
//   - Credits - the router keeps track of the absolute credit count and
//               expects incremental updates from the components downstream.
//             - credits are registered so there is a one-cycle delay
//               from when the credit is received and the credit count
//               updated.
//             - the router likewise sends credit updates upstream.
//   - Dedicated credit channels - Each vc is associated with an indepenendent
//                                 channel for credit updates.
//   - Output bufferless - once a phit is arbitrated for, the phit is
//                         immediately transfered downstream.
//   - Fixed priority - a fixed priority scheme is implemented.
// TODO(tedhong): 2021-01-31 - Add support for alternative prority scheme.
class SimInputBufferedVCRouter : public SimNetworkComponentBase {
 public:
  static absl::StatusOr<SimInputBufferedVCRouter> Create(
      NetworkComponentId nc_id, NocSimulator& simulator) {
    SimInputBufferedVCRouter ret;
    XLS_RETURN_IF_ERROR(ret.Initialize(nc_id, simulator));
    return ret;
  }

 private:
  SimInputBufferedVCRouter() = default;

  // Represents a specific input or output location.
  struct PortIndexAndVCIndex {
    int64 port_index;
    int64 vc_index;
  };

  absl::Status InitializeImpl(NocSimulator& simulator) override;

  // Forward propagation
  //  1. Updates the credit count (internal propagation)
  //  2. Waits until all input ports are ready.
  //  3. Enqueues phits into input buffers and performs routing if able.
  bool TryForwardPropagation(NocSimulator& simulator) override;

  // Reverse propagation
  //  1. Sends credits back upstream (due to fwd propagation routing phits).
  //  2. Registers credits received from downstream.
  bool TryReversePropagation(NocSimulator& simulator) override;

  // Perform the routing function of this router.
  //
  // Returns a pair of <output_port_index, output_vc_index> -- the
  // output port and vc a phit should go out on given the input port and vc
  // along with the eventual phit destination.
  absl::StatusOr<PortIndexAndVCIndex> GetDestinationPortIndexAndVcIndex(
      NocSimulator& simulator, PortIndexAndVCIndex input,
      int64 destination_index);

  // Index for the input connections associated with this router.
  // Each input port is associated with a single connection.
  int64 input_connection_index_start_;
  int64 input_connection_count_;

  // Index for the output connections associated with this router.
  // Each output port is associated with a single connection.
  int64 output_connection_index_start_;
  int64 output_connection_count_;

  // The router as finished internal propagation once it has
  // updated its credit count from the updates received in the previous cycle.
  int64 internal_propagated_cycle_;

  // Stores the input buffers associated with each input port and vc.
  std::vector<std::vector<DataFlitQueue>> input_buffers_;

  // Stores the credit count associated with each output port and vc.
  // Each cycle, the router updates its credit count from credit_update_.
  std::vector<std::vector<int64>> credit_;

  // Stores the credit count received on cycle N-1.
  std::vector<std::vector<CreditState>> credit_update_;

  // The maximum number of vcs on for an input port.
  // Used for the priority scheme implementation.
  int64 max_vc_;

  // Used by forward propagation to store the number of phits that left
  // the input buffers and hence credits that can be sent back upstream.
  std::vector<std::vector<int64>> input_credit_to_send_;
};

// Main simulator class that drives the simulation and stores simulation
// state and objects.
class NocSimulator {
 public:
  NocSimulator()
      : mgr_(nullptr), params_(nullptr), routing_(nullptr), cycle_(-1) {}

  // Creates all simulation objects for a given network.
  // NetworkManager, NocParameters, and DistributedRoutingTable should
  // have aleady been setup.
  absl::Status Initialize(NetworkManager& mgr, NocParameters& params,
                          DistributedRoutingTable& routing, NetworkId network) {
    mgr_ = &mgr;
    params_ = &params;
    routing_ = &routing;
    network_ = network;
    cycle_ = -1;

    return CreateSimulationObjects(network);
  }

  NetworkManager* GetNetworkManager() { return mgr_; }
  NocParameters* GetNocParameters() { return params_; }
  DistributedRoutingTable* GetRoutingTable() { return routing_; }

  // Maps a given connection id to its index in the connection store.
  int64 GetConnectionIndex(ConnectionId id) {
    return connection_index_map_[id];
  }

  // Returns a SimConnectionState given an index.
  SimConnectionState& GetSimConnectionByIndex(int64 index) {
    return connections_[index];
  }

  // Allocates and returns a new SimConnectionState object.
  SimConnectionState& NewConnection(ConnectionId id) {
    int64 index = connections_.size();
    connections_.resize(index + 1);
    connection_index_map_[id] = index;
    return GetSimConnectionByIndex(index);
  }

  // Returns a reference to the store previously reserved with
  // GetNewConnectionIndicesStore.
  absl::Span<int64> GetConnectionIndicesStore(int64 start, int64 size = 1) {
    return absl::Span<int64>(component_to_connection_index_.data() + start,
                             size);
  }

  // Allocates and returns an index that can then be used
  // with GetConnectionIndicesStore to retrieve an array of size.
  int64 GetNewConnectionIndicesStore(int64 size) {
    int64 next_start = component_to_connection_index_.size();
    component_to_connection_index_.resize(next_start + size);
    return next_start;
  }

  // Allocates and returns an index that can be used with
  // GetPortIdStore to retreive an array of size)
  int64 GetNewPortIdStore(int64 size) {
    int64 next_start = port_id_store_.size();
    port_id_store_.resize(next_start + size);
    return next_start;
  }

  // Returns a reference to the store previously reserved with
  // GetNewConnectionIndicesStore.
  absl::Span<PortId> GetPortIdStore(int64 start, int64 size = 1) {
    return absl::Span<PortId>(port_id_store_.data() + start, size);
  }

  // Returns current/in-progress cycle;
  int64 GetCurrentCycle() { return cycle_; }

  // Logs the current simulation state.
  void Dump();

  // Run a single cycle of the simulator.
  absl::Status RunCycle(int64 max_ticks = 9999);

  // Runs a single tick of the simulator.
  bool Tick();

  // Returns corresponding simulation object for a src network component.
  absl::StatusOr<SimNetworkInterfaceSrc*> GetSimNetworkInterfaceSrc(
      NetworkComponentId src);

  // Returns corresponding simulation object for a sink network component.
  absl::StatusOr<SimNetworkInterfaceSink*> GetSimNetworkInterfaceSink(
      NetworkComponentId sink);

 private:
  absl::Status CreateSimulationObjects(NetworkId network);
  absl::Status CreateConnection(ConnectionId connection_id);
  absl::Status CreateNetworkComponent(NetworkComponentId nc_id);
  absl::Status CreateNetworkInterfaceSrc(NetworkComponentId nc_id);
  absl::Status CreateNetworkInterfaceSink(NetworkComponentId nc_id);
  absl::Status CreateLink(NetworkComponentId nc_id);
  absl::Status CreateRouter(NetworkComponentId nc_id);

  NetworkManager* mgr_;
  NocParameters* params_;
  DistributedRoutingTable* routing_;

  NetworkId network_;
  int64 cycle_;

  // Map a specific ConnectionId to an index used to access
  // a specific SimConnectionState via the connections_ object.
  absl::flat_hash_map<ConnectionId, int64> connection_index_map_;

  // Map a network interface src to a SimNetworkInterfaceSrc.
  absl::flat_hash_map<NetworkComponentId, int64> src_index_map_;

  // Map a network interface sink to a SimNetworkInterfaceSink.
  absl::flat_hash_map<NetworkComponentId, int64> sink_index_map_;

  // Used by network components to store an array of indices.
  //
  // Those indices are used to index into the connection_ object to
  // access a SimConnectionState.
  //
  // For example, a router can reserve space so that for port x
  //  connections_[component_to_connection_index_[x]] is then the
  //  corresponding SimConnectionState for said port.
  std::vector<int64> component_to_connection_index_;
  std::vector<SimConnectionState> connections_;

  // Stores port ids for routers.
  std::vector<PortId> port_id_store_;

  std::vector<SimLink> links_;
  std::vector<SimNetworkInterfaceSrc> network_interface_sources_;
  std::vector<SimNetworkInterfaceSink> network_interface_sinks_;
  std::vector<SimInputBufferedVCRouter> routers_;
};

}  // namespace noc
}  // namespace xls

#endif  // XLS_NOC_SIMULATION_SIM_OBJECTS_H_