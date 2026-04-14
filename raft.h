#ifndef RAFT_H
#define RAFT_H

#define INITIAL_LOG_CAPACITY 8

typedef enum { FOLLOWER, CANDIDATE, LEADER } Role;

typedef struct {
  int term;
  char command[256];
} LogEntry;

typedef struct {
  // Voting
  int currentTerm;
  int votedFor;

  // Log
  LogEntry *log;
  int logLength;
  int logCapacity;
  
  // Volatile 
  int commitIndex;
  int lastApplied;

  // Leader-only state
  int *nextIndex;
  int *matchIndex;

  Role role;
  int id;
  int numPeers;
} RaftNode;

RaftNode *init_raft_node(int id, int numPeers);
void become_leader(RaftNode *R);
void step_down(RaftNode *R, int newTerm);

#endif