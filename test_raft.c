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

static log_entry_t make_entry(int term) {
  log_entry_t entry;
  memset(&entry, 0, sizeof(entry));
  entry.term = term;
  return entry;
}

static append_entries_args_t make_append_entries_args(
  int leader_term,
  int leader_id,
  int prev_log_term,
  int prev_log_index,
  log_entry_t *new_entries,
  int entries_count,
  int commit_index
) {
  append_entries_args_t args;
  args.leader_term = leader_term;
  args.leader_id = leader_id;
  args.prev_log_term = prev_log_term;
  args.prev_log_index = prev_log_index;
  args.new_entries = new_entries;
  args.entries_count = entries_count;
  args.commit_index = commit_index;
  return args;
}

static void seed_log_terms(raft_node_t *node, const int *terms, int count) {
  assert(count >= 0);
  assert(count <= node->log_capacity);

  node->log_length = count;
  for (int i = 0; i < count; i++) {
    node->log[i] = make_entry(terms[i]);
  }
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
  assert(node->commit_index == -1);
  assert(node->last_applied == -1);
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
  append_entries_args_t args = make_append_entries_args(5, 2, 0, -1, NULL, 0, 0);

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

static void test_handle_append_entries_rejects_stale_leader_term(void) {
  raft_node_t *node = init_raft_node(1, 3);
  int terms[] = {1, 1};
  append_entries_args_t args;

  seed_log_terms(node, terms, 2);
  node->current_term = 5;
  node->commit_index = 1;

  args = make_append_entries_args(4, 2, 1, 1, NULL, 0, 1);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == false);
  assert(reply.follower_term == 5);
  assert(node->role == FOLLOWER);
  assert(node->current_term == 5);
  assert(node->log_length == 2);
  assert(node->log[0].term == 1);
  assert(node->log[1].term == 1);
  assert(node->commit_index == 1);

  destroy_node(node);
}

static void test_handle_append_entries_steps_down_on_higher_term_and_clears_vote(void) {
  raft_node_t *node = init_raft_node(1, 3);
  append_entries_args_t args = make_append_entries_args(4, 2, 0, -1, NULL, 0, 0);

  node->current_term = 3;
  node->voted_for = node->id;
  become_leader(node);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == true);
  assert(reply.follower_term == 4);
  assert(node->role == FOLLOWER);
  assert(node->current_term == 4);
  assert(node->voted_for == -1);
  assert(node->next_index == NULL);
  assert(node->match_index == NULL);

  destroy_node(node);
}

static void test_handle_append_entries_demotes_candidate_on_equal_term(void) {
  raft_node_t *node = init_raft_node(1, 3);
  append_entries_args_t args = make_append_entries_args(6, 2, 0, -1, NULL, 0, 0);

  node->current_term = 6;
  node->role = CANDIDATE;
  node->voted_for = node->id;

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == true);
  assert(reply.follower_term == 6);
  assert(node->role == FOLLOWER);
  assert(node->current_term == 6);
  assert(node->voted_for == node->id);

  destroy_node(node);
}

static void test_handle_append_entries_rejects_invalid_negative_prev_log_index(void) {
  raft_node_t *node = init_raft_node(1, 3);
  int terms[] = {1};
  append_entries_args_t args;

  seed_log_terms(node, terms, 1);
  node->current_term = 2;

  args = make_append_entries_args(2, 2, 1, -2, NULL, 0, 0);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == false);
  assert(reply.follower_term == 2);
  assert(node->log_length == 1);
  assert(node->log[0].term == 1);

  destroy_node(node);
}

static void test_handle_append_entries_rejects_out_of_bounds_prev_log_index(void) {
  raft_node_t *node = init_raft_node(1, 3);
  int terms[] = {1, 2};
  append_entries_args_t args;

  seed_log_terms(node, terms, 2);
  node->current_term = 3;

  args = make_append_entries_args(3, 2, 2, 2, NULL, 0, 0);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == false);
  assert(reply.follower_term == 3);
  assert(node->log_length == 2);
  assert(node->log[0].term == 1);
  assert(node->log[1].term == 2);

  destroy_node(node);
}

