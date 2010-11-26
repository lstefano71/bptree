#pragma once
/// B+ Tree
/// Copyright (c) Flaviu Cibu. All rights reserved.
/// Created 20-sep-‎2010
/// Updated 25-nov-‎2010

#ifndef PCH
	#include <algorithm>
	#include <hash_map>
	#include <map>
	#include <iostream>
	#include <cassert>
	#include "lru_cache.h"
#endif

#if !defined(BP_TREE_ASSERTIONS) && defined(_DEBUG)
	#define BP_TREE_ASSERTIONS
#endif

#ifdef BP_TREE_ASSERTIONS
	#define BP_TREE_ASSERT(x) assert(x)
#else
	#define BP_TREE_ASSERT(x)
#endif

namespace stdext
{
	template <typename _Key, typename _Val, typename _Bitmap>
	class bp_tree_default_stream
	{
		mutable std::iostream& io;

		void skip_keys( const size_t count)
		{
			skip( sizeof( key_type) * count);
		}

		void skip_data( const size_t count)
		{
			skip( sizeof( value_type) * count);
		}

		bool compact_;

	public:
		typedef _Key	key_type;
		typedef _Val	value_type;
		typedef size_t	offset_type;
		typedef _Bitmap bitmap_type;

		enum E
		{
			key_storage_size	= sizeof( _Key),
			value_storage_size	= sizeof( _Val)
		};

		bp_tree_default_stream( std::iostream& s): io( s), compact_( false)
		{
			s.seekg( 0);
		}

		bool is_compact() const 
		{
			return compact_;
		}

		void set_compact( const bool value)
		{
			compact_ = value;
		}

		void read( void* data, const size_t bytes)
		{
			io.read( (char*) data, bytes);
		}

		void write( const void* data, const size_t bytes)
		{
			io.write( (const char*) data, bytes);
		}

		void read_keys( key_type* const keys, const size_t used, const size_t count, const bitmap_type bmp)
		{
			read( keys, sizeof( key_type) * used);
			if ( !compact_)
			{
				skip_keys( count - used);
			}
		}

		void write_keys( const key_type* const keys, const size_t used, const size_t count, const bitmap_type bmp)
		{
			write( keys, sizeof( key_type) * used);
			if ( !compact_)
			{
				skip_keys( count - used);
			}
		}

		void read_offsets( offset_type* const items, const size_t used)
		{
			read( items, sizeof( offset_type) * used);
		}

		void read_offsets( offset_type* const items, const size_t used, const size_t count)
		{
			read( items, sizeof( offset_type) * used);
			if ( !compact_)
			{
				skip( sizeof( offset_type) * ( count - used));
			}
		}

		void read_data( value_type* const data, const size_t used, const size_t count, const bitmap_type bmp)
		{
			read( data, sizeof( value_type) * used);
			if ( !compact_)
			{
				skip_data( count - used);
			}
		}

		void write_data( const value_type* const data, const size_t used, const size_t count, const bitmap_type bmp)
		{
			write( data, sizeof( value_type) * used);
			if ( !compact_)
			{
				skip_data( count - used);
			}
		}

		void seek( const int pos)
		{
			io.seekg( pos);
		}

		size_t position() const
		{
			return io.tellg();
		}

		void skip( const size_t bytes)
		{
			io.seekg( bytes, 1);
		}

		bool ok() const
		{
			return !io.fail() && !io.bad();
		}
	};

	
	struct bp_tree_default_traits
	{
		enum E
		{
			slot_count		= 63,
			signature_size	= 2,
			leaf_marker_size= 2
		};

		typedef unsigned char		slotn_t;		// slot number type
		typedef unsigned long long	bitmap_type;

		static const char* const signature()	{ return "B+"; }
		static const char* const leaf_marker()	{ return "<>"; }
	};

	/// B+ Tree
	template <typename	_Key,									// key type
			typename	_Val,									// value type
			typename	_Traits		= bp_tree_default_traits,	// default traits
			typename	_Stream		= bp_tree_default_stream<_Key,_Val, typename bp_tree_default_traits::bitmap_type>,	// io stream type
			typename	_KeyComp	= void,						// key comparator predicate type - not used yet
			typename	_Alloc		= std::allocator< _Val>		// allocator
	>
	class bp_tree
	{
	public:
		typedef _Key		key_type;
		typedef _Val		value_type;
		typedef _Stream		stream_type;
		typedef _KeyComp	key_compare;
		typedef typename _Stream::offset_type	offset_type;

	protected:
		struct _Node;
		struct _Inner;
		struct _Leaf;

		typedef typename _Traits				traits;
		typedef typename _Traits::slotn_t		slotn_t;
		typedef typename _Traits::bitmap_type	bitmap_type;
		typedef pair<_Leaf*, slotn_t>			_IterDef;

		template <typename Node>
		union _Ref
		{
			offset_type	offset; // offset of reffered node
			Node*		ptr;	// pointer to node

			operator bool () const { return offset != 0; } // valid?
		};

		typedef _Ref<_Node> _NodeRef;
		typedef _Ref<_Leaf> _LeafRef;

		/// Base node
		struct _Node
		{
			enum E
			{
				slot_count		= _Traits::slot_count,
				slot_mid		= ( slot_count + 1) / 2,
				extra			= slot_count % 2,
				min_slots		= slot_count / 2,
				storage_size	= sizeof( slotn_t) + slot_count * stream_type::key_storage_size
			};

			size_t				level;				//< Level in the b-tree, if level == 0 -> leaf node
			slotn_t				used_slots;			//< Number of key slotuse use, so number of valid children or data pointers
			mutable bitmap_type	key_changes_bmp;
			key_type			keys[ slot_count];
			offset_type			offset;
			_Inner*				parent;

			_Node( const offset_type offset = 0, _Inner* const parent = 0, const slotn_t level = 0):
				level( level),
				used_slots( 0),
				key_changes_bmp( ~0),
				offset( offset),
				parent( parent)
			{}

			~_Node()
			{
				if ( parent)
				{
					parent->unlink( this);
				}
			}

			bool is_leaf() const { return !level; }

