#include "raft.h"
#include <stdlib.h>
#include <string.h>

RaftNode *init_raft_node(int id, int numPeers) {
    RaftNode *R = calloc(1, sizeof(RaftNode));
    R->id = id;
    R->numPeers = numPeers;
    R->votedFor = -1;
    R->log = calloc(INITIAL_LOG_CAPACITY, sizeof(LogEntry));
    R->logCapacity = INITIAL_LOG_CAPACITY;
    return R;
}