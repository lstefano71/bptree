#pragma once
/// LRU cache
/// Copyright (c) Flaviu Cibu. All rights reserved.
/// Created 29-apr-‎2009
/// Updated 30-nov-‎2010

#include <functional>
#include <hash_map>
#include <set>

namespace stdext
{
	template <class HashMap>
	struct hm_accessor: HashMap::iterator, HashMap::_Mylist
	{
		static void assign( typename HashMap::iterator& dest, const typename HashMap::iterator& src)
		{
			dest = src;
			dest->second.backRef = (void*) src._Ptr;
		}

		static void set( typename HashMap::iterator& b, void* const backRef)	
		{ 
			b._Ptr = (_Node*) backRef;
		}
	};

	
	template <class _Cache>
	class lru_cache_statistics
	{
		friend _Cache;
		protected:
			size_t iRefs;
			size_t iMisses;

			void inc_refs()		{ ++iRefs; }
			void inc_misses()	{ ++iMisses; }

			lru_cache_statistics(): iRefs( 0), iMisses( 0) {}

		public:
			size_t refs()	const { return iRefs; }
			size_t misses() const { return iMisses; }

			void reset() { iRefs = iMisses = 0; }

			float hit_rate() const { return iRefs ? 1 - float( iMisses) / iRefs : 0; }
	};


	template <class _Cache>
	class lru_cache_dummy_statistics
	{
		friend _Cache;
		protected:
			void inc_refs()		{}
			void inc_misses()	{}

		public:
			size_t refs()	const { return 0; }
			size_t misses() const { return 0; }

			void reset() {}

			float hit_rate() const { return 0; }
	};


	struct lru_cache_dummy_eviction_observer
	{
		template <typename T>
		void operator () ( const T&) {}
	};

	template <typename Key, typename Data, typename EvictionObserver = lru_cache_dummy_eviction_observer, 
				template <typename> class Statistics = lru_cache_dummy_statistics,
				typename HashCompare = hash_compare<Key>, template <typename> class HashMapAccessor = hm_accessor>
	class lru_cache
	{
		lru_cache( const lru_cache&);
		lru_cache& operator = ( const lru_cache&);

		protected:
			struct Item;

			struct MruItem
			{
				Item*	prev;
				Item*	next;

				MruItem(): prev( 0), next( 0) {}

				void clear() { next = prev = (Item*) this; }

				void unlink()
				{
					if ( next) next->prev = prev;
					if ( prev) prev->next = next;
				}

				void append( Item* item)
				{
					if ( next) next->prev = item;
					item->prev = (Item*) this;
					item->next = next;
					next = item;
				}
			};

			struct Item: public MruItem
			{
				void*	backRef;
				Data	data;
			};

			typedef hash_map<Key, Item, HashCompare> HashMap;
			typedef typename HashMap::iterator HmIterator;

			template <const bool reverse>
			struct Cursor
			{
				static void next( HmIterator& i) { ++i; }
				static void prev( HmIterator& i) { --i; }
			};

			template <>
			struct Cursor<true>
			{
				static void next( HmIterator& i) { --i; }
				static void prev( HmIterator& i) { ++i; }
			};

			template <const bool reverse>
			struct MruCursor
			{
				static void next( Item*& p) { if ( p) p = p->next; }
				static void prev( Item*& p) { if ( p) p = p->prev; }
			};

			template <>
			struct MruCursor<true>
			{
				static void next( Item*& p) { if ( p) p = p->prev; }
				static void prev( Item*& p) { if ( p) p = p->next; }
			};


		public:
			typedef Key					key_type;
			typedef Data				value_type;
			typedef EvictionObserver	eviction_observer_type;
			typedef Statistics<lru_cache>	statistics_type;

			template <const bool reverse>
			class const_iterator_base
			{
				protected:
					friend class lru_cache;
					typedef Cursor<reverse> C;
					HmIterator iter;
					const_iterator_base( HmIterator it): iter( it) {}

				public:
					typedef std::bidirectional_iterator_tag iterator_category;
					typedef Data				value_type;
					typedef ptrdiff_t			difference_type;
					typedef value_type*			pointer;
					typedef value_type&			reference;
					typedef const value_type*	const_pointer;
					typedef const value_type&	const_reference;

					const_iterator_base() {}

					const_reference operator* ()	const { return iter->second.data; }
					const_pointer	operator-> ()	const { return &iter->second.data; }

