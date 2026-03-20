
## Por implementar

Phase 0: Prerequisite Refactoring (hardest, highest risk)
                                                                                                                                      
  Extract game logic from the monolithic main.c into a callable API driven by serializable GameAction commands. Without this,
  networking can't be cleanly layered on. This is the Command Pattern that CLAUDE.md already calls for.                               
                                                            
  Phase 1: Network Transport Layer                                                                                                    
                                                            
  Low-level TCP client/server, non-blocking sockets with poll(), length-prefixed binary message framing, connection lifecycle. Pure   
  infrastructure, no game semantics. No Raylib networking — raw POSIX sockets.
                                                                                                                                      
  Phase 2: Authentication & Player Identity                                                                                           
  
  SQLite for accounts, password hashing, login/register flow, session tokens. Needed before rooms or matchmaking because ranked play  
  requires persistent identity.                             
                                                                                                                                      
  Phase 3: Room System (Friend Rooms)                       

  Create/join rooms via 6-char codes, lobby UI, seat assignment, AI backfill for empty seats. First user-visible networking feature.  
  
  Phase 4: Online Game Sessions (most complex)                                                                                        
                                                            
  Server-authoritative game execution — state sync, turn management, hidden information filtering (never send other players' hands),  
  pass phase, Phase 2 mods over network. The server is event-driven (not 60fps), and only processes actions when messages arrive.
                                                                                                                                      
  Phase 5: Reconnection Handling                            

  Disconnect detection, AI takeover for absent players, 2-minute seat reservation, full state resync on reconnect.                    
  
  Phase 6: Matchmaking & Ladder                                                                                                       
                                                            
  ELO rating (multi-player variant), matchmaking queue with widening search window, leaderboard queries, leave penalties.             
  
  ---                                                                                                                                 
  Dependency chain:                                         
                                                                                                                                      
  Phase 0 → 1 → 2 → 3 → 4 → 5
                   └──→ 6 (also requires 4)                                                                                           
                                                                                                                                      
  Key architectural decisions:                                                                                                        
                                                                                                                                      
  - Single-threaded server using poll() — a Hearts game generates ~1 msg/sec, hundreds of games in one process trivially              
  - TCP (not UDP/WebSocket) — turn-based, order-critical, low bandwidth
  - Server is a separate binary sharing game logic code but not rendering                                                             
  - Hidden information filtering is the critical security boundary — the server must never send cards a client shouldn't see          
  - Estimated ~4000–6000 lines of new/modified code total                                                                             
                                                                                                                                      
  Phase 0 (refactoring) and Phase 4 (game sessions) are the heaviest. Phase 6 (matchmaking) is relatively self-contained.  





Time Counter
Transmutation
Contract Draft System
Game over
In game menu.

Check:
  Point-scoring transmutation cards are included in scoring menu 




  



Cartas especiales:

  Gana cualquier trick
  Pierde cualquier trick
  Duplica los puntos obtenidos al final de la ronda.
  Se convierte en una reina de picas




statistics
collection
animaciones


networking (docker container per game room)
sprites
fuente de letra


## Contratos

### Easy

1. Take at least 4 clubs
2. Take at least 4 diamonds
3. Take at least 4 spades
4. Take at least 4 hearts

5. Win 3 consecutive tricks
6. Hit at least 3 times with hearts

7. End the round scoring less points than the others.

8. Don't start any trick with clubs
9. Don't start any trick with diamonds
10. Don't start any trick with spades
11. Don't start any trick with hearts

12. Win the first trick.

13. Break hearts.
14. Capture the queen of spades.


### Medium

1. Win the first 3 tricks
2. Avoid winning any of the last 3 tricks
3. End the round without scoring any points

4. Win a trick with a card received during passing phase.
5. Hit with a card you received during passing phase.

6. Win the trick number 7
7. Win the first and the last trick

8. Lead the trick where the queen of spades is played.
9. Hit with a queen of spades you received during passing phase.

### Hard

1. Hit the moon.
2. Take the last card a player is missing to hit the moon.
3. Play queen of spades as the first spade card in the round.
4. Hit with a scoring transmutation card.