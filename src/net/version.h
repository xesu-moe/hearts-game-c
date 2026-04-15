#ifndef HH_VERSION_H
#define HH_VERSION_H

/* Build version string shared by client, game server, and lobby.
 * Bumped manually per release. Exact-match enforced at lobby handshake
 * (see NET_MSG_LOBBY_HELLO in protocol.h). */
#define HH_VERSION "0.1.2"

#endif /* HH_VERSION_H */
