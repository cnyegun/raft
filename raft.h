#ifndef RAFT_H
#define RAFT_H

#include <stdbool.h>

#define INITIAL_LOG_CAPACITY 8

typedef enum { FOLLOWER, CANDIDATE, LEADER } server_state_t;

typedef struct {
  // Term when the leader created this entry.
  int term;
  // Command to apply to the replicated state machine.
  char command[256];
} log_entry_t;

/* In-memory log storage.
 *
 * Raft log indexes are 1-based, but the C array is 0-based.
 * That means Raft index i is stored at entries[i - 1].
 * If log.length == 0, the last Raft log index is 0.
 */
typedef struct {
  // Dynamic array of log entries.
  log_entry_t *entries;
  // Number of valid entries currently stored.
  int length;
  // Number of allocated entry slots.
  int capacity;
} log_t;

typedef struct {
  /* Persistent Raft state on all servers.
   * In a full implementation, this state must be saved to stable storage
   * before replying to RPCs.
   */

  // Latest term this server has seen.
  int current_term;

  // Candidate ID that received this server's vote in the current term.
  // Use -1 if this server has not voted yet.
  int voted_for;

  // Replicated log stored in memory.
  log_t log;
  
  /* Volatile Raft state on all servers. */
  
  // Index of the highest log entry known to be committed.
  // This value increases monotonically.
  int commit_index;

  // Index of the highest log entry applied to the state machine.
  // This value increases monotonically.
  int last_applied;

  /* Volatile leader-only state.
   * These arrays are reinitialized whenever a node becomes leader.
   */
  
  // For each peer, the next log index the leader should send.
  // Initialize each slot to the leader's last log index + 1.
  int *next_index;

  // For each peer, the highest log index known to be replicated there.
  // Initialize each slot to 0. This value increases monotonically.
  int *match_index;

  /* Candidate-only implementation state. */

  // Number of granted votes seen in the current election.
  int votes_received;

  // Current server state: follower, candidate, or leader.
  server_state_t state;

  // Unique ID for this node.
  int id;

  // Number of OTHER nodes in the cluster, excluding this node.
  // Total cluster size = num_peers + 1
  // Majority = (num_peers + 1) / 2 + 1
  int num_peers;
} raft_node_t;

// Allocate and initialize a new Raft node.
raft_node_t *init_raft_node(int id, int num_peers);

// Transition a node into the leader role and initialize leader-only state.
void become_leader(raft_node_t *node);

// Convert a node to follower state and update its current term.
void convert_to_follower(raft_node_t *node, int new_term);

/* RequestVote RPC types. */
typedef struct {
  // Candidate's current term.
  int term;

  // ID of the candidate requesting the vote.
  int candidate_id;

  // Index of the candidate's last log entry.
  int last_log_index;

  // Term of the candidate's last log entry.
  int last_log_term;
} request_vote_rpc_args_t;

typedef struct {
  // Receiver's current term.
  int term;

  // True if the receiver granted its vote.
  bool vote_granted;
} request_vote_rpc_reply_t;

/* AppendEntries RPC types. */
typedef struct {
  // Leader's current term.
  int term;

  // Leader ID so followers can identify the current leader.
  int leader_id;

  // Index of the log entry immediately preceding the new entries.
  int prev_log_index;

  // Term of the log entry immediately preceding the new entries.
  int prev_log_term;

  // Log entries to store. Use NULL with entries_count == 0 for a heartbeat.
  const log_entry_t *entries;

  // Number of entries stored in entries.
  int entries_count;

  // Leader's commit index.
  int leader_commit;
} append_entries_rpc_args_t;

typedef struct {
  // Current term, for the leader to update itself.
  int term;

  // True if the follower contained an entry matching prev_log_index and
  // prev_log_term.
  bool success;
} append_entries_rpc_reply_t;

// Process an incoming RequestVote RPC and return the receiver's reply.
request_vote_rpc_reply_t handle_request_vote_rpc(raft_node_t *node, request_vote_rpc_args_t args);

// Process an incoming AppendEntries RPC and return the receiver's reply.
append_entries_rpc_reply_t handle_append_entries_rpc(raft_node_t *node, append_entries_rpc_args_t args);

// Start a new election and build the outgoing RequestVote arguments.
request_vote_rpc_args_t start_election(raft_node_t *node);

// Process a RequestVote reply while this node is a candidate.
void handle_request_vote_reply(raft_node_t *node, request_vote_rpc_reply_t reply);

// Build an AppendEntries RPC for a specific peer.
append_entries_rpc_args_t build_append_entries_rpc(raft_node_t *node, int peer_index);

#endif
