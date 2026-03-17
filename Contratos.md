
## Por implementar

Mejorar layout
	When choosing a contract, don't show any other text. Right now, passing cards text is showing and overlapping. Let's divide the passing phase in three subphases: 1. Host choose host action, 2. players pick contract, 3. cards passing.
	One phase after the other. Not simultaneous. When host is choosing the host actions, for other players show "Host is choosing the round modifier..." and this phase should have a time limit. If the host doesn't choose anything in 3 or 5 seconds the host action is skipped. Choosing a contract phase also have a time limit of 8 seconds. Passing phase also have a time limit of 8 seconds. Time limits should be coded in a way that it is easy to modify later or include it in game creation configuration (when we implement that in the future).

	When choosing contracts, host actions or revenge actions. Buttons should also include a description of the element in a smaller font size. For example, Heartless should display just below in smaller font: Don't collect any hearts this round.

	FPS counter is overlaping with the text "log" in the corner. Remove the "log" text.

	Card positioning for opponents is wrong. North looks like they are rotated in the wrong direction. Fix it. Then you could just pick north's card shape and rotate the full set 90º clockwise and counterclockwise to recreate east and west card positions.

bugs:
	Efectos no funcionan
	When choosing a host action it displays "choose a contract:" instead "Choose a host action:"
musica

collection

settings
	antialiasing?
statistics

animaciones

networking (docker container per game room)

sprites

reorganización de archivos.

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
5. Hit with a card you received during passing phase./btw 

6. Win the trick number 7
7. Win the first and the last trick

8. Lead the trick where the queen is played.

### Hard

1. Hit the moon.
2. Take the last card a player is missing to hit the moon.
3. Play queen of spades as the first spade card in the round.