			bool is_full() const { return used_slots == slot_count; }

			//bool is_few() const { return used_slots <= min_slots; }

			//bool is_underflow() const { return used_slots < min_slots; }

			slotn_t find_upper( const key_type& key) const
			{
				if ( !used_slots) return 0;
				const key_type* const p = std::upper_bound( keys, keys + used_slots, key);
				return p - keys;
			}

			slotn_t find_lower( const key_type& key) const
			{
				if ( !used_slots) return 0;
				const key_type* const p = std::lower_bound( keys, keys + used_slots, key);
				return p - keys;
			}

			void is_key_changed( const slotn_t index) const
			{
				return key_changes_bmp & ( Bitmap( 1) << index);
			}

			void mark_key_changed( const slotn_t index)
			{
				key_changes_bmp |= Bitmap( 1) << index;
			}

		protected:
			bool raw_load_from( stream_type& input)
			{
				input.read( &used_slots, sizeof( used_slots));
				input.read_keys( keys, used_slots, slot_count, key_changes_bmp);
				return input.ok();
			}

			bool raw_save_to( stream_type& out) const
			{
				out.write( &used_slots, sizeof( used_slots));
				out.write_keys( keys, used_slots, slot_count, key_changes_bmp);
				return out.ok();
			}

			template <const size_t additional_data, typename _Val>
			static _Val& split_( slotn_t& key_pos, slotn_t& dest_used_slots, slotn_t& src_used_slots,
						key_type* const dest_keys, _Val* const dest_values,
						key_type* const src_keys,  _Val* const src_values,
						const key_type& key)
			{
				_Val* result;
				if ( key_pos < slot_mid)
				{
					std::move( src_keys + slot_mid - extra + additional_data, src_keys + slot_count, dest_keys);
					std::rotate( src_keys + key_pos, src_keys + slot_mid - 1, src_keys + slot_mid);

					src_keys[ key_pos] = key;
					result = src_values + key_pos;

					std::move( src_values + slot_mid - extra + additional_data, src_values + slot_count + additional_data, dest_values);
					std::rotate( src_values + key_pos, src_values + slot_mid - 1 + additional_data, src_values + slot_mid + additional_data);
				}
				else
				{
					const size_t new_key_pos = key_pos - slot_mid - !extra;
					if ( new_key_pos)
					{
						std::move( src_keys   + slot_mid + !extra + additional_data, src_keys   + key_pos, dest_keys);
						std::move( src_values + slot_mid + !extra + additional_data, src_values + key_pos + additional_data, dest_values);
					}

					dest_keys[ new_key_pos - additional_data] = key;
					result = dest_values + new_key_pos;

					std::move( src_keys   + key_pos, src_keys   + slot_count, dest_keys   + new_key_pos + 1);
					std::move( src_values + key_pos, src_values + slot_count + additional_data, dest_values + new_key_pos + 1);

					key_pos = new_key_pos;
				}

				dest_used_slots = slot_mid - additional_data;
				src_used_slots = slot_count - slot_mid + extra;
				return *result;
			}

			template <const size_t additional_data, typename _Val>
			void insert_( const key_type& key, const slotn_t pos, _Val* const values)
			{
				if ( pos < used_slots)
				{
					key_type* const keys_ptr = keys + used_slots;
					_Val* const values_ptr = values + used_slots + additional_data;

					std::rotate( keys + pos, keys_ptr, keys_ptr + 1);
					std::rotate( values + pos, values_ptr, values_ptr + 1);
				}
				++used_slots;
				BP_TREE_ASSERT( used_slots <= traits::slot_count);
				BP_TREE_ASSERT( pos < used_slots);
				keys[ pos] = key;
			}

			template <typename _Val>
			void erase_( const key_type& key, _Val* const values)
			{
				return erase( key, find_lower( key), values);
			}

			template <const size_t additional_data, typename _Val>
			void erase_( const key_type& key, const slotn_t pos, _Val* const values)
			{
				if ( pos < used_slots)
				{
					key_type* const dest_keys = keys + pos;
					_Val* const dest_values = values + pos;
					std::move( dest_keys + 1, dest_keys + count, dest_keys);
					std::move( dest_values + 1, dest_values + count + additional_data, dest_values);
					--used_slots;
				}
			}

			template <typename T>
			static bool save_( T* const node, stream_type& out)
			{
				bool ok;
				if ( node->is_changed())
				{
					out.seek( node->offset);
					ok = node->raw_save_to( out);
				}
				else
				{
					ok = true;
				}
				return ok;
			}
		};

		// inner node
		struct _Inner: public _Node
		{
			bitmap_type	children_ptr_bmp;
			_NodeRef	children[ slot_count + 1];

			_Inner( const offset_type offset = 0, _Inner* const parent = 0, const slotn_t level = 0):
				_Node( offset, parent, level),
				children_ptr_bmp( 0)
			{}

			~_Inner()
			{
				if ( children_ptr_bmp)
				{
					bitmap_type mask = 1;
					for( slotn_t i = 0; i < used_slots + 1; ++i, mask <<= 1)
					{
						if ( children_ptr_bmp & mask)
						{
							children[ i].ptr->parent = 0;
						}
					}
				}
			}

			bool is_changed() const { return key_changes_bmp != 0; }

			enum E { storage_size = _Node::storage_size + ( slot_count + 1) * sizeof( offset_type) };

			bitmap_type is_ptr_at( const slotn_t index) const { return children_ptr_bmp & ( bitmap_type( 1) << index); }

			void link( const slotn_t index, _Node* const node)
			{
				node->parent = this;
				children_ptr_bmp |= bitmap_type( 1) << index;
				children[ index].ptr = node;
			}

			//void unlink( const slotn_t index, const offset_type offset)
			//{
			//	children_ptr_bmp &= ~( bitmap_type( 1) << index);
			//	children[ index].offset = offset;
			//}

