#define DO_MOTD	1

/* Raise from the upstream default of 5 to support parallel tooling
 * (simultaneous scp + rapid-fire probes from a single ops workstation).
 * Still bounded well below MAX_UNAUTH_CLIENTS (30) so one client cannot
 * exhaust the global unauth session pool. */
#define MAX_UNAUTH_PER_IP 20