					const_iterator_base& operator++()		{ C::next( iter); return *this; }
					const_iterator_base  operator++(int)	{ const_iterator_base it( iter); C::next( iter); return it; }
					const_iterator_base& operator--()		{ C::prev( iter); return *this; }
					const_iterator_base  operator--(int)	{ const_iterator_base it( iter); C::prev( iter); return it; }

					bool operator == ( const const_iterator_base& it) const { return iter == it.iter; }
					bool operator != ( const const_iterator_base& it) const { return iter != it.iter; }
			};

			template <const bool reverse>
			class iterator_base: public const_iterator_base<reverse>
			{
				protected:
					friend lru_cache;
					iterator_base( const HmIterator& it): const_iterator_base( it) {}

				public:
					reference operator* ()	const { return iter->second.data; }
					pointer operator-> ()	const { return &iter->second.data; }

					iterator_base& operator++()		{ C::next( iter); return *this; }
					iterator_base  operator++(int)	{ const_iterator it( iter); C::next( iter); return it; }
					iterator_base& operator--()		{ C::prev( iter); return *this; }
					iterator_base  operator--(int)	{ const_iterator it( iter); C::prev( iter); return it; }
			};

			template <const bool reverse>
			class const_mru_iterator_base
			{
				protected:	
					friend class lru_cache;
					typedef Item* ItemPtr;
					typedef MruCursor<reverse> C;
					ItemPtr iter;
					const_mru_iterator_base( ItemPtr const it): iter( it) {}

				public:
					typedef std::bidirectional_iterator_tag iterator_category;
					typedef Data				value_type;
					typedef ptrdiff_t			difference_type;
					typedef value_type*			pointer;
					typedef value_type&			reference;
					typedef const value_type*	const_pointer;
					typedef const value_type&	const_reference;

					const_mru_iterator_base(): iter( 0) {}

					const_reference operator* ()	const { return iter->data; }
					const_pointer	operator-> ()	const { return &iter->data; }

					const_mru_iterator_base& operator++()		{ C::next( iter); return *this; }
					const_mru_iterator_base  operator++(int)	{ const_mru_iterator_base it( iter); C::next( iter); return it; }
					const_mru_iterator_base& operator--()		{ C::prev( iter); return *this; }
					const_mru_iterator_base  operator--(int)	{ const_mru_iterator_base it( iter); C::prev( iter); return it; }

					bool operator == ( const const_mru_iterator_base& it) const { return iter == it.iter; }
					bool operator != ( const const_mru_iterator_base& it) const { return iter != it.iter; }
			};

			template <const bool reverse>
			class mru_iterator_base: public const_mru_iterator_base<reverse>
			{
				protected:
					friend class lru_cache;
					mru_iterator_base( ItemPtr it): const_mru_iterator_base( it) {}

				public:
					reference	operator* ()	const { return iter->data; }
					pointer		operator-> ()	const { return &iter->data; }

					mru_iterator_base& operator++()		{ C::next( iter); return *this; }
					mru_iterator_base  operator++(int)	{ mru_iterator_base it( iter); C::next( iter); return it; }
					mru_iterator_base& operator--()		{ C::prev( iter); return *this; }
					mru_iterator_base  operator--(int)	{ mru_iterator_base it( iter); C::prev( iter); return it; }
			};

			typedef const_iterator_base<false> const_iterator;
			typedef iterator_base<false> iterator;

			typedef const_iterator_base<true> const_reverse_iterator;
			typedef iterator_base<true> reverse_iterator;

			typedef const_mru_iterator_base<false> const_mru_iterator;
			typedef mru_iterator_base<false> mru_iterator;

			typedef const_mru_iterator_base<true> const_reverse_mru_iterator;
			typedef mru_iterator_base<true> reverse_mru_iterator;

		protected:
			typedef std::set<Item*>	LockedSet;

			MruItem					iMruHead;
			size_t					iMaxLimit;
			eviction_observer_type*	iObserver;
			Statistics<lru_cache>		iStatistics;
			HashMap					iMap;
			LockedSet				iLockedSet;

			Item* head() const { return (Item*) &iMruHead; }
			void touch( HmIterator& it) { if ( it != iMap.end()) set_mru( it); }

			iterator erase( const HmIterator& it)
			{
				if ( it != iMap.end())
				{
					it->second.unlink();
					if ( iObserver)
					{
						(*iObserver)( it->second.data);
					}
					return iMap.erase( it);
				}
				return iMap.end();
			}

