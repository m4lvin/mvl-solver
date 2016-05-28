//**********************************************************************//
//
// Finite Domain Solver
// File : Formula.cc
//
//**********************************************************************//
//Including Libraries and Header files
#include "Formula.h"
#include "Clause.h"
#include <cstring>
#include <stdexcept>
#include <climits>
using namespace std;
//**********************************************************************//
//Default 0-arg constructor
Formula::Formula()
{
  //Default values - 10 atoms per clause, 10 variables per problem
  VARLIST.reserve(10);
  CLAUSELIST.reserve(10);
  TIMELIMIT = 3600; //1hour = 60mins * 60secs
  TIME_S = 0;
  TIME_E = 0;
  LEVEL = 0;
  UNITS = 0;
  BACKTRACKS = 0;
  DECISIONS = 0;
  ENTAILS = 0;
  UNITCLAUSE = -1;
  CONFLICT = false;
  CONFLICTINGCLAUSE = -1;
  DECSTACK.reserve(10);
  RESTARTS = 0;
  LOG = false;
}

//1-arg constructor
Formula::Formula(CommandLine * cline)
{
  //Setting appropriate values and resizing vectors
  //  VARLIST.reserve(cline->NUM_VAR+1);
  //  CLAUSELIST.reserve((int)(cline->CLAUSE_SIZE*1.2));
  TIMELIMIT = cline->TIME;
  TIME_S = 0;
  TIME_E = 0;
  LEVEL = 0;
  UNITS = 0;
  BACKTRACKS = 0;
  DECISIONS = 0;
  ENTAILS = 0;
  UNITCLAUSE = -1;
  CONFLICT = false;
  CONFLICTINGCLAUSE = -1;
  //  DECSTACK.reserve(cline->NUM_VAR+1);
  RESTARTS = 0;
  LOG = cline->LOG;
  // WATCH = cline->WATCH;
}

//BuildFunction
void Formula::BuildFunction(CommandLine * cline)
{
  //local variables
  ifstream infile;
  char line_buffer[3000];
  char word_buffer[3000];
  int clause_num = 0;
  int atom_num = 0;
  int var, val;
  char ch;
  char ch1;
  string str;
  Variable * temp_var = NULL;
  Literal * temp_atom = NULL;
  Clause * temp_clause = NULL;

  //to start variable from 1 and not from 0 we need this
  VARLIST.push_back( new Variable(0, 0));
  VARLIST[0]->SAT = true;

  TIME_S = GetTime();
  //opening infile to read and checking if it opens
  infile.open(cline->FILE, ios::in);
  if(!infile)
  {
    cout<<endl;
    cout<<"**** ERROR ****"<<endl;
    cout<<"Could not open input file : "<<endl;
    cout<<endl;
    exit(1);
  }

  //now reading each line from the infile and if needed to convert
  //then converting it and writing to outfile or just writing directly
  while(infile.getline(line_buffer, 3000))
  {
    temp_clause = new Clause(cline->CLAUSE_SIZE);
    //if line is comment - just write
    if(line_buffer[0] == 'c')
    {
      //do nothing
      ;
    }
    //else if line is p cnf var_num clause_num - just write
    else if(line_buffer[0] == 'p')
    {
      sscanf(line_buffer, "p cnf %d %d", &var, &val);

    }
    //else if line is d var# domsize
    else if(line_buffer[0] == 'd')
    {
      sscanf(line_buffer, "d %d %d", &var, &val);
      temp_var = new Variable(var, val);
      VARLIST.push_back(temp_var);
    }
    //else its clause lines
    else
    {
      char * lp = line_buffer;
      do
      {
        char * wp = word_buffer;
        //erasing all leading space/tabs
        while (*lp && ((*lp == ' ') || (*lp == '\t')))
        {
          lp++;
        }
        //reading each value into word buffer
        while (*lp && (*lp != ' ') && (*lp != '\t') && (*lp != '\n'))
        {
          *(wp++) = *(lp++);
        }
        *wp = '\0';
        //converting into int and writing to file
        if(strlen(word_buffer) != 0)
        {
          sscanf(word_buffer, "%d %c %d", &var, &ch, &val);
          if(ch == '!')
          sscanf(word_buffer, "%d %c %c %d", &var, &ch, &ch1, &val);
          //checking if variable = 0 i.e end of line
          if(var != 0)
          {
            //add it to clause_list's atom_list
            temp_atom = new Literal(var, ch, val);
            temp_clause->AddAtom(temp_atom);

            //increment the index representing value so that
            //we can keep track of number of occurences of each
            //variable and its domain value, if ch is = then increment
            //the value count, else increment others count
            if(ch == '=')
            {
              VARLIST[var]->ATOMCNTPOS[val]++;
              VARLIST[var]->AddRecord(clause_num, val, true);
            }
            else
            {
              VARLIST[var]->ATOMCNTNEG[val]++;
              VARLIST[var]->AddRecord(clause_num, val, false);
            }
            atom_num++;
          }
        }
      }while(*lp);
      CLAUSELIST.push_back(temp_clause);

      // with watched literals option, assign first two literals to watched1 and watched2
      if (cline -> WATCH) {
        temp_clause -> WATCHED[0] = temp_clause -> ATOM_LIST[0];
        if (temp_clause -> NumAtom > 1) {
          temp_clause -> WATCHED[1] = temp_clause -> ATOM_LIST[1];
        } else temp_clause -> WATCHED[1] = NULL; // if only one literal, watched2 is null
      }
      atom_num = 0;
      ++clause_num;
    }
  }

  //closing file
  infile.close();
  TIME_E = GetTime();
}

//PrintVar
void Formula::PrintVar()
{
  //local variable
  int size = VARLIST.size();
  for(int x=1; x<size; x++)
  {
    cout<<VARLIST[x]->VAR<<"="<<VARLIST[x]->VAL;
    cout<<" Domain = "<<VARLIST[x]->DOMAINSIZE<<endl;
    VARLIST[x]->Print();
    cout<<endl;
  }
}

void Formula::PrintModel()
{
  for(unsigned int  i=0; i<DECSTACK.size();i++)
  {
    if(DECSTACK[i]->EQUAL) DECSTACK[i]->Print();
  }
  if (LOG) cout<<endl;
}

//PrintClause
void Formula::PrintClauses()
{
  //local variable
  int size = CLAUSELIST.size();
  for(int x=0; x<size; x++)
  {
    CLAUSELIST[x]->Print();
    cout<<endl;
  }
}


//PrintInfo
void Formula::PrintInfo()
{
  printf("\n");
  printf("Decisions   : %d\n", DECISIONS);
  //  printf("Number of Units       : %d\n", UNITS);
  printf("Backtracks  : %d\n", BACKTRACKS);
  printf("Entails     : %d\n", ENTAILS);
  //  printf("Number of Levels      : %d\n", LEVEL);
  printf( "Variables   : %zu\n", VARLIST.size()-1 );
  printf( "Clauses     : %zu\n", CLAUSELIST.size() );
  printf("Restarts   : %d\n", RESTARTS);

  printf("\n");
}