			void unlink( _Node* const node)
			{
				_NodeRef* const end = children + used_slots + 1;
				bitmap_type mask = 1;
				for( _NodeRef* child = children; child < end; ++child, mask <<= 1)			
				{
					if ( child->ptr == node)
					{
						BP_TREE_ASSERT( children_ptr_bmp & mask);
						children_ptr_bmp &= ~mask;
						child->offset = node->offset;
						break;
					}
				}
			}

			bool load_from( stream_type& input)
			{
				input.seek( offset);
				return raw_load_from( input);
			}

			bool save_to( stream_type& out) const
			{
				return _Node::save_( this, out);
			}

			bool raw_load_from( stream_type& input)
			{
				//input.read( &level, sizeof( level));
				_Node::raw_load_from( input);
				input.read_offsets( (offset_type*) children, used_slots + 1, slot_count + 1);
				if ( input.ok())
				{
					key_changes_bmp = 0;
					children_ptr_bmp = 0;
				}
				return input.ok();
			}

			size_t actual_storage_size() const 
			{ 
				return storage_size - ( slot_count - used_slots) * ( stream_type::key_storage_size + sizeof( offset_type));
			}

			offset_type child_offset( const bitmap_type flag, const slotn_t index) const
			{
				return ( children_ptr_bmp & flag) ? children[ index].ptr->offset : children[ index].offset;
			}

			bool raw_save_to( stream_type& out) const
			{
				// used_slots
				// keys
				// children's offsets
				if ( _Node::raw_save_to( out))
				{
					{
						slotn_t index = 0;
						bitmap_type flag = 1;
						offset_type offset;
						do
						{
							if ( key_changes_bmp & flag)
							{
								offset = child_offset( flag, index);
								out.write( &offset, sizeof( offset_type));
							}
							else
							{
								out.skip( sizeof( offset_type));
							}
							++index;
							flag <<= 1;
						}
						while( index < used_slots + 1 && out.ok());
					}

					out.skip( sizeof( offset_type) * ( slot_count - used_slots));

					if ( out.ok())
					{
						key_changes_bmp = 0;
					}
					return out.ok();
				}
				return false;
			}

			void insert( const key_type& key, _Node* const node)
			{
				const slotn_t pos = find_lower( key);
				insert_<1>( key, pos, children + 1);
				children_ptr_bmp = bit_insert( children_ptr_bmp, pos);
				link( pos + 1, node);
			}

			static bitmap_type bit_insert( bitmap_type bits, const slotn_t key_pos)
			{
				const bitmap_type mask = bitmap_type( ~0) << key_pos;
				return ( ( bits & mask) << 1) | ( bitmap_type( 1) << key_pos) | ( bits & ~mask);
			}

			void split( key_type& key_for_parent, _Inner& new_inner, const key_type& key, _Node* const new_child)
			{
				slotn_t orig_key_pos;
				slotn_t key_pos = orig_key_pos = find_lower( key);
				BP_TREE_ASSERT( key_pos <= slot_count);
				split_<1, _NodeRef>( key_pos, new_inner.used_slots, used_slots, new_inner.keys, new_inner.children, keys, children, key);

				BP_TREE_ASSERT( used_slots < slot_count);
				BP_TREE_ASSERT( new_inner.used_slots < slot_count);

				bitmap_type bits = children_ptr_bmp;
				if ( key_pos != orig_key_pos)
				{
					bits >>= slot_mid + extra;
				}

				new_inner.children_ptr_bmp = bit_insert( bits, key_pos);
				new_inner.children[ key_pos].ptr = new_child;
				new_inner.set_as_parent();

				key_for_parent = keys[ slot_mid];
			}

			void set_as_parent()
			{
				bitmap_type mask = 1;
				for( slotn_t i = 0; i < used_slots + 1; ++i, mask <<= 1)
				{
					if ( children_ptr_bmp & mask)
					{
						children[ i].ptr->parent = this;
					}
				}
			}

			void clear()
			{
				children_ptr_bmp = 0;
			}
		};

		// leaf node
		struct _Leaf: public _Node
		{
			_LeafRef			siblings[ 2]; // next, prev
			mutable bitmap_type siblings_changes_bmp;
			mutable bitmap_type siblings_ptr_bmp;
			mutable bitmap_type data_changes_bmp;
			value_type			data[ slot_count];

			enum E
			{
				sibling_next = 0,
				sibling_prev = 1,
				sibling_mask_next = 1,
				sibling_mask_prev = 2,
				storage_size = traits::leaf_marker_size + _Node::storage_size + 2 * sizeof( offset_type) + slot_count * stream_type::value_storage_size
			};

			_Leaf( const offset_type offset = 0, _Inner* const parent = 0):
				_Node( offset, parent),
				siblings_changes_bmp( ~0),
				siblings_ptr_bmp( 0),
				data_changes_bmp( ~0)
			{
				memset( siblings, 0, sizeof( siblings));
			}

			~_Leaf()
			{
				if ( siblings_ptr_bmp & sibling_mask_next)
				{
					siblings[ sibling_next].ptr->unlink_sibling( sibling_prev, offset);
				}

				if ( siblings_ptr_bmp & sibling_mask_prev)
				{
					siblings[ sibling_prev].ptr->unlink_sibling( sibling_next, offset);
				}
			}

			bool is_changed() const { return ( key_changes_bmp | data_changes_bmp | siblings_changes_bmp) != 0; }

			bool load_from( stream_type& input)
			{
				input.seek( offset);
				return raw_load_from( input);
			}

			bool save_to( stream_type& out) const
			{
				return _Node::save_( this, out);
			}

			bool raw_load_from( stream_type& input)
			{
				input.skip( traits::leaf_marker_size);
				if ( _Node::raw_load_from( input))
				{
					input.read_offsets( (offset_type*) siblings, 2);
					input.read_data( data, used_slots, slot_count, data_changes_bmp);
					BP_TREE_ASSERT( used_slots);
					if ( input.ok())
					{
						siblings_ptr_bmp = 0;
						clear_change_flags();
					}
					return input.ok();
				}
				return false;
			}

			bool raw_save_to( stream_type& out) const
			{
				out.write( traits::leaf_marker(), traits::leaf_marker_size);
				_Node::raw_save_to( out);
				save_sibling( out, sibling_next);
				save_sibling( out, sibling_prev);
				out.write_data( data, used_slots, slot_count, data_changes_bmp);
				if ( out.ok())
				{
					clear_change_flags();
				}
				return out.ok();
			}

