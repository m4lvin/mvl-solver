## Finite Domain Satisfiablity Solver

*Note*: This solver corrects and improves the solver created by Hemal A. Lal. See: http://www.d.umn.edu/~lalx0004/research/.

Currently the solver is unsound as shown by the following example (described in *Jain_report.pdf*, p.28). When you run the solver on the *counterexample.txt*, you get the following result:

```
**** Finite Domain Sat Solver - Hemal A Lal ****
0.0001	 0.0003	SAT

Number of Decisions   : 4
Number of Units       : 0
Number of Backtracks  : 0
Number of Entails     : 0
Number of Levels      : 4
Number of Variables   : 4
Number of Clauses     : 18

Verifying model ... unsat clause :0
model is INCORRECT
```

Here follows the description of how to execute the program. 

### Generating Benchmark Problem 

If you want to generate a random benchmark problem, use the following format to run the program: 

``` exe -genben -var <int> -clause <int> -clausesize <int> -sat <1/0>
      -domain <int> -drand <1/0> -file <string> ```

where :

```
exe               | * name of executable

  -genben         | * option stating create benchmark problem
  -var            | * number of variables in benchmark problem
  -clause         | * number of clauses in benchmark problem
  -clausesize     |   number of atoms in each clause, [DEFAULT : 3]
  -sat            |   states whether this benchmark problem is satisfiable or not
                      [possible value : 1/0] [default : 1/true]
  -domain         |   states the domain size of each variables, [DEFAULT : 10]
  -drand          |   states whether to assign domain size of each variable randomly
                      [possible value : 1/0] [DEFAULT : 0/false]
  -bool           |   states whether the file is in boolean cnf (0) or modified cnf (1)
  -file           | * name of the output file

 * - required fields
```

### Finite Domain Solver with Chronological Backtracking

Use the following format to run the program. The solver accepts problems in extended DIMACS format. See *report.pdf*, p. 30 for the syntax description. 

``` exe -solvech -var <int> -clause <int> -file <string> -time <int> ```

where :

``` 
  exe             : * name of executable
  -solvech        : * option stating to solve the finite domain problem
  -var            : * number of variables in benchmark problem
  -clause         : * number of clauses in benchmark problem
  -file           : * name of the input file
  -time           : amount of time allowed for solver to run (in seconds)

 * - required fields
``` 
EXAMPLE: ``` ./Solver -solvech -var 4 -clause 18 -file "counterexample.txt" ```


### Finite Domain Solver with NonChronological Backtracking

Use the following format to run the program: 

``` exe -solvenc -var <int> -clause <int> -file <string> -time <int> ```

where :
``` 
  exe             : * name of executable
  -solvenc        : * option stating to solve the finite domain problem
  -var            : * number of variables in benchmark problem
  -clause         : * number of clauses in benchmark problem
  -file           : * name of the output file
  -time           : amount of time allowed for solver to run

 * - required fields
```


### Convert Boolean to Finite Domain 

Use the following format to run the program. The solver accepts problems in DIMACS format.

``` exe -b2f -file <string> -model <string> ```
where :

``` 
 exe             : * name of executable
 -b2f            : * option stating to convert file
 -file           : * name of the boolean file
 -model          : * name of the finite file

 * - required fields
``` 


### Convert Finite Domain to Boolean : Linear Encoding

Use the following format to run the program:

``` exe -linenc -file <string> -model <string> ```

where :
```
 exe             : * name of executable
 -linenc         : * option stating to convert file
 -file           : * name of the boolean file
 -model          : * name of the finite file

 * - required fields
```


### Convert Finite Domain to Boolean : Quadratic Encoding 

Use the following format to run the program:

``` exe -quadenc -file <string> -model <string> ```

where :
```
 exe             : * name of executable
 -quadenc        : * option stating to convert file
 -file           : * name of the boolean file
 -model          : * name of the finite file

 * - required fields
```