bool Formula::hasClause(Clause * clause){
  for(unsigned int  i=0; i < CLAUSELIST.size(); i++){
    if(clause->ClauseisEqual(CLAUSELIST[i],clause)) return true;
  }
  return false;
}


//verifyModel
bool Formula::verifyModel()
{
  //set all clause to false
  for(unsigned int  i=0; i<CLAUSELIST.size(); i++)
  CLAUSELIST[i]->SAT = false;
  //for each variable and its value set all clauses in
  //which it occurs to true
  int val = 0;
  int domainsize = 0;
  VARRECORD * curr = NULL;
  for(unsigned int  i=1; i<VARLIST.size(); i++)
  {
    val = VARLIST[i]->VAL;
    if(val != -1)
    {
      domainsize = VARLIST[i]->DOMAINSIZE;
      for(int j=0; j<domainsize; j++)
      {
        if(j != val)
        {
          curr = VARLIST[i]->ATOMRECNEG[j];
          while(curr)
          {
            CLAUSELIST[curr->c_num]->SAT = true;
            curr = curr->next;
          }
        }
        else
        {
          curr = VARLIST[i]->ATOMRECPOS[j];
          while(curr)
          {
            CLAUSELIST[curr->c_num]->SAT = true;
            curr = curr->next;
          }
        }
      }
    }
    else
    {
      domainsize = VARLIST[i]->DOMAINSIZE;
      for(int j=0; j<domainsize; j++)
      {
        if(VARLIST[i]->ATOMASSIGN[j] == -1)
        {
          curr = VARLIST[i]->ATOMRECNEG[j];
          while(curr)
          {
            CLAUSELIST[curr->c_num]->SAT = true;
            curr = curr->next;
          }
        }
        else
        VARLIST[i]->VAL = j;
      }
    }
  }
  return (checkSat());
}

//checkSat
bool Formula::checkSat()
{  //if (LOG) cout<<"Checking satisfiability..."<<endl;
int size = CLAUSELIST.size();
for(int i=0; i<size; i++)
{
  //if clause is not SAT return false
  if(!CLAUSELIST[i]->SAT) {
    //  if (LOG) cout<<"Unsatisfied clause # "<<i<<endl;
    // CLAUSELIST[i]->Print();
    return false; }
  }
  return true;
}

//checkConflict: returns id of a conflicting clause
int Formula::checkConflict()
{
  int size = CLAUSELIST.size();
  for(int i=0; i<size; i++)
  {
    //if clause is not SAT and has 0 unassigned literal
    //return index i
    if((CLAUSELIST[i]->NumUnAss == 0) &&
    (!CLAUSELIST[i]->SAT)){
      CONFLICT = true;
      CONFLICTINGCLAUSE = i;
      return CONFLICTINGCLAUSE; }
    }
    return -1;
  }

  //checkUnit
  void Formula::checkUnit()
  { //if (LOG) cout<<"Checking if there is a unit clause..."<<endl;
  int size = CLAUSELIST.size();
  UNITLIST.clear();
  for(int i=0; i<size; i++)
  {//if clause is not SAT and has 1 unassigned literal
    //add the index i into Unitlist
    if( (CLAUSELIST[i]->NumUnAss == 1 &&
      !CLAUSELIST[i]->SAT ) ){
        // if (LOG) cout<<"Found a unit clause: "<<i<<endl;
        UNITLIST.push_back(i);
      } else if ( CLAUSELIST[i]->NumUnAss == 0 && !CLAUSELIST[i]->SAT) CONFLICT = true;
    }
  }


  //checkEntail
  bool Formula::checkEntail(int var)
  {  // if (LOG) cout<<"Checking entailment..."<<endl;

  int domainsize = 0;
  bool flag = false;
  int domainvalue = -1;

  //if variable not satisfied
  if(!VARLIST[var]->SAT)
  {// if (LOG) cout<<var<<" is not assigned"<<endl;
  domainsize = VARLIST[var]->DOMAINSIZE;
  //check its domain and see that exactly one
  //domain value has atomassign 0, if more than one
  //then there is no entailment
  for(int j=0; j<domainsize; j++)
  {
    if(VARLIST[var]->ATOMASSIGN[j] == 0)
    {
      if(!flag)
      {
        domainvalue = j;
        flag = true;
      }
      else
      {
        flag = false;
        break;
      }
    }
  }
}
//if found an entail literal then assign memory to variable
//and return true
if(flag)
{
  ENTAILLITERAL = new Literal(var, domainvalue);
  //  if (LOG) cout<<"Entailment... "<<ENTAILLITERAL->VAR<<"="<<ENTAILLITERAL->VAL<<endl;

  return true;
}
return false;
}

//chooseLiteral
Literal * Formula::chooseLiteral()
{
  int max = INT_MIN;
  int size = VARLIST.size();
  int domainsize = -1;
  int tvar = -1;
  int tval = -1;
  int tmax = -1;
  UNITCLAUSE = -1;
  for(int i=0; i<size; i++)
  {
    if(!VARLIST[i]->SAT)
    {
      domainsize = VARLIST[i]->DOMAINSIZE;
      for(int j=0; j<domainsize; j++)
      {
        if(VARLIST[i]->ATOMASSIGN[j] == 0)
        {
          tmax = VARLIST[i]->ATOMCNTPOS[j] - VARLIST[i]->ATOMCNTNEG[j];
          if(max < tmax)
          {
            max = tmax;
            tvar = i;
            tval = j;
          }
        }
      }
    }
  }
  return (tvar != -1 ? new Literal(tvar, tval) : NULL);
}