			size_t actual_storage_size() const 
			{ 
				return storage_size - ( slot_count - used_slots) * ( stream_type::key_storage_size + stream_type::value_storage_size);
			}

			value_type& insert( const key_type& key)
			{
				return insert<0>( key, data);
			}

			value_type& insert( const key_type& key, const slotn_t pos)
			{
				insert_<0>( key, pos, data);
				return data[ pos];
			}

			void erase( const key_type& key)
			{
				erase<0>( key);
			}

			value_type& split( _IterDef& def, key_type& key_for_parent, _Leaf& new_leaf, const key_type& key)
			{
				const slotn_t orig_key_pos = find_lower( key);
				slotn_t key_pos = orig_key_pos;
				value_type& res = split_<0, value_type>( key_pos, new_leaf.used_slots, used_slots, new_leaf.keys, new_leaf.data, keys, data, key);
				def.first  = key_pos == orig_key_pos ? this : &new_leaf;
				def.second = key_pos;
				key_for_parent = new_leaf.keys[ 0];
				return res;
			}

			size_t is_sibling_ptr_at( const slotn_t index) const { return siblings_ptr_bmp & ( bitmap_type( 1) << index); }

			void link_sibling( _Leaf* const node, const int index)
			{
				siblings_ptr_bmp |= bitmap_type( 1) << index;
				siblings[ index].ptr = node;
			}

			void unlink_sibling( const slotn_t index, const offset_type offset)
			{
				siblings_ptr_bmp &= ~( bitmap_type( 1) << index);
				siblings[ index].offset = offset;
			}

			void clear()
			{
				siblings_ptr_bmp = 0;
			}

		protected:
			void save_sibling( stream_type& out, const int index) const
			{
				const bitmap_type mask = bitmap_type( 1) << index; 
				if ( siblings_changes_bmp & mask)
				{
					const offset_type offset = ( siblings_ptr_bmp & mask) ? siblings[ index].ptr->offset : siblings[ index].offset;
					out.write( &offset, sizeof( offset_type));
				}
				else
				{
					out.skip( sizeof( offset_type));
				}
			}

			void clear_change_flags() const
			{
				if ( siblings_changes_bmp) siblings_changes_bmp = 0;
				if ( key_changes_bmp) key_changes_bmp = 0;
				if ( data_changes_bmp) data_changes_bmp = 0;
			}
		};

		static void link_siblings( _Leaf* const a, _Leaf* const b)
		{
			BP_TREE_ASSERT( a && b);
			a->link_sibling( b, 0);
			b->link_sibling( a, 1);
		}

		struct _NodeManager
		{
			typedef typename _Alloc::rebind<_Inner>::other	_InnerAllocator;
			typedef typename _Alloc::rebind<_Leaf>::other	_LeafAllocator;

			_Stream*		stream;
			_Inner			inner_node;
			_Leaf			leaf_node;
			_InnerAllocator	inner_allocator;
			_LeafAllocator	leaf_allocator;

			_NodeManager(): stream( 0) {}
			_NodeManager( const _InnerAllocator& inner_alloc, const _LeafAllocator& leaf_alloc):
				inner_allocator( inner_alloc),
				leaf_allocator( leaf_alloc)
			{}

			~_NodeManager()
			{
				inner_node.parent = 0;
				leaf_node.parent = 0;
			}

			_Leaf* allocate_leaf( const offset_type offset = 0, _Inner* const parent = 0)
			{
				_Leaf* p = static_cast<_Leaf*>( leaf_allocator.allocate( sizeof( _Leaf)));
				if ( p)
				{
					leaf_node.offset = offset;
					leaf_node.parent = parent;
					leaf_allocator.construct( p, leaf_node);
				}
				return p;
			}

			_Inner* allocate_inner( const offset_type offset, _Inner* const parent = 0, const slotn_t level = 0)
			{				
				_Inner* p = static_cast<_Inner*>( inner_allocator.allocate( sizeof( _Inner)));
				if ( p)
				{
					inner_node.offset = offset;
					inner_node.parent = parent;
					inner_node.level  = level;
					inner_allocator.construct( p, inner_node);
				}
				return p;
			}

			// called on cache's eviction
			void operator () ( _Node* const node)
			{
				if ( node->is_leaf())
				{
					if ( stream)
					{
						static_cast<_Leaf*>( node)->save_to( *stream);
					}
					leaf_allocator.destroy( static_cast<_Leaf*>( node));
					leaf_allocator.deallocate( static_cast<_Leaf*>( node), sizeof( _Leaf));
				}
				else
				{
					if ( stream)
					{
						static_cast<_Inner*>( node)->save_to( *stream);
					}
					inner_allocator.destroy( static_cast<_Inner*>( node));
					inner_allocator.deallocate( static_cast<_Inner*>( node), sizeof( _Inner));
				}
			}
		};


		template <typename Container>
		void compact_analyse_descend( Container& map, _Inner* const node)
		{
			map[ node->offset] = node->actual_storage_size();
			if ( node->level != 1)
			{
				for( slotn_t i = 0; i < node->used_slots + 1; ++i)
				{
					compact_analyse_descend( map, (_Inner*) get_child( node, i));
				}
			}
			else
			{
				for( slotn_t i = 0; i <  node->used_slots + 1; ++i)
				{
					const _Leaf* const leaf = (_Leaf*) get_child( node, i);
					map[ leaf->offset] = leaf->actual_storage_size();
				}
			}
		};