			void set_mru( HmIterator& it) { set_mru( it->second); }
			void set_mru( Item& item)
			{
				if ( item.prev != head())
				{
					item.unlink();
					head()->append( &item);
				}
			}

		public:
			lru_cache( const size_t maxLimit, eviction_observer_type* const observer = 0):
				iMaxLimit( maxLimit),
				iObserver( observer)
			{ 
				iMruHead.prev = iMruHead.next = head();
			}

			~lru_cache()
			{
				clear();
			}

			size_t size()  const	{ return iMap.size(); }
			size_t max_limit() const{ return iMaxLimit; }
			bool is_full() const	{ return size() == max_limit(); }

			void set_observer( eviction_observer_type* const item) { iObserver = item; }

			statistics_type& statistics() const { return iStatistics; }
			statistics_type& statistics() { return iStatistics; }

			iterator begin()	{ return iMap.begin(); }
			iterator end()		{ return iMap.end(); }

			const_iterator begin()	const { return iMap.begin(); }
			const_iterator end()	const { return iMap.end(); }

			reverse_iterator rbegin()	{ return iMap.begin(); }
			reverse_iterator rend()		{ return iMap.end(); }

			const_reverse_iterator rbegin()	const { return iMap.begin(); }
			const_reverse_iterator rend()	const { return iMap.end(); }


			mru_iterator mru_begin()	{ return iMruHead.next; }
			mru_iterator mru_end()		{ return head(); }

			const_mru_iterator mru_begin()	const { return iMruHead.next; }
			const_mru_iterator mru_end()	const { return head(); }

			reverse_mru_iterator mru_rbegin()	{ return iMruHead.prev; }
			reverse_mru_iterator mru_rend()		{ return head(); }

			const_reverse_mru_iterator mru_rbegin()	const { return iMruHead.prev; }
			const_reverse_mru_iterator mru_rend()	const { return head(); }


			iterator find( const Key& key, const bool mru = true)
			{
				iStatistics.inc_refs();
				HmIterator it = iMap.find( key);
				if ( it != iMap.end())
				{
					if ( mru)
						set_mru( it);
				}
				else
					iStatistics.inc_misses();
				return it;
			}

			void touch( const Key& key)	{ touch( iMap.find( key)); }
			void touch( iterator& it)	{ touch( it.iter); }

			bool is_locked( const iterator& it) const
			{
				return it.iter != iMap.end() && iLockedSet.find( &it.iter.second) != iLockedSet.end();
			}

			void lock( const iterator& it)
			{
				if ( it.iter != iMap.end())
				{
					Item& item = it.iter->second;
					item.prev->next = item.next;
					item.next->prev = item.prev;
					item.next = item.prev = 0;
					iLockedSet.insert( &it.iter->second);
				}
			}

			void unlock( const iterator& it)
			{
				if ( it.iter != iMap.end() && iLockedSet.find( &it.iter->second) != iLockedSet.end())
				{
					Item& item = it.iter->second;
					item.next = iMruHead.next;
					item.prev = head();
					iMruHead.next->prev = &item;
					iMruHead.next = &item;
				}
			}

			iterator erase( const iterator& it)	{ return erase( it.iter); }
			iterator erase( const Key &key)		{ return erase( iMap.find( key)); }

			void clear() 
			{ 
				iMruHead.clear();
				if ( iObserver)
				{
					//const HashMap::reverse_iterator end = iMap.rend();
					//for( HashMap::reverse_iterator i = iMap.rbegin(); i != end; ++i)
					const HashMap::iterator end = iMap.end();
					for( HashMap::iterator i = iMap.begin(); i != end; ++i)
					{
						(*iObserver)( i->second.data);
					}
				}
				iMap.clear(); 
			}

			std::pair<iterator, bool> get( const Key& key, const bool find = true)
			{
				bool exists = true;
				HmIterator it = iMap.find( key);
				if ( find)
					iStatistics.inc_refs();
				if ( it == iMap.end())
				{
					if ( find)
						iStatistics.inc_misses();
					typedef hm_accessor<HashMap> Accessor;
					if ( is_full())
					{
						it = iMap.begin();
						Accessor::set( it, iMruHead.prev->backRef);
						erase( it);
					}

					std::pair<HmIterator, bool> item = iMap.insert( std::pair<Key,Item>( key, Item()));
					if ( item.second)
					{
						Accessor::assign( it, item.first);
						iMruHead.append( &it->second);
					}
					exists = false;
				}
				return std::pair<iterator, bool>( iterator( it), exists);
			}
	};
}
