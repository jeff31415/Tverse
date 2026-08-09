# major boundaries:
- Ngnix considered trusted. Run on same macine on loopback, or do debugging in internal netowrk.
- Currently allow empty/barer/bypass token, until impl of auth handler
- Short interruption of connection left for game room to handle. For longer time(>5m), Gateway kick out clinet to prevent leakage.
- No target of any speific game in SDK

# 