		template <typename Container>
		void compact_write_descend( _Inner* node, stream_type& out, Container& map, _Inner& inner, _Leaf& leaf)
		{
			std::copy( node->keys, node->keys + node->used_slots, inner.keys);
			inner.used_slots = node->used_slots;
			inner.key_changes_bmp = bitmap_type( ~0);
			bitmap_type flag = 1;
			for( slotn_t i = 0; i < node->used_slots + 1; ++i, flag <<= 1)
			{
				inner.children[ i].offset = map[ node->child_offset( flag, i)].new_offset;
				BP_TREE_ASSERT( inner.children[ i].offset);
			}
			out.seek( map[ node->offset].new_offset);
			inner.raw_save_to( out);

			if ( node->level != 1)
			{
				for( slotn_t i = 0; i < node->used_slots + 1; ++i)
				{
					compact_write_descend( (_Inner*) get_child( node, i), out, map, inner, leaf);
				}
			}
			else
			{
				bitmap_type tmp;
				_Leaf* leafSrc;
				for( slotn_t i = 0; i < node->used_slots + 1; ++i)
				{
					leafSrc = (_Leaf*) get_child( node, i);

					leaf.used_slots = leafSrc->used_slots;
					leaf.offset = map[ leafSrc->offset].new_offset;

					std::copy( leafSrc->keys, leafSrc->keys + leafSrc->used_slots, leaf.keys);
					std::copy( leafSrc->data, leafSrc->data + leafSrc->used_slots, leaf.data);

					_LeafRef& nextRef = leafSrc->siblings[ _Leaf::sibling_next];
					_LeafRef& prevRef = leafSrc->siblings[ _Leaf::sibling_prev];
					
					leaf.siblings[ _Leaf::sibling_next].offset = nextRef ? map[ leafSrc->is_sibling_ptr_at( _Leaf::sibling_next) ? nextRef.ptr->offset : nextRef.offset].new_offset : 0;
					leaf.siblings[ _Leaf::sibling_prev].offset = prevRef ? map[ leafSrc->is_sibling_ptr_at( _Leaf::sibling_prev) ? prevRef.ptr->offset : prevRef.offset].new_offset : 0;

					out.seek( map[ leafSrc->offset].new_offset);
					leaf.key_changes_bmp = bitmap_type( ~0);
					leaf.siblings_changes_bmp = bitmap_type( ~0);
					leaf.raw_save_to( out);
				}
			}
		};

		stream_type& get_stream() const
		{ 
			BP_TREE_ASSERT( nodeman_.stream);
			return *nodeman_.stream; 
		}

		_IterDef find_( const key_type& key) const
		{
			_Node* node = root_;
			slotn_t pos;
			if ( root_)
			{
				while( !node->is_leaf())
				{
					node = get_child_by_key( static_cast<_Inner*>( node), key);
				}
				_Leaf* const lf = (_Leaf*) node;
				pos = node->find_lower( key);
				if ( pos == node->used_slots)
				{
					pos = 0;
					node = 0;
				}
			}
			else
			{
				pos = 0;
			}

			return _IterDef( static_cast<_Leaf*>( node), pos);
		}

		void link_possible_siblings( _Leaf* const node) const
		{
			{
				const offset_type off = node->siblings[ _Leaf::sibling_next].offset;
				if ( off == tail_->offset)
				{
					link_siblings( node, tail_);
				}
				else
				{
					_Cache::iterator sibling = cache_.find( off, false);
					if ( sibling != cache_.end())
					{
						link_siblings( node, static_cast<_Leaf*>( *sibling));
					}
				}
			}

			const offset_type off = node->siblings[ _Leaf::sibling_prev].offset;
			if ( off == head_->offset)
			{
				link_siblings( head_, node);
			}
			else
			{
				_Cache::iterator sibling = cache_.find( off, false);
				if ( sibling != cache_.end())
				{
					link_siblings( static_cast<_Leaf*>( *sibling), node);
				}
			}
		}

		_Node* get_child( _Inner* const node, const slotn_t pos) const
		{
			_Node* child;
			if ( node != root_)
			{
				cache_.touch( node->offset);
			}

			if ( node->is_ptr_at( pos))
			{
				child = node->children[ pos].ptr;
				if ( child->offset != head_->offset && child->offset != tail_->offset)
				{
					cache_.touch( child->offset);
				}
			}
			else
			{
				const offset_type offset = node->children[ pos].offset;
				BP_TREE_ASSERT( offset && offset < eof_);
				if ( node->level != 1)
				{
					_Inner* const item = nodeman_.allocate_inner( offset, node, node->level - 1);
					item->load_from( get_stream());
					cache_new_node( child = item);
				}
				else if ( offset == head_->offset)
				{
					child = head_;
				}
				else if ( offset == tail_->offset)
				{
					child = tail_;
				}
				else
				{
					_Leaf* const item = nodeman_.allocate_leaf( offset, node);
					item->load_from( get_stream());
					link_possible_siblings( item);
					cache_new_node( child = item);
				}

				node->link( pos, child);
			}

			return child;
		}

		_Node* get_child_by_key( _Inner* const node, const key_type& key) const
		{
			const slotn_t pos = node->find_lower( key);
			return get_child( node, slotn_t( pos + ( node->keys[ pos] == key)));
		}

		_Leaf* get_sibling( _Leaf* const node, const slotn_t index)
		{
			cache_.touch( node->offset);
			if ( !node->siblings[ index])
			{
				return 0;
			}

			if ( node->is_sibling_ptr_at( index))
			{
				_Leaf* const leaf = static_cast<_Leaf*>( node->siblings[ index].ptr);
				cache_.touch( leaf->offset);
				return leaf;
			}

			const offset_type offset = node->siblings[ index].offset;
			BP_TREE_ASSERT( offset && offset < eof_);

			bool cache_now = true;
			_Leaf* item;
			if ( offset == head_->offset)
			{
				cache_now = false;
				item = static_cast<_Leaf*>( head_);
				item->link_sibling( node, !index);
			}
			else if ( offset == tail_->offset)
			{
				cache_now = false;
				item = static_cast<_Leaf*>( tail_);
				item->link_sibling( node, !index);
			}
			else
			{
				item = nodeman_.allocate_leaf( offset);
				item->load_from( get_stream());
				link_possible_siblings( item);
			}

			node->link_sibling( item, index);
			if ( cache_now)
			{
				std::pair<_Cache::iterator, bool> res = cache_.get( offset);
				*res.first = item;
			}

			return item;
		}

