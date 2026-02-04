# Cluster Support

The current implementation supports manual placement only. Use `placement.target = "peer:<addr:port>"` in the session spec to forward execution to a peer coordd instance. Automatic scheduling is not implemented in this initial version.