static void test_handle_append_entries_rejects_prev_log_term_mismatch(void) {
  raft_node_t *node = init_raft_node(1, 3);
  int terms[] = {1, 2};
  append_entries_args_t args;

  seed_log_terms(node, terms, 2);
  node->current_term = 3;

  args = make_append_entries_args(3, 2, 9, 1, NULL, 0, 0);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == false);
  assert(reply.follower_term == 3);
  assert(node->log_length == 2);
  assert(node->log[0].term == 1);
  assert(node->log[1].term == 2);

  destroy_node(node);
}

static void test_handle_append_entries_accepts_heartbeat_with_matching_prev_entry(void) {
  raft_node_t *node = init_raft_node(1, 3);
  int terms[] = {4, 4};
  append_entries_args_t args;

  seed_log_terms(node, terms, 2);
  node->current_term = 4;
  node->commit_index = 0;

  args = make_append_entries_args(4, 2, 4, 1, NULL, 0, 1);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == true);
  assert(reply.follower_term == 4);
  assert(node->log_length == 2);
  assert(node->log[0].term == 4);
  assert(node->log[1].term == 4);
  assert(node->commit_index == 1);

  destroy_node(node);
}

static void test_handle_append_entries_appends_entries_from_start_when_prev_is_minus_one(void) {
  raft_node_t *node = init_raft_node(1, 3);
  log_entry_t new_entries[2];
  append_entries_args_t args;

  new_entries[0] = make_entry(1);
  new_entries[1] = make_entry(1);

  node->current_term = 1;
  node->commit_index = 0;

  args = make_append_entries_args(1, 2, 0, -1, new_entries, 2, 1);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == true);
  assert(reply.follower_term == 1);
  assert(node->log_length == 2);
  assert(node->log[0].term == 1);
  assert(node->log[1].term == 1);
  assert(node->commit_index == 1);

  destroy_node(node);
}

static void test_handle_append_entries_truncates_conflicting_suffix_and_appends(void) {
  raft_node_t *node = init_raft_node(1, 3);
  int terms[] = {1, 2, 2, 2};
  log_entry_t new_entries[2];
  append_entries_args_t args;

  seed_log_terms(node, terms, 4);
  node->current_term = 3;
  node->commit_index = 1;
  new_entries[0] = make_entry(3);
  new_entries[1] = make_entry(3);

  args = make_append_entries_args(3, 2, 2, 1, new_entries, 2, 3);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == true);
  assert(reply.follower_term == 3);
  assert(node->log_length == 4);
  assert(node->log[0].term == 1);
  assert(node->log[1].term == 2);
  assert(node->log[2].term == 3);
  assert(node->log[3].term == 3);
  assert(node->commit_index == 3);

  destroy_node(node);
}

static void test_handle_append_entries_skips_matching_term_entry_before_appending(void) {
  raft_node_t *node = init_raft_node(1, 3);
  int terms[] = {1, 2, 3};
  log_entry_t new_entries[2];
  append_entries_args_t args;

  seed_log_terms(node, terms, 3);
  node->current_term = 4;
  node->commit_index = 0;
  new_entries[0] = make_entry(2);
  new_entries[1] = make_entry(4);

  args = make_append_entries_args(4, 2, 1, 0, new_entries, 2, 2);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == true);
  assert(reply.follower_term == 4);
  assert(node->log_length == 3);
  assert(node->log[0].term == 1);
  assert(node->log[1].term == 2);
  assert(node->log[2].term == 4);
  assert(node->commit_index == 2);

  destroy_node(node);
}

static void test_handle_append_entries_grows_log_storage_when_capacity_reached(void) {
  raft_node_t *node = init_raft_node(1, 3);
  int terms[INITIAL_LOG_CAPACITY - 1];
  log_entry_t new_entries[2];
  append_entries_args_t args;

  for (int i = 0; i < INITIAL_LOG_CAPACITY - 1; i++) {
    terms[i] = 1;
  }

  seed_log_terms(node, terms, INITIAL_LOG_CAPACITY - 1);
  node->current_term = 2;
  new_entries[0] = make_entry(2);
  new_entries[1] = make_entry(2);

  args = make_append_entries_args(2, 2, 1, INITIAL_LOG_CAPACITY - 2, new_entries, 2, 0);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == true);
  assert(node->log_capacity == INITIAL_LOG_CAPACITY * 2);
  assert(node->log_length == INITIAL_LOG_CAPACITY + 1);
  assert(node->log[INITIAL_LOG_CAPACITY - 1].term == 2);
  assert(node->log[INITIAL_LOG_CAPACITY].term == 2);

  destroy_node(node);
}