		void insert_descend( _IterDef& def, key_type& splitkey, _Node*& splitnode, _Node* const node_item, const key_type& key)
		{
			const slotn_t slot = node_item->find_lower( key);
			if ( !node_item->is_leaf()) // Inner -----------------------------------------------
			{
				_Inner* const node = static_cast<_Inner*>( node_item);
				key_type new_key;
				_Node* new_child = 0;
				insert_descend( def, new_key, new_child, get_child( node, slot), key);
				if ( new_child)
				{
					if ( node->is_full())
					{
						_Inner* const new_node = nodeman_.allocate_inner( eof_, node->parent, node->level);
						eof_ += _Inner::storage_size;
						static_cast<_Inner*>( node)->split( splitkey, *new_node, new_key, new_child);
						BP_TREE_ASSERT( splitkey > 0);
						cache_new_node( splitnode = new_node);
					}
					else
					{
						node->insert( new_key, new_child);
						//splitnode = 0;
					}
				}
			}
			else // Leaf -----------------------------------------------------------------------
			{
				_Leaf* const node = static_cast<_Leaf*>( node_item);
				if ( node->is_full())
				{
					_Leaf* const new_node = nodeman_.allocate_leaf( eof_);
					eof_ += _Leaf::storage_size;

					_Leaf* const next_node = get_sibling( node, _Leaf::sibling_next);
					node->split( def, splitkey, *new_node, key);
					BP_TREE_ASSERT( splitkey > 0);
					if ( next_node)
					{
						link_siblings( new_node, next_node);
						next_node->siblings_changes_bmp |= _Leaf::sibling_mask_prev;
					}

					link_siblings( node, new_node);
					node->siblings_changes_bmp |= _Leaf::sibling_mask_next;

					splitnode = new_node;
					if ( tail_ == node)
					{
						tail_ = new_node;
						if ( node != head_)
						{
							cache_new_node( node);
						}
					}
				}
				else
				{
					//splitnode = 0;
					node->insert( key, slot);
					def.first = node;
					def.second = slot;
				}
			}
		}

		class base_iterator
		{
			friend bp_tree;

		protected:
			bp_tree*	tree;
			_Leaf*		node;
			slotn_t		index;

			base_iterator( bp_tree* const tree, _Leaf* const node, const slotn_t index): tree( tree), node( node), index( index) {}
			base_iterator( const base_iterator& item): tree( item.tree), node( item.node), index( item.index) {}

			void to_next()
			{
				if ( node)
				{
					if ( index + 1 == node->used_slots)
					{
						index = 0;
						node = tree->get_sibling( node, _Leaf::sibling_next);
					}
					else
					{
						++index;
					}
				}
			}

			void to_prev()
			{
				if ( node)
				{
					if ( !index)
					{
						node = tree->get_sibling( node, _Leaf::sibling_prev);
						index = node ? node->used_slots - 1 : 0;
					}
					else
					{
						--index;
					}
				}
			}


		public:
			base_iterator(): tree( 0), node( 0), index( 0) {}

			const key_type& key() const { return node->keys[ index]; }
			const value_type& value() const { return node->data[ index]; }

			const value_type& operator * () const { return node->data[ index]; }
			const value_type* operator -> () const { return &node->data[ index]; }

			bool operator == ( const base_iterator& x) const
			{
				return node == x.node && index == x.index;
			}

			bool operator != ( const base_iterator& x) const
			{
				return node != x.node || index != x.index;
			}

			operator bool () const { return node != 0; }
		};

		bool is_locked_( _Node* const node) const
		{
			return cache_.is_locked( cache_.find( node->offset, false));
		}

		void lock_( _Node* const node) const
		{
			cache_.lock( cache_.find( node->offset, false));
		}

		void unlock_( _Node* const node) const
		{
			cache_.unlock( cache_.find( node->offset, false));
		}

		static std::ostream& pad( std::ostream& out, const int padding)
		{
			for( int i = 0; i < padding; ++i)
			{
				out << ' ';
			}
			return out;
		}

		static void print_keys( std::ostream& out, const _Node* const node, const int padding)
		{
			pad( out, padding) << "Keys ";
			for( slotn_t i = 0; i < node->used_slots; ++i)
			{
				out << node->keys[ i] << '\t';
			}
			out << '\n';
		}

		void print_( std::ostream& out, _Node* const item, int padding)
		{
			BP_TREE_ASSERT( item->used_slots && item->used_slots <= _Node::slot_count);
			pad( out, padding) << "Offset " << item->offset << '\n';
			if ( !item->is_leaf())
			{
				_Inner* const node = static_cast<_Inner*>( item);
				pad( out, padding) << "Level " << node->level << '\n';
				print_keys( out, node, padding);
				padding += 4;
				lock_( node);
				for( slotn_t i = 0; i < node->used_slots + 1; ++i)
				{
					print_( out, get_child( node, i), padding);
					BP_TREE_ASSERT( node->used_slots <= _Node::slot_count);
					out << '\n';
				}
				unlock_( node);
			}
			else
			{
				_Leaf* const node = static_cast<_Leaf*>( item);
				print_keys( out, node, padding);
				out << '\n';
			}
		}

	public:
		typedef typename _NodeManager::_InnerAllocator	inner_allocator_type;
		typedef typename _NodeManager::_LeafAllocator	leaf_allocator_type;

	protected:
		typedef lru_cache<offset_type, _Node*, typename bp_tree::_NodeManager> _Cache;
		typedef std::pair<typename _Cache::iterator, bool> _GetResult;

		void cache_node( _Node* const node) const
		{
			if ( node != head_ && node != tail_)
			{
				cache_new_node( node);
			}
		}

		void cache_new_node( _Node* const node) const
		{
			_GetResult pos = cache_.get( node->offset);
			if ( !pos.second)
			{
				*pos.first = node;
			}
		}

		enum E 
		{ 
			count_mask	= 1,
			root_mask	= 2,
			head_mask	= 4,
			tail_mask	= 8,

