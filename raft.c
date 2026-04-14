#include "raft.h"
#include <stdlib.h>
#include <string.h>

RaftNode *init_raft_node(int id, int numPeers) {
    RaftNode *R = calloc(1, sizeof(RaftNode));
    R->id = id;
    R->numPeers = numPeers;
    R->role = FOLLOWER;
    R->votedFor = -1;
    R->log = calloc(INITIAL_LOG_CAPACITY, sizeof(LogEntry));
    R->logCapacity = INITIAL_LOG_CAPACITY;
    return R;
}

void become_leader(RaftNode *R) {
    R->role = LEADER; 
    int numPeers = R->numPeers;
    int logLength = R->logLength;
    R->nextIndex = malloc(numPeers * sizeof(int));
    for (int i = 0; i < numPeers; i++) {
        R->nextIndex[i] = logLength;
    }
    R->matchIndex = calloc(numPeers, sizeof(int));
}

void step_down(RaftNode *R, int newTerm) {
    R->currentTerm = newTerm;
    R->votedFor = -1;
    if (R->role == LEADER) {
        free(R->nextIndex);
        free(R->matchIndex);
        R->nextIndex = NULL;
        R->matchIndex = NULL;
    }
    R->role = FOLLOWER;
}