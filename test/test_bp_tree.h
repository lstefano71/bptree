#pragma once
#include "bp_tree.h"
#include <iosfwd>

typedef stdext::bp_tree<size_t, size_t> BpTree;

void fill( BpTree& bpt);
void iterate_forward( BpTree& bpt);
void iterate_backward( BpTree& bpt);
void create_bpt( const char* fileName, std::fstream& bptFile);
void open_bpt( const char* fileName, std::fstream& bptFile);
void compact_bpt( BpTree& bpt, const char* fileName);
void simple_test();