static void test_handle_append_entries_commit_index_does_not_decrease_for_lower_leader_commit(void) {
  raft_node_t *node = init_raft_node(1, 3);
  int terms[] = {5, 5, 5};
  append_entries_args_t args;

  seed_log_terms(node, terms, 3);
  node->current_term = 5;
  node->commit_index = 2;

  args = make_append_entries_args(5, 2, 5, 2, NULL, 0, 1);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == true);
  assert(node->commit_index == 2);

  destroy_node(node);
}

static void test_handle_append_entries_commit_index_advances_but_is_capped_by_last_log_index(void) {
  raft_node_t *node = init_raft_node(1, 3);
  int terms[] = {6, 6};
  append_entries_args_t args;

  seed_log_terms(node, terms, 2);
  node->current_term = 6;
  node->commit_index = 0;

  args = make_append_entries_args(6, 2, 6, 1, NULL, 0, 10);

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == true);
  assert(node->commit_index == 1);

  destroy_node(node);
}

static void test_handle_append_entries_does_not_reduce_commit_index_on_empty_log(void) {
  raft_node_t *node = init_raft_node(1, 3);
  append_entries_args_t args = make_append_entries_args(7, 2, 0, -1, NULL, 0, 3);

  node->current_term = 7;
  node->commit_index = -1;

  append_entries_reply_t reply = handle_append_entries(node, args);

  assert(reply.append_succeed == true);
  assert(node->commit_index == -1);

  destroy_node(node);
}

static void test_become_leader_is_idempotent_without_reallocating_leader_state(void) {
  raft_node_t *node = init_raft_node(1, 3);

  node->log_length = 2;
  become_leader(node);

  int *first_next_index = node->next_index;
  int *first_match_index = node->match_index;

  become_leader(node);

  assert(node->role == LEADER);
  assert(node->next_index == first_next_index);
  assert(node->match_index == first_match_index);

  destroy_node(node);
}

static void test_restart_without_persistence_drops_term_vote_and_log(void) {
  raft_node_t *node = init_raft_node(9, 3);

  node->current_term = 11;
  node->voted_for = 2;
  node->log[0] = make_entry(11);
  node->log_length = 1;

  destroy_node(node);

  raft_node_t *after_restart = init_raft_node(9, 3);

  assert(after_restart->current_term == 0);
  assert(after_restart->voted_for == -1);
  assert(after_restart->log_length == 0);

  destroy_node(after_restart);
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

  test_handle_append_entries_rejects_stale_leader_term();
  test_handle_append_entries_steps_down_on_higher_term_and_clears_vote();
  test_handle_append_entries_demotes_candidate_on_equal_term();
  test_handle_append_entries_demotes_leader_and_clears_leader_state_on_equal_term();
  test_handle_append_entries_rejects_invalid_negative_prev_log_index();
  test_handle_append_entries_rejects_out_of_bounds_prev_log_index();
  test_handle_append_entries_rejects_prev_log_term_mismatch();
  test_handle_append_entries_accepts_heartbeat_with_matching_prev_entry();
  test_handle_append_entries_appends_entries_from_start_when_prev_is_minus_one();
  test_handle_append_entries_truncates_conflicting_suffix_and_appends();
  test_handle_append_entries_skips_matching_term_entry_before_appending();
  test_handle_append_entries_grows_log_storage_when_capacity_reached();
  test_handle_append_entries_commit_index_does_not_decrease_for_lower_leader_commit();
  test_handle_append_entries_commit_index_advances_but_is_capped_by_last_log_index();

  test_restart_without_persistence_drops_term_vote_and_log();

  // Strict-invariant tests. These are expected to fail until the implementation catches up.
  test_handle_append_entries_does_not_reduce_commit_index_on_empty_log();
  test_become_leader_is_idempotent_without_reallocating_leader_state();

  puts("all tests passed");
  return 0;
}
