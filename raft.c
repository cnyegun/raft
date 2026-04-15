#include "raft.h"
#include <assert.h>

raft_node_t *init_raft_node(int id, int num_peers) {
  raft_node_t *node = calloc(1, sizeof(raft_node_t));
  if (node == NULL) {
    perror("calloc failed");
    exit(EXIT_FAILURE);
  }
  node->id = id;
  node->num_peers = num_peers;
  node->role = FOLLOWER;
  node->voted_for = -1;
  node->vote_received = 0;
  node->log = malloc(INITIAL_LOG_CAPACITY * sizeof(log_entry_t));
  if (node->log == NULL) {
    perror("malloc failed");
    exit(EXIT_FAILURE);
  }
  node->log_capacity = INITIAL_LOG_CAPACITY;
  node->commit_index = -1;
  node->last_applied = -1;
  return node;
}

void become_leader(raft_node_t *node) {
  if (node == NULL) return;
  if (node->role == LEADER) return;
  node->role = LEADER; 
  int num_peers = node->num_peers;
  int log_length = node->log_length;
  node->next_index = malloc(num_peers * sizeof(int));
  if (node->next_index == NULL) {
    perror("malloc failed");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < num_peers; i++) {
    node->next_index[i] = log_length;
  }
  node->match_index = malloc(num_peers * sizeof(int));
  if (node->match_index == NULL) {
    perror("malloc failed");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < num_peers; i++) {
    node->match_index[i] = -1;
  }
}

void step_down(raft_node_t *node, int new_term) {
  if (node == NULL) return;
  node->current_term = new_term;
  node->voted_for = -1;
  if (node->role == LEADER) {
    free(node->next_index);
    free(node->match_index);
    node->next_index = NULL;
    node->match_index = NULL;
  }
  node->role = FOLLOWER;
  node->vote_received = 0;
}

request_vote_reply_t handle_request_vote(raft_node_t *node, request_vote_args_t args) {
  request_vote_reply_t reply; 
  reply.vote_granted = false;
  reply.term = -1;
  if (node == NULL) return reply;
  
  if (args.candidate_term > node->current_term) {
    step_down(node, args.candidate_term);
  }

  reply.term = node->current_term;

  // If candidate's term smaller than mine, reject
  if (args.candidate_term < node->current_term) {
    return reply;
  }

  // If already voted, reject
  if (node->voted_for != -1 && node->voted_for != args.candidate_id) {
    return reply;
  }

  // Compare logs
  if (node->log_length == 0) {
    reply.vote_granted = true;
    node->voted_for = args.candidate_id;
    return reply;
  }
  else if (node->log[node->log_length - 1].term > args.term_last_entry) {
    return reply;
  }
  else if (node->log[node->log_length - 1].term == args.term_last_entry) {
    // if two last entry term is equal we gonna compare the log length
    if (node->log_length - 1 > args.index_last_entry) {
      return reply;
    }
  }

  reply.vote_granted = true;
  node->voted_for = args.candidate_id;
  return reply;
}

append_entries_reply_t handle_append_entries(raft_node_t *node, append_entries_args_t args) {
  append_entries_reply_t reply;
  int append_index;
  reply.append_succeed = false;
  reply.follower_term = -1;
  if (node == NULL) return reply;

  // Follower's term is higher than leader's term
  if (node->current_term > args.leader_term) {
    reply.follower_term = node->current_term;
    return reply;
  }
  // Leader's term is higher or equal, need to step down
  if (node->current_term < args.leader_term)
    step_down(node, args.leader_term);

  if (node->role == LEADER) {
    free(node->next_index);
    free(node->match_index);
    node->next_index = NULL;
    node->match_index = NULL;
  }

  if (node->role != FOLLOWER)
    node->role = FOLLOWER;

  reply.follower_term = node->current_term;

  /* If prev_log_index == -1 skip the consistency check entirely
   * and append from the start 
   */
  if (args.prev_log_index == -1) {
    append_index = 0;
  }
  // Check for out of bound log access
  else if (args.prev_log_index > node->log_length - 1 || args.prev_log_index < 0) 
    return reply;
  // Check if previous log entry are the same term
  else if (args.prev_log_term == node->log[args.prev_log_index].term) {
    append_index = args.prev_log_index + 1;
  }
  else {
    return reply;
  }

  /* Appending */

  for (int i = 0; i < args.entries_count; i++) {
    int idx = append_index + i;
    // If log is full, double the size and realloc
    if (idx >= (node->log_capacity - 1)) {
      node->log = realloc(node->log, sizeof(log_entry_t) * node->log_capacity * 2);
      if (node->log == NULL) {
        perror("realloc error");
        exit(EXIT_FAILURE);
      }
      node->log_capacity *= 2;
    }

    // skips if a entry has the same term
    if (idx < node->log_length && node->log[idx].term == args.new_entries[i].term) {
      continue;
    }

    // truncate from this point if the entry's term is different
    if (idx < node->log_length) {
      node->log_length = idx;
    }

    node->log[idx] = args.new_entries[i];
    node->log_length++;
  }

  if (args.commit_index > node->commit_index) {
    node->commit_index = MIN(args.commit_index, node->log_length - 1);
  }

  reply.append_succeed = true;

  return reply;
}

request_vote_args_t start_election(raft_node_t *node) {
  assert(node != NULL);
  request_vote_args_t request = {0};

  // elects itself to candidate and vote for itself.
  node->role = CANDIDATE;
  node->current_term++;
  node->voted_for = node->id;
  node->vote_received = 1;

  request.candidate_id = node->id;
  request.candidate_term = node->current_term;
  request.index_last_entry = node->log_length - 1;
  request.term_last_entry = node->log_length > 0 ? (node->log[node->log_length - 1].term) : -1;

  return request;
}

void handle_vote_reply(raft_node_t *node, request_vote_reply_t reply) {
  if (node == NULL) return;

  if (node->role != CANDIDATE) return;
  
}