			count_offset		= traits::signature_size,
			flag_offset			= count_offset + sizeof( size_t),
			root_level_offset	= flag_offset + 1,
			root_offset			= root_level_offset + sizeof( slotn_t),
			head_offset			= root_offset + sizeof( offset_type),
			tail_offset			= head_offset + sizeof( offset_type),
			items_offset		= tail_offset + sizeof( offset_type)
		};

		_Node*					root_;
		_Leaf*					head_;
		_Leaf*					tail_;
		offset_type				eof_;
		size_t					item_count_;
		mutable bitmap_type		change_flags_;
		mutable _Cache			cache_;
		mutable _NodeManager	nodeman_;

	public:
		bp_tree( const size_t cache_size):
			root_( 0),
			head_( 0),
			tail_( 0),
			eof_( 0),
			item_count_( 0),
			change_flags_( ~0),
			cache_( cache_size)
		{
			cache_.set_observer( &nodeman_);
		}

		bp_tree( const size_t cache_size, const inner_allocator_type& inner_allocator, const leaf_allocator_type& leaf_allocator):
			root_( 0),
			head_( 0),
			tail_( 0),
			eof_( 0),
			item_count_( 0),
			change_flags_( ~0),
			cache_( cache_size),
			nodeman_( inner_allocator, leaf_allocator)
		{
			cache_.set_observer( &nodeman_);
		}

		~bp_tree()
		{
			cache_.clear();
			_Stream* const stream = nodeman_.stream;
			if ( stream)
			{
				if ( change_flags_ & count_mask)
				{
					stream->seek( count_offset);
					stream->write( &item_count_, sizeof( item_count_));
				}

				if ( item_count_)
				{
					BP_TREE_ASSERT( root_);
					if ( change_flags_ & root_mask)
					{
						stream->seek( root_level_offset);
						stream->write( &root_->level, sizeof( slotn_t));

						stream->seek( root_offset);
						stream->write( &root_->offset, sizeof( offset_type));
					}

					if ( !root_->is_leaf())
					{
						BP_TREE_ASSERT( head_);
						if ( change_flags_ & head_mask)
						{
							stream->seek( head_offset);
							stream->write( &head_->offset, sizeof( offset_type));
						}

						BP_TREE_ASSERT( tail_);
						if ( change_flags_ & tail_mask)
						{
							stream->seek( tail_offset);
							stream->write( &tail_->offset, sizeof( offset_type));
						}

						BP_TREE_ASSERT( head_ != root_);
						static_cast<_Inner*>( root_)->save_to( *stream);
						head_->save_to( *stream);
						tail_->save_to( *stream);
						delete head_;
						delete tail_;
					}
					else
					{
						static_cast<_Leaf*>( root_)->save_to( *stream);
					}
					delete root_;
				}
			}
		}

		// signature
		// item count
		// root level
		// root offset
		// head offset
		// tail offset
		bool open( stream_type& io, const offset_type end_off = 0)
		{
			bool ok;
			eof_ = end_off;
			if ( eof_)
			{ // existing file
				char sign[ traits::signature_size];
				io.read( sign, traits::signature_size);
				if ( !memcmp( traits::signature(), sign, traits::signature_size))
				{
					io.read( &item_count_, sizeof( item_count_));
					if ( item_count_)
					{
						char flags;
						io.read( &flags, 1);
						io.set_compact( flags & 1);

						slotn_t root_level;
						io.read( &root_level, sizeof( slotn_t));

						offset_type root_off;
						io.read( &root_off, sizeof( offset_type));
						BP_TREE_ASSERT( root_off && root_off < end_off);

						ok = io.ok();
						if ( ok)
						{
							change_flags_ = 0;
							if ( item_count_ > traits::slot_count)
							{
								offset_type head_off, tail_off;

								io.read( &head_off, sizeof( offset_type));
								BP_TREE_ASSERT( head_off && head_off < end_off);

								io.read( &tail_off, sizeof( offset_type));
								BP_TREE_ASSERT( tail_off && tail_off < end_off);

								ok = io.ok();
								if ( ok)
								{
									_Inner* const node = nodeman_.allocate_inner( root_off, 0, root_level);
									node->load_from( io);
									root_ = node;

									head_ = nodeman_.allocate_leaf( head_off);
									head_->load_from( io);

									tail_ = nodeman_.allocate_leaf( tail_off);
									tail_->load_from( io);
								}
							}
							else
							{
								_Leaf* const node = nodeman_.allocate_leaf( root_off);
								node->load_from( io);
								root_ = head_ = tail_ = node;
							}
						}
					}
					else
					{
						ok = true;
					}
				}
				else
				{
					ok = false;
				}
			}
			else
			{ // new file
				io.write( traits::signature(), traits::signature_size);
				item_count_ = 0;
				io.write( &item_count_, sizeof item_count_);
				io.write( &item_count_, 1);
				eof_ = items_offset;
				ok = io.ok();
			}
			nodeman_.stream = ok ? &io : 0;
			return ok;
		}

		class const_iterator: public base_iterator
		{
			friend bp_tree;

			const_iterator( bp_tree* const tree, _Leaf* const node, const slotn_t index): base_iterator( tree, node, index) {}
		public:
			typedef const_iterator _Self;

			const_iterator() {}
			const_iterator( const base_iterator& item): base_iterator( item) {}

			_Self& operator ++ ()
			{
				to_next();
				return *this;
			}

			_Self& operator ++ (int)
			{
				_Self tmp = *this;
				to_next();
				return tmp;
			}

			_Self& operator -- ()
			{
				to_prev();
				return *this;
			}

			_Self& operator -- (int)
			{
				_Self tmp = *this;
				to_prev();
				return tmp;
			}
		};

		class iterator: public const_iterator
		{
			friend bp_tree;

			iterator( bp_tree* const tree, _Leaf* const node, const slotn_t index): const_iterator( tree, node, index) {}

		public:
			iterator() {}
			iterator( const base_iterator& item): const_iterator( item) {}

			value_type& operator * () { return node->data[ index]; }
			value_type* operator -> () { return &node->data[ index]; }
		};

		class const_reverse_iterator: public base_iterator
		{
			friend bp_tree;

