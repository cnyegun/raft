#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifndef RAFT_H
#define RAFT_H

#define INITIAL_LOG_CAPACITY 8

typedef enum { FOLLOWER, CANDIDATE, LEADER } role_t;

typedef struct {
  int term;
  char command[256];
} log_entry_t;

typedef struct {
  // Voting
  int current_term;
  int voted_for;

  // Log
  log_entry_t *log;
  int log_length;
  int log_capacity;
  
  // Volatile 
  int commit_index;
  int last_applied;

  // Leader-only state
  int *next_index;
  int *match_index;

  role_t role;
  int id;
  int num_peers;
} raft_node_t;

raft_node_t *init_raft_node(int id, int num_peers);
void become_leader(raft_node_t *node);
void step_down(raft_node_t *node, int new_term);

// RPC structs
typedef struct {
  int candidate_term;
  int candidate_id;
  int term_last_entry;
  int index_last_entry;
} request_vote_args_t;

typedef struct {
  int term;
  bool vote_granted;
} request_vote_reply_t;

typedef struct {
  int leader_term;
  int leader_id;
  int prev_log_term;
  int prev_log_index;
  log_entry_t *new_entries;
  int entries_count;
  int commit_index;
} append_entries_args_t;

typedef struct {
  int follower_term;
  bool append_succeed;
} append_entries_reply_t;

request_vote_reply_t handle_request_vote(raft_node_t *node, request_vote_args_t args);

#endif