//reduceTheory
void Formula::reduceTheory(int var, bool equals, int val)
{

  if(equals)
  {
    //if (LOG) cout<<"Reducing literal: "<<var<<"="<<val<<" at level "<<LEVEL<<endl;
    //first satisfy all clauses with literal, and remove
    //negate literal from clasues
    satisfyClauses(var, equals, val);
    removeLiteral(var, !equals, val);
    VARLIST[var]->ATOMASSIGN[val] = 1;
    VARLIST[var]->ATOMLEVEL[val] = LEVEL;

    VARLIST[var]->VAL = val;
    VARLIST[var]->SAT = true; // means variable is assigned
    VARLIST[var]->LEVEL = LEVEL; // value assigned a positive value at this level

    VARLIST[var]->CLAUSEID[val] = UNITCLAUSE;
    //    if (LOG) cout<<"The reason for the literal: "<<endl;
    //  if (UNITCLAUSE > -1) CLAUSELIST[UNITCLAUSE]->Print(); else if (LOG) cout<<UNITCLAUSE<<endl;
    //Add literal to DecisionStack
    DECSTACK.push_back(new Literal(var, '=', val));

    //foreach domain value x from dom(v) which is not assigned
    //assign it

    for(int i=0; i<val && !CONFLICT; i++)
    {

      if(VARLIST[var]->ATOMASSIGN[i] == 0)
      {
        satisfyClauses(var, !equals, i);
        removeLiteral(var, equals, i);
        VARLIST[var]->ATOMASSIGN[i] = -1;

        VARLIST[var]->ATOMLEVEL[i] = LEVEL;

        VARLIST[var]->CLAUSEID[i] = UNITCLAUSE;
        //      if (LOG) cout<<"Set the reason for the literal "<<var<<(!equals?"=":"!")<<i<<endl;
        //    if (UNITCLAUSE > -1) CLAUSELIST[UNITCLAUSE]->Print(); else if (LOG) cout<<-1<<endl;

      }
    }
    for(int i=val+1; i<VARLIST[var]->DOMAINSIZE && !CONFLICT; i++)
    {
      if(VARLIST[var]->ATOMASSIGN[i] == 0)
      {
        satisfyClauses(var, !equals, i);
        removeLiteral(var, equals, i);

        VARLIST[var]->ATOMASSIGN[i] = -1;
        VARLIST[var]->ATOMLEVEL[i] = LEVEL;
        VARLIST[var]->CLAUSEID[i] = UNITCLAUSE;
        //      if (LOG) cout<<"Set the reason for the literal "<<var<<(!equals?"=":"!")<<i<<endl;
        //  if (UNITCLAUSE > -1) CLAUSELIST[UNITCLAUSE]->Print(); else if (LOG) cout<<-1<<endl;


      }
    }
  }
  else
  {
    // if (LOG) cout<<"Reducing: "<<var<<"!"<<val<<" at level "<<LEVEL<<endl;

    //first satisfy all clauses with negate literal, and remove
    //literal from claues
    satisfyClauses(var, equals, val);
    removeLiteral(var, !equals, val);
    VARLIST[var]->ATOMASSIGN[val] = -1;
    // reason and level should be assigned only once:

    // if(VARLIST[var]->ATOMLEVEL[val] == -1)
    VARLIST[var]->ATOMLEVEL[val] = LEVEL;
    //   if(VARLIST[var]->CLAUSEID[val] == -10)
    VARLIST[var]->CLAUSEID[val] = UNITCLAUSE;
    //Add literal to DecisionStack
    //    if (LOG) cout<<"Adding literal to the decision stack: "<<var<<"!"<<val<<endl;
    DECSTACK.push_back(new Literal(var, '!', val));

  }
  //check Entailment on this variable
  if(checkEntail(var))
  {
    ENTAILS++;
    //if (LOG) cout<<"Entailment... "<<ENTAILLITERAL->VAR<<"="<<ENTAILLITERAL->VAL<<endl;
    UNITCLAUSE = -2;
    reduceTheory(ENTAILLITERAL->VAR, true, ENTAILLITERAL->VAL);

  }

}

//satisfyClauses
inline void Formula::satisfyClauses(int var, bool equals, int val)
{
  int lit_var = -1;
  bool lit_equal = false;
  int lit_val = -1;
  VARRECORD * current = NULL;
  if(equals)
  current = VARLIST[var]->ATOMRECPOS[val];
  else
  current = VARLIST[var]->ATOMRECNEG[val];
  //for every clause that contains this literal, satisfy it
  //and update the counts of the literals
  while(current)
  {
    if(!CLAUSELIST[current->c_num]->SAT)
    {
      CLAUSELIST[current->c_num]->SAT = true;
      CLAUSELIST[current->c_num]->LEVEL = LEVEL;
      for(int i=0; i<CLAUSELIST[current->c_num]->NumAtom; i++)
      {
        lit_var = CLAUSELIST[current->c_num]->ATOM_LIST[i]->VAR;
        lit_equal = CLAUSELIST[current->c_num]->ATOM_LIST[i]->EQUAL;
        lit_val = CLAUSELIST[current->c_num]->ATOM_LIST[i]->VAL;
        if(VARLIST[lit_var]->ATOMASSIGN[lit_val] == 0)
        {
          CLAUSELIST[current->c_num]->NumUnAss--;
          if(lit_equal)
          VARLIST[lit_var]->ATOMCNTPOS[lit_val]--;
          else
          VARLIST[lit_var]->ATOMCNTNEG[lit_val]--;
        }
      }
    }
    current = current->next;
  }
  current = NULL;
}

