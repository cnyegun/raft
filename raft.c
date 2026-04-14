#include "raft.h"
#include <stdlib.h>
#include <string.h>

raft_node_t *init_raft_node(int id, int num_peers) {
    raft_node_t *node = calloc(1, sizeof(raft_node_t));
    node->id = id;
    node->num_peers = num_peers;
    node->role = FOLLOWER;
    node->voted_for = -1;
    node->log = calloc(INITIAL_LOG_CAPACITY, sizeof(log_entry_t));
    node->log_capacity = INITIAL_LOG_CAPACITY;
    return node;
}

void become_leader(raft_node_t *node) {
    node->role = LEADER; 
    int num_peers = node->num_peers;
    int log_length = node->log_length;
    node->next_index = malloc(num_peers * sizeof(int));
    for (int i = 0; i < num_peers; i++) {
        node->next_index[i] = log_length;
    }
    node->match_index = calloc(num_peers, sizeof(int));
}

void step_down(raft_node_t *node, int new_term) {
    node->current_term = new_term;
    node->voted_for = -1;
    if (node->role == LEADER) {
        free(node->next_index);
        free(node->match_index);
        node->next_index = NULL;
        node->match_index = NULL;
    }
    node->role = FOLLOWER;
}
