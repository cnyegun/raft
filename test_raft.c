#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "raft.h"

static void destroy_node(raft_node_t *node) {
  if (node == NULL) {
    return;
  }

  free(node->next_index);
  free(node->match_index);
  free(node->log);
  free(node);
}

static void test_init_raft_node_sets_defaults(void) {
  raft_node_t *node = init_raft_node(2, 5);

  assert(node != NULL);
  assert(node->id == 2);
  assert(node->num_peers == 5);
  assert(node->role == FOLLOWER);
  assert(node->current_term == 0);
  assert(node->voted_for == -1);
  assert(node->log != NULL);
  assert(node->log_length == 0);
  assert(node->log_capacity == INITIAL_LOG_CAPACITY);
  assert(node->commit_index == 0);
  assert(node->last_applied == 0);
  assert(node->next_index == NULL);
  assert(node->match_index == NULL);

  destroy_node(node);
}

static void test_become_leader_initializes_leader_state(void) {
  raft_node_t *node = init_raft_node(1, 3);

  node->log_length = 4;
  become_leader(node);

  assert(node->role == LEADER);
  assert(node->next_index != NULL);
  assert(node->match_index != NULL);

  for (int i = 0; i < node->num_peers; i++) {
    assert(node->next_index[i] == 4);
    assert(node->match_index[i] == 0);
  }

  destroy_node(node);
}

static void test_step_down_resets_leader_state(void) {
  raft_node_t *node = init_raft_node(1, 3);

  node->voted_for = 2;
  node->log_length = 3;
  become_leader(node);
  step_down(node, 7);

  assert(node->role == FOLLOWER);
  assert(node->current_term == 7);
  assert(node->voted_for == -1);
  assert(node->next_index == NULL);
  assert(node->match_index == NULL);

  destroy_node(node);
}

static void test_handle_request_vote_rejects_stale_term(void) {
  raft_node_t *node = init_raft_node(1, 3);
  request_vote_args_t args = {
    .candidate_term = 4,
    .candidate_id = 2,
    .term_last_entry = 0,
    .index_last_entry = 0,
  };

  node->current_term = 5;

  request_vote_reply_t reply = handle_request_vote(node, args);

  assert(reply.term == 5);
  assert(reply.vote_granted == false);
  assert(node->voted_for == -1);

  destroy_node(node);
}

static void test_handle_request_vote_steps_down_on_higher_term(void) {
  raft_node_t *node = init_raft_node(1, 3);
  request_vote_args_t args = {
    .candidate_term = 4,
    .candidate_id = 2,
    .term_last_entry = 0,
    .index_last_entry = 0,
  };

  node->current_term = 3;
  node->voted_for = 1;
  become_leader(node);

  request_vote_reply_t reply = handle_request_vote(node, args);

  assert(reply.term == 4);
  assert(reply.vote_granted == true);
  assert(node->role == FOLLOWER);
  assert(node->current_term == 4);
  assert(node->voted_for == 2);
  assert(node->next_index == NULL);
  assert(node->match_index == NULL);

  destroy_node(node);
}

static void test_handle_request_vote_rejects_if_already_voted_for_other_candidate(void) {
  raft_node_t *node = init_raft_node(1, 3);
  request_vote_args_t args = {
    .candidate_term = 5,
    .candidate_id = 4,
    .term_last_entry = 0,
    .index_last_entry = 0,
  };

  node->current_term = 5;
  node->voted_for = 3;

  request_vote_reply_t reply = handle_request_vote(node, args);

  assert(reply.term == 5);
  assert(reply.vote_granted == false);
  assert(node->voted_for == 3);

  destroy_node(node);
}

static void test_handle_request_vote_rejects_stale_log_term(void) {
  raft_node_t *node = init_raft_node(1, 3);
  request_vote_args_t args = {
    .candidate_term = 5,
    .candidate_id = 2,
    .term_last_entry = 2,
    .index_last_entry = 0,
  };

  node->current_term = 5;
  node->log_length = 1;
  node->log[0].term = 3;

  request_vote_reply_t reply = handle_request_vote(node, args);

  assert(reply.term == 5);
  assert(reply.vote_granted == false);
  assert(node->voted_for == -1);

  destroy_node(node);
}

static void test_handle_request_vote_rejects_shorter_log_when_terms_match(void) {
  raft_node_t *node = init_raft_node(1, 3);
  request_vote_args_t args = {
    .candidate_term = 5,
    .candidate_id = 2,
    .term_last_entry = 4,
    .index_last_entry = 1,
  };

  node->current_term = 5;
  node->log_length = 3;
  node->log[2].term = 4;

  request_vote_reply_t reply = handle_request_vote(node, args);

  assert(reply.term == 5);
  assert(reply.vote_granted == false);
  assert(node->voted_for == -1);

  destroy_node(node);
}

static void test_handle_request_vote_grants_up_to_date_log(void) {
  raft_node_t *node = init_raft_node(1, 3);
  request_vote_args_t args = {
    .candidate_term = 5,
    .candidate_id = 2,
    .term_last_entry = 4,
    .index_last_entry = 2,
  };

  node->current_term = 5;
  node->log_length = 3;
  node->log[2].term = 4;

  request_vote_reply_t reply = handle_request_vote(node, args);

  assert(reply.term == 5);
  assert(reply.vote_granted == true);
  assert(node->voted_for == 2);

  destroy_node(node);
}

static void test_handle_append_entries_demotes_leader_and_clears_leader_state_on_equal_term(void) {
  raft_node_t *node = init_raft_node(1, 3);
  append_entries_args_t args;

  args.leader_term = 5;
  args.leader_id = 2;
  args.prev_log_term = 0;
  args.prev_log_index = -1;
  args.new_entries = NULL;
  args.entries_count = 0;
  args.commit_index = 0;

  node->current_term = 5;
  become_leader(node);

  assert(node->role == LEADER);
  assert(node->next_index != NULL);
  assert(node->match_index != NULL);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == true);
  assert(reply.follower_term == 5);
  assert(node->role == FOLLOWER);
  assert(node->next_index == NULL);
  assert(node->match_index == NULL);

  destroy_node(node);
}

int main(void) {
  test_init_raft_node_sets_defaults();
  test_become_leader_initializes_leader_state();
  test_step_down_resets_leader_state();
  test_handle_request_vote_rejects_stale_term();
  test_handle_request_vote_steps_down_on_higher_term();
  test_handle_request_vote_rejects_if_already_voted_for_other_candidate();
  test_handle_request_vote_rejects_stale_log_term();
  test_handle_request_vote_rejects_shorter_log_when_terms_match();
  test_handle_request_vote_grants_up_to_date_log();
  test_handle_append_entries_demotes_leader_and_clears_leader_state_on_equal_term();

  puts("all tests passed");
  return 0;
}