inline void Formula::watchedSatisfyLiteral(int var, bool equals, int val)
{
  VARRECORD * current = NULL;
  // get clauses with atom var?val
  if(equals)
  current = VARLIST[var]->ATOMRECPOS[val];
  else
  current = VARLIST[var]->ATOMRECNEG[val];
  //for every clause that contains this literal update watched literals
  while(current)
  {
    Literal * watched1 = CLAUSELIST[current->c_num]->WATCHED[0];
    Literal * watched2 = CLAUSELIST[current->c_num]->WATCHED[1];

    if ( watched1 -> SAT != 1 )
    {
      if ( !( watched2 ->VAR == var &&  watched2 ->VAL == val && watched2 ->EQUAL == equals ) ) {

        for ( int j = 0; j < CLAUSELIST[current->c_num] -> NumAtom; j++ ) {
          if ( watched1 ->VAR == var &&  watched1 ->VAL == val &&
            watched1 ->EQUAL == equals )

            watched1 = CLAUSELIST[current->c_num] ->ATOM_LIST[j]; break;   // assign literal from the clause
          }
        }

        else {
          watched2 = watched1;

          for ( int j = 0; j < CLAUSELIST[current->c_num] -> NumAtom; j++ ) {
            if ( watched1 ->VAR == var &&  watched1 ->VAL == val && watched1 ->EQUAL == equals ) watched1 = CLAUSELIST[current->c_num] ->ATOM_LIST[j]; break;   // assign literal from the clause

          }
        }
      }
      current = current -> next;
    }

  }

  //satisfyClauses
  inline void Formula::watchedSatisfyLiteral(Literal * literal)
  {
    VARRECORD * current = NULL;
    literal -> SAT = 1;
    literal -> LEVEL = LEVEL;

    // get clauses with occurences of atom var equal val
    if ( literal -> EQUAL )
    current = VARLIST[literal -> VAR]->ATOMRECPOS[literal -> VAL];
    else
    current = VARLIST[literal -> VAR]->ATOMRECNEG[literal -> VAL];
    //for every clause that contains this literal update watched literals
    while(current)
    {
      Literal * watched1 = CLAUSELIST[current->c_num]->WATCHED[0];
      Literal * watched2 = CLAUSELIST[current->c_num]->WATCHED[1];

      if ( watched1 -> SAT != 1 )
      {
        if ( watched2 != NULL && !LitisEqual(watched2, literal) ) watched1 = literal;

        else {
          watched2 = watched1;
          watched1 = literal;
        }
      }
      current = current -> next;
    }
  }


  // falsify clauses with the literal
  inline void Formula::watchedFalsifyLiteral ( Literal* literal ) {

    VARRECORD * current = NULL;


    if ( literal -> EQUAL )
    current = VARLIST[literal -> VAR]->ATOMRECPOS[literal -> VAL];
    else
    current = VARLIST[literal -> VAR]->ATOMRECNEG[literal -> VAL];

    int var = literal -> VAR;
    int val = literal -> VAR;
    bool equals = literal -> EQUAL;

    while ( current )
    {
      if ( !CLAUSELIST[current->c_num] -> SAT ) {

        for ( int i = 0; i < CLAUSELIST[current->c_num] -> NumAtom; i++ ) {
          Literal* falsified = CLAUSELIST[current->c_num] -> ATOM_LIST[i];
          if ( falsified -> SAT == 2 && literal -> VAR == falsified -> VAR && literal -> VAL == falsified -> VAL && literal -> EQUAL != falsified -> EQUAL ) falsified -> SAT = 0;

          else if ( falsified -> SAT == 2 && literal -> VAR == falsified -> VAR && literal -> VAL != falsified -> VAL && literal -> EQUAL == falsified -> EQUAL && literal -> EQUAL) falsified -> SAT = 0;

        }

        CLAUSELIST[current->c_num]->NumUnAss--;
        if ( equals )
        VARLIST[var]->ATOMCNTPOS[val]--;
        else
        VARLIST[var]->ATOMCNTNEG[val]--;

      }
      current = current->next;
    }


    if ( literal -> EQUAL )
    current = VARLIST[literal -> VAR]->ATOMRECPOS[literal -> VAL];
    else
    current = VARLIST[literal -> VAR]->ATOMRECNEG[literal -> VAL];

    while ( current ) {

      Literal* watched1 = CLAUSELIST[current->c_num] -> WATCHED[0];
      Literal* watched2 = CLAUSELIST[current->c_num] -> WATCHED[1];

      if (     watchedCheckSat() != 0 &&
watched1->SAT != 1  && ( LitisEqual( watched1, literal )
      || ( watched2 != NULL && LitisEqual( watched2, literal ) ) ) ) {
      SwapPointer ( CLAUSELIST[current->c_num] );
      }
    current = current->next;
    }
  }

  inline void Formula::watchedFalsifyLiteral(int var, bool equals, int val) {

    VARRECORD * current = NULL;

    if(equals)
    current = VARLIST[var]->ATOMRECPOS[val];
    else
    current = VARLIST[var]->ATOMRECNEG[val];

    while ( current ) {
      Literal* watched1 = CLAUSELIST[current->c_num] -> WATCHED[0];
      Literal* watched2 = CLAUSELIST[current->c_num] -> WATCHED[1];

      if( watched1->SAT != 1  && ( ( watched1 ->VAR == var &&  watched1 ->VAL == val && watched1 ->EQUAL == equals )
      || ( watched2 ->VAR == var &&  watched2 ->VAL == val && watched2 ->EQUAL == equals ) ) ) {

        SwapPointer ( CLAUSELIST[current->c_num] );
        current = current->next;

      }
    }
  }

  //removeLiteral
  inline void Formula::removeLiteral(int var, bool equals, int val)
  {
    VARRECORD * current = NULL;
    //for every record of this literal reduce the number of
    //unassigned literals from unsatisfied clauses
    if(equals)
    current = VARLIST[var]->ATOMRECPOS[val];
    else
    current = VARLIST[var]->ATOMRECNEG[val];
    while(current)
    {
      if(!CLAUSELIST[current->c_num]->SAT)
      {
        CLAUSELIST[current->c_num]->NumUnAss--;
        if(equals)
        VARLIST[var]->ATOMCNTPOS[val]--;
        else
        VARLIST[var]->ATOMCNTNEG[val]--;
        if(CLAUSELIST[current->c_num]->NumUnAss == 1){ //if (LOG) cout<<"adding unit clause: "<<current->c_num<<endl;
        UNITLIST.push_front(current->c_num);  }

        if(CLAUSELIST[current->c_num]->NumUnAss == 0)
        {
          CONFLICT = true;
          CONFLICTINGCLAUSE = current->c_num;
        }
      }
      current = current->next;
    }
    current = NULL;
  }

  //undoTheory
  void Formula::undoTheory(int level)
  {
    //for each variable v
    //for each domain d from dom(v) .. undo
    for(unsigned int  i=1; i<VARLIST.size(); i++)
    {
      for(int j=0; j<VARLIST[i]->DOMAINSIZE; j++)
      {
        //if this domain has been assigned at level this or greater undo
        if(VARLIST[i]->ATOMLEVEL[j] > level)
        {
          if(VARLIST[i]->ATOMASSIGN[j] == -1)
          addLiteral(i, true, j);
          else
          addLiteral(i, false, j);
        }
      }
    }
    //for each variable v
    //for each domain d from dom(v) .. undo
    for(unsigned int  i=1; i<VARLIST.size(); i++)
    {
      for(int j=0; j<VARLIST[i]->DOMAINSIZE; j++)
      {
        //if this domain has been assigned at level this or greater undo
        if(VARLIST[i]->ATOMLEVEL[j] > level)
        {
          if(VARLIST[i]->ATOMASSIGN[j] == -1)
          unsatisfyClauses(i, false, j, level);
          else
          unsatisfyClauses(i, true, j, level);

          VARLIST[i]->ATOMLEVEL[j] = -10;
          VARLIST[i]->ATOMASSIGN[j] = 0;
          VARLIST[i]->CLAUSEID[j] = -10;
        }
      }
      if(VARLIST[i]->LEVEL > level)
      {
        VARLIST[i]->LEVEL = -1;
        VARLIST[i]->SAT = false;
        VARLIST[i]->VAL = -1;
      }
    }
    //undo the decision stack
    int decsize = DECSTACK.size();
    for(int i=decsize-1; i>-1; i--)
    {
      if(VARLIST[DECSTACK[i]->VAR]->ATOMASSIGN[DECSTACK[i]->VAL] == 0)
      {
        DECSTACK.erase(DECSTACK.begin()+i);
      }
    }
  }

  //unsatisfyClauses, used in undoTheory(level)
  inline void Formula::unsatisfyClauses(int var, bool equals, int val, int level)
  {
    VARRECORD * current = NULL;
    int lit_var = -1;
    bool lit_equal = false;
    int lit_val = -1;

    if(equals)
    current = VARLIST[var] -> ATOMRECPOS[val];
    else
    current = VARLIST[var] -> ATOMRECNEG[val];

    while (current) {
      if ( CLAUSELIST[current->c_num] -> LEVEL > level ) {
        for (int i=0; i<CLAUSELIST[current->c_num]->NumAtom; i++) {

          lit_var = CLAUSELIST[current->c_num]->ATOM_LIST[i]->VAR;
          lit_equal = CLAUSELIST[current->c_num]->ATOM_LIST[i]->EQUAL;
          lit_val = CLAUSELIST[current->c_num]->ATOM_LIST[i]->VAL;

          if ( ( VARLIST[lit_var]->ATOMLEVEL[lit_val] > level )
          || ( VARLIST[lit_var]->ATOMASSIGN[lit_val] == 0 ) ) // the condition seems wrong TODO FIX!!!
          {
            CLAUSELIST[current->c_num] -> NumUnAss++;

            if(lit_equal) // was if (equals) WRONG ?
            VARLIST[lit_var]->ATOMCNTPOS[lit_val]++;
            else
            VARLIST[lit_var]->ATOMCNTNEG[lit_val]++;
          }
        }
        CLAUSELIST[current->c_num]->LEVEL = -1;
        CLAUSELIST[current->c_num]->SAT = false;
      }
      current = current->next;
    }
    current = NULL;
  }

  //addLiteral : used when undoing the theory
  inline void Formula::addLiteral(int var, bool equals, int val)
  {
    // if (LOG) cout<<"add .. "<<var<<(equals?"=":"!")<<val<<endl;
    VARRECORD * current = NULL;
    //for every record of this literal increase the number of
    //unassigned literals from unsatisfied clauses
    if(equals)
    current = VARLIST[var]->ATOMRECPOS[val];
    else
    current = VARLIST[var]->ATOMRECNEG[val];
    while(current)
    {
      if(!CLAUSELIST[current->c_num]->SAT)
      {
        CLAUSELIST[current->c_num]->NumUnAss++;
        if(equals)
        VARLIST[var]->ATOMCNTPOS[val]++;
        else
        VARLIST[var]->ATOMCNTNEG[val]++;
      }
      current = current->next;
    }
    current = NULL;
  }


  bool Formula::HasAtom(Clause * clause, Literal * atom)
  {
    for(int i=0; i<clause->NumAtom;i++){
      if (clause->ATOM_LIST[i]->VAR == atom->VAR && clause->ATOM_LIST[i]->EQUAL == atom->EQUAL && clause->ATOM_LIST[i]->VAL == atom->VAL) return true;
    }
    return false;
  }

  Clause * Formula::resolve(Clause * clause, Literal * literal, Clause * reason)
  {
    Clause * resolvent = new Clause();
    // the literals from clause C that are satisfied by at least one interpretation that does not satisfy L
    for ( unsigned int i=0; i<clause->ATOM_LIST.size();i++ ) {
      if (clause->ATOM_LIST[i]->VAR != literal->VAR ) resolvent->AddAtom(clause->ATOM_LIST[i]);

      else if (clause->ATOM_LIST[i]->VAL == literal->VAL && clause->ATOM_LIST[i]->EQUAL != literal->EQUAL) resolvent->AddAtom(clause->ATOM_LIST[i]);

      else if (clause->ATOM_LIST[i]->VAL != literal->VAL && clause->ATOM_LIST[i]->EQUAL == literal->EQUAL) resolvent->AddAtom(clause->ATOM_LIST[i]);

    }
    // the literals from C that are satisfied by at least one interpretation that also satisfies L.
    for (unsigned int  i=0; i<reason->ATOM_LIST.size();i++)
    {
      if (reason->ATOM_LIST[i]->VAR != literal->VAR && !HasAtom(resolvent,reason->ATOM_LIST[i])) resolvent->AddAtom(reason->ATOM_LIST[i]);

      else if (reason->ATOM_LIST[i]->VAL == literal->VAL && reason->ATOM_LIST[i]->EQUAL == literal->EQUAL && !HasAtom(resolvent,reason->ATOM_LIST[i])) resolvent->AddAtom(reason->ATOM_LIST[i]);

      else if (reason->ATOM_LIST[i]->VAL != literal->VAL && reason->ATOM_LIST[i]->EQUAL != literal->EQUAL && !HasAtom(resolvent,reason->ATOM_LIST[i])) resolvent->AddAtom(reason->ATOM_LIST[i]);

      else if (reason->ATOM_LIST[i]->VAL != literal->VAL && reason->ATOM_LIST[i]->EQUAL == literal->EQUAL && reason->ATOM_LIST[i]->EQUAL ==  false && !HasAtom(resolvent,reason->ATOM_LIST[i])) resolvent->AddAtom(reason->ATOM_LIST[i]);
    }

    return resolvent;
  }

  // Check if there is exactly one atom falsified at the current level
  bool Formula::Potent ( Clause* clause ) {
    // int index = 0;
    int counter = 0;
    for ( int i = 0; i < clause -> NumAtom; i++ ) {

      Literal* atom = clause->ATOM_LIST[i];

      if ( VARLIST[atom->VAR]->ATOMLEVEL[atom->VAL] == LEVEL ) {
        //  index = i;
        counter++;
      }
    }

    if(counter != 1) return false;
    else return true;
  }

  int Formula::backtrackLevel ( Clause * learnedClause ) {

    if (LOG) cout << "Finding backtrack level..."<<endl;
    int max = -1;
    int csize = learnedClause -> NumAtom;

    //if learned clause has only one literal then backtrack to the level 0
    if ( csize == 1 )
    return 0;

    for ( int i = 0; i < csize; i++ ) {

      Literal* atom = learnedClause -> ATOM_LIST[i];
      int atom_level = VARLIST[atom -> VAR] -> ATOMLEVEL[atom -> VAL];

      if ( LEVEL > atom_level && max < atom_level )
      max = atom_level;
    }

    if(max != -1)
    return max;
    else
    return LEVEL--;
  }

  //--------------- Resolution-based clause learning ---------------------//

  Clause* Formula::analyzeConflict ( Clause * clause ) {

    /*
    First check if there is exactly one atom falsified at the current level.
    If yes, learn the clause: this way upon backtrack it becomes unit. Otherwise
    continue analyzing the conflict.
    */
    if ( Potent ( clause ) ) {
      // After backtracking the clause should be detected as unit
      clause -> NumUnAss = 0;
      if (LOG) {
        cout << "Learned a clause: " << endl;
        clause->Print();
      }
      // add the clause to the clause list
      CLAUSELIST.push_back( clause );
      // it's id
      int cid = CLAUSELIST.size()-1;
      // update global records for each atom in the clause
      for ( int i = 0; i < clause -> NumAtom; i++ ) {
        Literal* atom = clause->ATOM_LIST[i];
        VARLIST[atom->VAR] -> AddRecord( cid, atom->VAL, atom->EQUAL);
      }

      return clause;

    }

    // Resolve the clause and its latest falsified literal's reason

    // Latest falsified literal:
    Literal* lastFalse = clause -> ATOM_LIST[maxLit ( clause )[0]];
    int var = lastFalse -> VAR;
    int val = lastFalse -> VAL;
    Clause * resolvent = new Clause();

    if ( LOG ) {
      cout << "Latest falsified literal: " << endl;
      lastFalse -> Print ();
      cout << "It's reason: " << endl;
      cout << VARLIST[var]-> CLAUSEID[val] << endl;
    }

    // Generate reasons for decision and entail reasons:
    Clause* reason = new Clause ();
    if ( VARLIST[var]-> CLAUSEID[val] == -1 ) {
      // Take literal that falsified lastFalse
      Literal * falsifier = DECSTACK[maxLit(clause)[1]];
      // Generate the reason clause:
      reason -> AddAtom ( new Literal ( falsifier -> VAR, '=', falsifier -> VAL ) );
      reason -> AddAtom ( new Literal ( falsifier -> VAR, '!', falsifier -> VAL ) );

    } else if ( VARLIST[var] -> CLAUSEID[val] == -2 ) {

      for ( int i = 0; i < VARLIST[var] -> DOMAINSIZE; i++ ) {
        reason -> AddAtom ( new Literal ( var, '=', i ) );
      }
    } else { reason = CLAUSELIST[VARLIST[var]-> CLAUSEID[val]]; }

    // resolve:
    resolvent = resolve( clause, lastFalse, reason );

    if (LOG) { cout<<"Resolvent:"<<endl;
    resolvent->Print(); }
    return analyzeConflict(resolvent);
  }
  //Finding the literal falsified the latest in the clause
  vector<int> Formula::maxLit(Clause * clause)
  { //if (LOG) cout<<"maxLit on: "<<endl;
  //  clause->Print();
  vector<int> result;
  result.reserve(2);
  int index = 0;
  int clindex = 0;
  int decisions = DECSTACK.size();

  for(int i=0; i<clause->NumAtom; i++)
  { Literal * current = clause->ATOM_LIST[i];
    for(int j=decisions-1; j<decisions && j > -1; j--)
    {
      if( ( (current->VAR == DECSTACK[j]->VAR && current->VAL == DECSTACK[j]->VAL && current->EQUAL != DECSTACK[j]->EQUAL)  || ( current->VAR == DECSTACK[j]->VAR && current->VAL != DECSTACK[j]->VAL && current->EQUAL == DECSTACK[j]->EQUAL && current->EQUAL )  )  && index <= j) { //TODO FIX
        index = j; clindex = i;
      }
    }

  }
  // if (VARLIST[clause->ATOM_LIST[clindex]->VAR]->CLAUSEID[clause->ATOM_LIST[clindex]->VAL]==-1)
  result[0] = clindex;
  result[1] = index; //  literal in decstack that falsified ATOM_LIST[clindex]
  return  result ;//clause->ATOM_LIST[clindex];


  /* if (LOG) { cout<<"maxLit: "<<endl;
  clause->ATOM_LIST[clindex]->Print(); } */

}

