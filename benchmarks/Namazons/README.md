# N Amazons

See https://de.wikipedia.org/wiki/Damenproblem#Andere_Figuren

The file `Namazons-generator.hs` generates mvl clauses for N amazons.
(It needs [Stack](https://www.haskellstack.org/) to run.)

Examples:

    ./Namazons-generator.hs 4 > 4amazons.txt
    ../../Solver -solvenc -file 4amazons.txt
    ...
    UNSAT
    ...
    ./Namazons-generator.hs 10 > 10amazons.txt
    ../../Solver -solvenc -file 10amazons.txt
    ...
    SAT
    ...
    The model:
    3=0
    6=1
    9=2
    1=3
    4=4
    2=7
    7=5
    8=9
    5=8
    10=6
    Verifying model ... model is CORRECT

|   | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 |   |   |   | X |   |   |   |   |   |   |
| 2 |   |   |   |   |   |   |   | X |   |   |
| 3 | X |   |   |   |   |   |   |   |   |   |
| 4 |   |   |   |   | X |   |   |   |   |   |
| 5 |   |   |   |   |   |   |   |   | X |   |
| 6 |   | X |   |   |   |   |   |   |   |   |
| 7 |   |   |   |   |   | X |   |   |   |   |
| 8 |   |   |   |   |   |   |   |   |   | X |
| 9 |   |   | X |   |   |   |   |   |   |   |
|10 |   |   |   |   |   |   | X |   |   |   |