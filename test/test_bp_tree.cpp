#pragma once
#include "test_bp_tree.h"
#include <fstream>
#include <stdlib.h>
#include <fstream>

using namespace std;

void fill( BpTree& bpt)
{
	typedef std::set<size_t> Set;
	Set s;

	const int n = 20000;
	for( int i = 0; i < n; ++i)	
	{
		s.insert( rand());
	}

	int x = s.size();
	for( Set::const_iterator i = s.begin(); i != s.end(); ++i)
	{
		*bpt.insert( *i) = *i;
	}
}

void iterate_forward( BpTree& bpt)
{
	for( BpTree::const_iterator i = bpt.begin(); i != bpt.end(); ++i)
	{
		*i;
	}
}

void iterate_backward( BpTree& bpt)
{
	for( BpTree::const_reverse_iterator i = bpt.rbegin(); i != bpt.rend(); ++i)
	{
		*i;
	}
}

void create_bpt( const char* fileName, fstream& bptFile)
{
	bptFile.open( fileName, ios_base::in | ios_base::out | ios_base::binary | ios_base::trunc, 64);
}

void open_bpt( const char* fileName, fstream& bptFile)
{
	bptFile.open( fileName, ios_base::in | ios_base::binary, 64);
}

void compact_bpt( BpTree& bpt, const char* fileName)
{
	fstream out;
	BpTree::stream_type stream( out);

	out.open( fileName, ios_base::in | ios_base::out | ios_base::binary | ios_base::trunc, 64);

	if ( out.is_open())
	{
		bpt.compact_to( stream);
	}
}

void simple_test()
{
	const char defaultFileName[] = "default.bpt";
	const char compactFileName[] = "compact.bpt";

	fstream bptFile;
	BpTree::stream_type stream( bptFile);

	bool newFile = false;
	bool compactFile = false;

	if ( newFile)
	{
		create_bpt( defaultFileName, bptFile);
	}
	else
	{
		open_bpt( defaultFileName, bptFile);
		if ( !bptFile.is_open())
		{
			open_bpt( compactFileName, bptFile);
		}
	}

	if ( bptFile.is_open())
	{
		BpTree bpt( 512);
		
		bptFile.seekg( 0, ios::end);
		streamsize fileSize = bptFile.tellg();
		bptFile.seekg( 0, ios::beg);
		bpt.open( stream, fileSize);

		if ( newFile)
		{
			fill( bpt);
		}

		iterate_forward( bpt);
		iterate_backward( bpt);

		if ( compactFile)
		{
			compact_bpt( bpt, compactFileName);
		}
	}
}