// Return unit literal in the unit clause
Literal* Formula::unitLiteral ( Clause* unit ) {

  for ( int i = 0; i < unit -> NumAtom; i++ ) {

    int lit_var = unit -> ATOM_LIST[i] -> VAR;
    int lit_val = unit -> ATOM_LIST[i] -> VAL;

    if ( VARLIST[lit_var] -> ATOMASSIGN[lit_val] == 0 )
    return unit -> ATOM_LIST[i];
  }

  // if no unit literal found, return default literal -1 ! -1
  return new Literal(-1, '!', -1);

}


// WATCHED LITERALS ALGORITHM

// WE HAVE TO MODIFY SEVERAL FUNCTIONS:
int Formula::watchedCheckSat () {

// returns 1 if all watched1 is sat (1), 0 if some watched1 and watched2 falsified (0), otherwise 2 -undefined (2)

  if (LOG) cout << "Checking satisfiability..." << endl;

  for ( unsigned int  i = 0; i < CLAUSELIST.size(); i++ ) {

    Literal* watched1 = CLAUSELIST[i] -> WATCHED[0];
    Literal* watched2 = CLAUSELIST[i] -> WATCHED[1];

    if ( watched1 -> SAT == 0
      && (  watched2  == NULL || watched2 -> SAT == 0 ) ) {
        if (LOG) cout << "Found conflict!" << endl;
        CONFLICT = true;
        return 0;
      } else if ( watched1 -> SAT == 2 ) {
        return 2;
      }
    }
    return 1;
  }

  Literal* Formula::watchedCheckUnit () {

    // watched -> SAT == 2 unassigned, == 1 satisfied, == 0 falsified

    if (LOG) cout << "Checking for units..." << endl;

    for ( unsigned int i = 0; i < CLAUSELIST.size(); i++ ) {

      Literal* watched1 = CLAUSELIST[i] -> WATCHED[0];
      Literal* watched2 = CLAUSELIST[i] -> WATCHED[1];

      if ( watched1 -> SAT == 2
        && ( watched2 == NULL || watched2 -> SAT == 0 ) )
        return watched1;

        else if ( watched1 -> SAT == 0 &&  watched2 == NULL ) {
          CONFLICT = true;
          return NULL;
        }
        else if ( watched1 -> SAT == 0 && watched2 -> SAT == 2 ) return watched2;
      }
      return NULL;
    }


    // Choose any unassigned first watched literal
    Literal * Formula::watchedChooseLiteral () {

      for ( unsigned int  i = 0; i < CLAUSELIST.size(); i++ ) {

        Literal* watched1 = CLAUSELIST[i] -> WATCHED[0];

        if ( watched1 -> SAT == 2) return  watched1;
      }
      return NULL;
    }

    // Swap watched1 and watched2
    void Formula::SwapPointer ( Clause* clause ) {

      Literal* watched1 = clause -> WATCHED[0];
      Literal* watched2 = clause -> WATCHED[1];

      if ( watched2 != NULL && watched2 -> SAT == 2 ) {
        for ( int i; i < clause -> NumAtom; i++ ) {

          Literal* literal = clause -> ATOM_LIST[i];

          if ( literal -> SAT == 2 && !LitisEqual( literal, watched2 ) )
          watched1 = literal;
        }
      } else if ( watched1 -> SAT == 2 ) {
        for ( int i; i < clause -> NumAtom; i++ ) {
          Literal* literal = clause -> ATOM_LIST[i];

          if ( literal -> SAT == 2 && !LitisEqual ( literal, watched1 ) )
          watched2 = literal;
        }
      }
    }

    bool Formula::LitisEqual(Literal * literal1, Literal * literal2){
      if (literal1 -> VAR == literal2 -> VAR && literal1 -> VAL == literal2 -> VAL
        && literal1 -> EQUAL == literal2 -> EQUAL) return true;
        else return false;
      }

      void Formula::watchedReduceTheory ( Literal * literal, int var, bool equals, int val ) {

        literal -> SAT = 1;
        literal -> LEVEL = LEVEL;

        if ( equals ) {
          if ( LOG ) cout << "Reducing literal: " << var << "=" << val << " at level " << LEVEL << endl;
          // satisfy literal in the clauses where it appears
          watchedSatisfyLiteral ( literal );
          // remove literal from the clauses where it's negation appears
          watchedFalsifyLiteral ( literal );

          // TODO: change analyzeConflict such that we don't need this bookeeping, use only watched literals info:
          VARLIST[var]->ATOMASSIGN[val] = 1;
          VARLIST[var]->ATOMLEVEL[val] = LEVEL;
          VARLIST[var]->VAL = val;
          VARLIST[var]->SAT = true; // means variable is assigned. REDUNDANT!
          VARLIST[var]->LEVEL = LEVEL; // value assigned a positive value at this level

          // Set the reason for the literal:
          VARLIST[var]->CLAUSEID[val] = UNITCLAUSE;
          if ( LOG ) {
            cout << "The reason for the literal: " << endl;
            if ( UNITCLAUSE > -1 ) CLAUSELIST[UNITCLAUSE] -> Print();
            else cout << UNITCLAUSE << endl;
        }
          // Add literal to the decision stack
          DECSTACK.push_back(literal);

          //for each different domain value x from dom(var) which is not assigned (0) assign it

          for ( int i = 0; i < val; i++ ) { // had && !CONFLICT in the condition - check!

            if ( VARLIST[var] -> ATOMASSIGN[i] == 0 ) {

              watchedSatisfyLiteral(var, !equals, i);
              watchedFalsifyLiteral(var, equals, i);

              VARLIST[var]->ATOMASSIGN[i] = -1;
              VARLIST[var]->ATOMLEVEL[i] = LEVEL;

              // Set the same reason:
              VARLIST[var]->CLAUSEID[i] = UNITCLAUSE;
            }
          }

          for ( int i = val + 1; i < VARLIST[var] -> DOMAINSIZE; i++ ) {
            // had && !CONFLICT in the condition - check!

            if ( VARLIST[var] -> ATOMASSIGN[i] == 0 ) {

              watchedSatisfyLiteral(var, !equals, i);
              watchedFalsifyLiteral(var, equals, i);

              VARLIST[var]->ATOMASSIGN[i] = -1;
              VARLIST[var]->ATOMLEVEL[i] = LEVEL;

              // Set the same reason:
              VARLIST[var] -> CLAUSEID[i] = UNITCLAUSE;

            }
          }
        } else {

          if (LOG) cout << "Reducing: " << var << "!" << val << " at level " << LEVEL << endl;

          //first satisfy all clauses with literal, and remove
          //literal from clauses
          watchedSatisfyLiteral ( literal );
          cout << "satisfied" << endl;
          watchedFalsifyLiteral ( literal );
          cout << "falsified" << endl;


          VARLIST[var]->ATOMASSIGN[val] = -1;
          VARLIST[var]->ATOMLEVEL[val] = LEVEL;

          // Set the reason:
          VARLIST[var]->CLAUSEID[val] = UNITCLAUSE;

          //Add literal to decision stack:
          DECSTACK.push_back(literal);

        }
        //check Entailment on this variable
        if ( checkEntail ( var ) ) {
          if (LOG) cout << "Entailment... " << ENTAILLITERAL -> VAR << "=" << ENTAILLITERAL -> VAL << endl;
          ENTAILS++;
          // Set the reason:
          UNITCLAUSE = -2;
          watchedReduceTheory ( ENTAILLITERAL, ENTAILLITERAL -> VAR, true, ENTAILLITERAL -> VAL );
        }
      }
      //=================== Watched literals NON-CHRONOLOGICAL BACKTRACK ============================//

      int Formula::WatchedLiterals () {

        // returns 0 if sat, 1 if timeout, 2 if unsat

        if (LOG) cout << "Solving with watched literal algorithm..." << endl;

        while ( true ) {
          //Check if the theory satisfied
          if ( watchedCheckSat() == 1 )
            return 0; //

          //Check if time out
          TIME_E = GetTime();
          if ( ( TIME_E - TIME_S ) > TIMELIMIT )
            return 1;

          //check if conflict
          if ( CONFLICT ) {

             if ( LOG ) {
               cout << "There is a conflict at level: " << LEVEL << endl;
               cout<<"Conflicting clause: "<<  endl;
             CLAUSELIST[CONFLICTINGCLAUSE] -> Print ();
           }


          if ( LEVEL == 0 ) return 2;

          LEVEL = backtrackLevel ( analyzeConflict ( CLAUSELIST[CONFLICTINGCLAUSE] ) );
          BACKTRACKS++;
          if ( LOG ) {
             cout << "We are backtracking to the level: " << LEVEL << endl;
             cout << "# of backtracks so far: "<<BACKTRACKS<<endl;
           }
          CONFLICT = false;
          undoTheory ( LEVEL );
        }
        // If there is a unit clause, propagate
        Literal* unit = watchedCheckUnit ();
        if ( unit ) {
          if ( LOG ) cout << "Found unit!" << endl;
          watchedReduceTheory ( unit, unit -> VAR, unit -> EQUAL, unit -> VAL );
        }
        // otherwise choose a literal and propagate - no need for separate unit propagation
        else if ( !CONFLICT ) {
          Literal * atom = watchedChooseLiteral ();
          if ( atom ) {
            DECISIONS++;
            LEVEL++;
            UNITCLAUSE = -1;
            if ( LOG ) cout << "Decision: " << atom -> VAR << ( atom -> EQUAL ? '=' : '!' ) << atom -> VAL << endl;

            // set REASON for subsequent falsified atoms
            atom -> SAT = 1;
            atom -> LEVEL = LEVEL;
            watchedReduceTheory ( atom, atom -> VAR, atom -> EQUAL, atom -> VAL );
          }

        }
      }

    }




    // with restarts
    int Formula::NonChronoBacktrack(int restarts)
    { int restartCount = restarts;
      //int RESTARTS = 0;
      // LEVEL = 0;
      //start of finite domain extended dpll
      // return 0 : if theory satisfied
      // return 1 : if time out
      // return 2 : if CONFLICT and later used as unsatisfied
      while(true){
        // if (LOG) cout<<"Number of clauses so far: "<<CLAUSELIST.size()<<endl;
        //Check if theory satisfied
        if(checkSat())
        return 0; // add PrintModel(); - from DECSTACK
        //Check if time out
        TIME_E = GetTime();
        if((TIME_E - TIME_S) > TIMELIMIT)
        return 1;
        //check if conflict

        if(CONFLICT)
        { if (LOG) cout << "There is a conflict at level: " << LEVEL << endl;
        if (LOG) { cout<<"Conflicting clause: "<<  endl;
        CLAUSELIST[CONFLICTINGCLAUSE] -> Print(); }

        if(LEVEL == 0) return 2;

        LEVEL = backtrackLevel(analyzeConflict(CLAUSELIST[CONFLICTINGCLAUSE]));
        if (LOG) cout << "We are backtracking to the level: " << LEVEL << endl;
        BACKTRACKS++;
        if (LOG) cout << "# of backtracks so far: "<<BACKTRACKS<<endl;
        CONFLICT = false;
        if(BACKTRACKS == restartCount) {
          undoTheory(0);
          restartCount = BACKTRACKS + restarts; RESTARTS++;
        }
        else undoTheory(LEVEL);
      }
      // If there is a unit clause, propagate

      checkUnit();
      if(!UNITLIST.empty())
      unitPropagation();

      // otherwise choose a literal and propagate - no need for separate unit propagation
      if(!CONFLICT)
      {

        Literal * atom = chooseLiteral();
        if(atom)
        {
          DECISIONS++;
          LEVEL++;
          if (LOG) cout<<"Decision: "<<atom->VAR<<(atom->EQUAL?'=':'!')<<atom->VAL<<endl;
          UNITCLAUSE = -1; // REASON for subsequent falsified atoms
          reduceTheory(atom->VAR, atom->EQUAL, atom->VAL);
        }

      }
    }


  }


  int Formula::NonChronoBacktrack()
  { //int restartCount = restarts;
    //int RESTARTS = 0;
    // LEVEL = 0;
    //start of finite domain extended dpll
    // return 0 : if theory satisfied
    // return 1 : if time out
    // return 2 : if CONFLICT and later used as unsatisfied
    while(true){
      // if (LOG) cout<<"Number of clauses so far: "<<CLAUSELIST.size()<<endl;
      //Check if theory satisfied
      if(checkSat())
      return 0; // add PrintModel(); - from DECSTACK
      //Check if time out
      TIME_E = GetTime();
      if((TIME_E - TIME_S) > TIMELIMIT)
      return 1;
      //check if conflict

      if(CONFLICT)
      { if (LOG) cout << "There is a conflict at level: " << LEVEL << endl;
      if (LOG) { cout<<"Conflicting clause: "<<  endl;
      CLAUSELIST[CONFLICTINGCLAUSE] -> Print();}

      if(LEVEL == 0) return 2;

      LEVEL = backtrackLevel(analyzeConflict(CLAUSELIST[CONFLICTINGCLAUSE]));
      if (LOG) cout << "We are backtracking to the level: " << LEVEL << endl;
      BACKTRACKS++;
      if (LOG) cout << "# of backtracks so far: "<<BACKTRACKS<<endl;
      CONFLICT = false;
      undoTheory(LEVEL);
    }
    // If there is a unit clause, propagate

    checkUnit();
    if(!UNITLIST.empty())
    unitPropagation();

    // otherwise choose a literal and propagate - no need for separate unit propagation
    if(!CONFLICT)
    {

      Literal * atom = chooseLiteral();
      if(atom)
      {
        DECISIONS++;
        LEVEL++;
        if (LOG) cout<<"Decision: "<<atom->VAR<<(atom->EQUAL?'=':'!')<<atom->VAL<<endl;
        UNITCLAUSE = -1; // REASON for subsequent falsified atoms
        reduceTheory(atom->VAR, atom->EQUAL, atom->VAL);
      }

    }
  }


}


