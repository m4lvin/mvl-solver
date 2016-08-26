//**************************************************************
//
// Finite Domain Solver
//
// File : Literal.h
// Description : Contains code for Literal class. Literal contains a
// variable and its value, eg. var = val or var ! val
//**************************************************************
#ifndef ATOM_H
#define ATOM_H

//Including Libraries and Header files
#include <iostream>
#include <stdio.h>
#include <iomanip>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <fstream>
#include <string>

using namespace std;
//**************************************************************
// Literal
// This class represents the Literal object for the solver which
// stores the variable name, and its value for the benchmark. It
// also stores a boolean value stating whether the atom is a
// positive or negative literal
//
// Start of Code
class Literal
{
  //public variables and functions
public:
  //VAR = variable name
  //VAL = value assigned to variable
  //EQUAL = is the atom = (true) or ! (false)
 // INDEX = point (# of previous decisions) when literal was falsified
  int VAR;
  int VAL;
  bool EQUAL;
  int SAT; // whether literal is sat 1, falsified 0, undef 2
  int LEVEL;
  bitset MULTIVAL; // to replace unary literals by multi-literals

  //Zero argument constructor
  Literal() { VAR = -1; VAL = -1; EQUAL = false; SAT = 2; LEVEL = -10;}
  //Two argument constructor
  Literal(int vr, int vl) { VAR = vr; VAL = vl; EQUAL = true; SAT = 2; LEVEL = -10;}
  //Three argument constructor
  Literal(int vr, char ch, int vl)
  { SAT = 2;
    VAR = vr; VAL = vl;
    if(ch == '=') EQUAL = true;
    else EQUAL = false;
    LEVEL = -10;
  }

  //Print : function to print the atom
  void Print()
  {
    cout<<VAR;
    if(EQUAL) cout<<"="<<VAL<<endl; else cout<<"!="<<VAL<<endl;
  //  cout<<INDEX;
  }

};
// End of Code
//
#endif
//**************************************************************