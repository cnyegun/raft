# What is Raft?

- Is a consensus algorithm, made to be understood. 
- There are three subproblem need to be solved:
    1. Leader Election: what happens when a leader dies, voting systems
    2. Log Replication: other servers receives operations from the leaders
    3. Safety: make sure that the system doesn't lost any entries even if any server dies.

# Core state machine

- Each server is one of three states: Follower, Candidate, Leader.

Rule:
- If a follower stop receiving ping from a leader, it becomes a candidate and ask for vote.
- If a candidate gets majority vote, it becomes a leader and propagate `term` to all the other servers.
- If a leader discover a new leader with higher term, it steps back to follower