int Formula::ChronoBacktrack(int level)
{
  //start of finite domain extended dpll
  // return 0 : if theory satisfied
  // return 1 : if time out
  // return 2 : if conflict and later used as unsatisfied

  //set LEVEL
  LEVEL = level;

  //check if theory satisfied or not
  if(checkSat())
  return 0;

  //check if time out
  TIME_E = GetTime();
  if((TIME_E - TIME_S) > TIMELIMIT)
  return 1;

  //check unit literal
  if(!UNITLIST.empty())
  unitPropagation();
  //check if theory satisfied or not
  if(checkSat())
  return 0;

  //check if time out
  TIME_E = GetTime();
  if((TIME_E - TIME_S) > TIMELIMIT)
  return 1;

  //check if conflict
  if(CONFLICT)
  {
    BACKTRACKS++;
    CONFLICT = false;
    undoTheory(LEVEL-1);
    LEVEL = LEVEL-1;
    return 2;
  }

  //since all is fine, now need to choose a literal
  //to branch on
  Literal * atom = chooseLiteral();
  if(atom)
  {
    DECISIONS++;
    LEVEL++;
    reduceTheory(atom->VAR, atom->EQUAL, atom->VAL);
    int result = ChronoBacktrack(LEVEL);
    if(result == 0)
    return 0;
    else if(result == 1)
    return 1;
    else if((result == 2) && (LEVEL != 0))
    {
      reduceTheory(atom->VAR, !atom->EQUAL, atom->VAL);
      return ChronoBacktrack(LEVEL);
    }
    else
    return 2;
  }
  else
  {
    cout<<"No Branch Atom selected"<<endl;
  }
  return 0;
}