			const_reverse_iterator( bp_tree* const tree, _Leaf* const node, const slotn_t index): base_iterator( tree, node, index) {}
		public:
			typedef const_reverse_iterator _Self;

			const_reverse_iterator() {}
			const_reverse_iterator( const base_iterator& item): base_iterator( item) {}

			_Self& operator ++ ()
			{
				to_prev();
				return *this;
			}

			_Self& operator ++ (int)
			{
				_Self tmp = *this;
				to_prev();
				return tmp;
			}

			_Self& operator -- ()
			{
				to_next();
				return *this;
			}

			_Self& operator -- (int)
			{
				_Self tmp = *this;
				to_next();
				return tmp;
			}
		};

		class reverse_iterator: public const_reverse_iterator
		{
			friend bp_tree;

			reverse_iterator( bp_tree* const tree, _Leaf* const node, const slotn_t index): const_reverse_iterator( tree, node, index) {}

		public:
			reverse_iterator() {}
			reverse_iterator( const base_iterator& item): const_reverse_iterator( item) {}

			value_type& operator * () { return node->data[ index]; }
			value_type* operator -> () { return &node->data[ index]; }
		};

		const_iterator begin() const
		{
			return const_iterator( this, head_, 0);
		}

		const_iterator end() const
		{
			return const_iterator( this, 0, 0);
		}

		iterator begin()
		{
			return iterator( this, head_, 0);
		}

		iterator end()
		{
			return iterator( this, 0, 0);
		}

		const_reverse_iterator rbegin() const
		{
			return const_reverse_iterator( this, tail_, tail_ ? tail_->used_slots - 1 : 0);
		}

		const_reverse_iterator rend() const
		{
			return const_reverse_iterator( this, 0, 0);
		}

		reverse_iterator rbegin()
		{
			return reverse_iterator( this, tail_, tail_ ? tail_->used_slots - 1 : 0);
		}

		reverse_iterator rend()
		{
			return reverse_iterator( this, 0, 0);
		}

		size_t size() const
		{
			return item_count_;
		}

		size_t depth() const
		{
			return root_ ? root_->level + 1: 0;
		}

		const_iterator find( const key_type& key) const
		{
			const _IterDef def = find_( key);
			return const_iterator( this, def.first, def.second);
		}

		const iterator find( const key_type& key)
		{
			const _IterDef def = find_( key);
			return iterator( this, def.first, def.second);
		}

		iterator insert( const key_type& key)
		{
			BP_TREE_ASSERT( !get_stream().is_compact());
			if ( root_)
			{
				key_type splitkey;
				_Node* splitnode = 0;
				_IterDef pos;
				insert_descend( pos, splitkey, splitnode, root_, key);
				if ( splitnode)
				{
					_Inner* const new_root = nodeman_.allocate_inner( eof_, 0, root_->level + 1);
					eof_ += _Inner::storage_size;

					new_root->keys[ 0] = splitkey;
					new_root->link( 0, root_);
					new_root->link( 1, splitnode);
					new_root->used_slots = 1;

					cache_node( root_);
					cache_node( splitnode);

					change_flags_ |= root_mask;
					root_ = new_root;
				}

				if ( pos.first)
				{
					++item_count_;
				}
				return iterator( this, pos.first, pos.second);
			}
			else
			{
				_Leaf* const leaf = nodeman_.allocate_leaf( eof_);
				eof_ += _Leaf::storage_size;

				leaf->insert( key, 0);
				root_ = head_ = tail_ = leaf;
				change_flags_ = ~0;

				if ( leaf)
				{
					item_count_ = 1;
				}
				return iterator( this, leaf, 0);
			}
		}

		void erase( const key_type& key)
		{
			if ( root_)
			{
				// not implemented
				BP_TREE_ASSERT( 0);
			}
		}

		void erase( const key_type& a, const key_type& b)
		{
			if ( root_)
			{
				// not implemented
				BP_TREE_ASSERT( 0);
			}
		}

		void clear()
		{
			if ( root_)
			{
				stream_type* tmp = nodeman_.stream;
				nodeman_.stream = 0;
				cache_.clear();
				if ( item_count_ > 1)
				{
					delete head_;
					delete tail_;
				}
				delete root_;
				item_count_ = 0;
				change_flags_ = count_mask /*| root_mask | head_mask | tail_mask*/;
				root_ = head_ = tail_ = 0;
				eof_ = items_offset;
				nodeman_.stream = tmp;
			}
		}

		void print( std::ostream& out)
		{
			if ( root_)
			{
				print_( out, root_, 0);
			}
		}

		bool compact_to( stream_type& out)
		{
			bool ok;
			if ( root_ && !root_->is_leaf())
			{
				struct _NodeInfo
				{
					size_t		storage_size;
					offset_type	new_offset;

					_NodeInfo() {}
					_NodeInfo( const size_t size): storage_size( size) {}
				};

				using namespace std;
				typedef map<offset_type, _NodeInfo> NodeInfoMap;
				NodeInfoMap nodeInfoMap;

				compact_analyse_descend( nodeInfoMap, (_Inner*) root_);
				size_t offset = items_offset;
				for( NodeInfoMap::iterator i = nodeInfoMap.begin(); i != nodeInfoMap.end(); ++i)
				{
					i->second.new_offset = offset;
					offset += i->second.storage_size;
				}

				out.write( traits::signature(), traits::signature_size);
				out.write( &item_count_, sizeof( item_count_));
				char flags = 1;
				out.write( &flags, 1);
				out.write( &root_->level, sizeof( slotn_t));
				out.write( &nodeInfoMap[ root_->offset].new_offset, sizeof( offset_type));
				out.write( &nodeInfoMap[ head_->offset].new_offset, sizeof( offset_type));
				out.write( &nodeInfoMap[ tail_->offset].new_offset, sizeof( offset_type));

				_Inner inner;
				_Leaf leaf;
				out.set_compact( true);
				compact_write_descend( (_Inner*) root_, out, nodeInfoMap, inner, leaf);
				inner.clear();
				ok = true;
			}
			else
			{
				ok = false;
			}
			return ok;
		}
	};
}