bool Formula::unitPropagation()
{
  int lit_var = -1;
  bool lit_equal = false;
  int lit_val = -1;
  int unit_clause = -1;
  bool flag = false;

  //while there is no conflict and there exists a unit clause
  //find the unit literal and satisfy it
  while(!CONFLICT && !UNITLIST.empty())
  {
    UNITS++;
    unit_clause = UNITLIST.front();
    UNITCLAUSE = unit_clause;
    //if (LOG) cout<<"unit c : "<<unit_clause<<endl;
    UNITLIST.pop_front();
    if(!CLAUSELIST[unit_clause]->SAT)
    {
      for(int i=0; i<CLAUSELIST[unit_clause]->NumAtom && !flag; i++)
      {
        lit_var = CLAUSELIST[unit_clause]->ATOM_LIST[i]->VAR;
        lit_equal = CLAUSELIST[unit_clause]->ATOM_LIST[i]->EQUAL;
        lit_val = CLAUSELIST[unit_clause]->ATOM_LIST[i]->VAL;
        if(VARLIST[lit_var]->ATOMASSIGN[lit_val] == 0)
        {
          flag = true;
          //set the reason for this literal
          VARLIST[lit_var]->CLAUSEID[lit_val] = unit_clause;
        }
      }

      if(flag)
      {
        // if (LOG) cout<<"Reducing on unit literal: "<<lit_var<<(lit_equal?"=":"!=")<<lit_val<<endl;
        reduceTheory(lit_var, lit_equal, lit_val);
        flag = false;
      }
    }
  }
  if(!CONFLICT)
  return true;
  else
  {
    //  if (LOG) cout<<"Conflict : "<<CONFLICTINGCLAUSE<<endl;
    UNITLIST.clear();
    return false;
  }
}


//End Formula
