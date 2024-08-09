/**  
 * This file contains code extracted from boost 1.78, by wcjohns 20240809.
 * 
 * It is the minimal amount of the boost hash implementation that we need to 
 * hash a SpecFile to generate a UUID based on spectrum file contents.
 * We use `boost::hash` because `std::hash` is not guaranteed to be stable 
 * across executions of the same executable.
 * 
 * Installing boost, particularly on Windows, for just this functionality
 * has proven to be a burden for developers, so this code was extracted
 * to be stand-alone.
 * 
 * Some function signatures have been changed to support this extraction
 * of a small amount of the code - this is not the complete implementation
 * of `boost::hash`, just what we use in SpecUtils.  The actual computation
 * code is left unchanged, and it was checked for a number of spectrum
 * files that this code produced the same answer as boost proper.
*/

// wcjohns: original notice from the top of `boost/container_hash/hash.hpp`:
//
// Copyright 2005-2014 Daniel James.
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

//  Based on Peter Dimov's proposal
//  http://www.open-std.org/JTC1/SC22/WG21/docs/papers/2005/n1756.pdf
//  issue 6.18.
//
//  This also contains public domain code from MurmurHash. From the
//  MurmurHash header:

// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.


#if !defined(SpecUtils_boost_hash_hpp)
#define SpecUtils_boost_hash_hpp

#include <string>
#include <vector>
#include <cstdint>
#include <type_traits>

namespace boost_hash
{
  // Forward declaration
  template <class T>
  void hash_combine( std::size_t& seed, T const& v );
  
  namespace hash_detail
  {
    template <typename T>
    typename std::enable_if<sizeof(T) == 8, std::size_t>::type
    hash_mix( T x )
    {
      std::uint64_t const m = (std::uint64_t(0xe9846af) << 32) + 0x9b1a615d;
      
      x ^= x >> 32;
      x *= m;
      x ^= x >> 32;
      x *= m;
      x ^= x >> 28;
      
      return x;
    }
    
    template <typename T>
    typename std::enable_if<sizeof(T) == 4, std::size_t>::type
    hash_mix( T x )
    {
      std::uint32_t const m1 = 0x21f0aaad;
      std::uint32_t const m2 = 0x735a2d97;
      
      x ^= x >> 16;
      x *= m1;
      x ^= x >> 15;
      x *= m2;
      x ^= x >> 15;
      
      return x;
    }
    
    template<class T,
    bool bigger_than_size_t = (sizeof(T) > sizeof(std::size_t)),
    bool is_unsigned = std::is_unsigned<T>::value,
    std::size_t size_t_bits = sizeof(std::size_t) * CHAR_BIT,
    std::size_t type_bits = sizeof(T) * CHAR_BIT>
    struct hash_integral_impl;
    
    template<class T, bool is_unsigned, std::size_t size_t_bits, std::size_t type_bits> struct hash_integral_impl<T, false, is_unsigned, size_t_bits, type_bits>
    {
      static std::size_t fn( T v )
      {
        return static_cast<std::size_t>( v );
      }
    };
    
    template<class T, std::size_t size_t_bits, std::size_t type_bits> struct hash_integral_impl<T, true, false, size_t_bits, type_bits>
    {
      static std::size_t fn( T v )
      {
        typedef typename std::make_unsigned<T>::type U;
        
        if( v >= 0 )
        {
          return hash_integral_impl<U>::fn( static_cast<U>( v ) );
        }
        else
        {
          return ~hash_integral_impl<U>::fn( static_cast<U>( ~static_cast<U>( v ) ) );
        }
      }
    };
    
    template<class T> struct hash_integral_impl<T, true, true, 32, 64>
    {
      static std::size_t fn( T v )
      {
        std::size_t seed = 0;
        
        seed = static_cast<std::size_t>( v >> 32 ) + hash_detail::hash_mix( seed );
        seed = static_cast<std::size_t>( v ) + hash_detail::hash_mix( seed );
        
        return seed;
      }
    };
    
    
    // float
    template <typename T>
    typename std::enable_if<sizeof(T) == 4, std::size_t>::type
    hash_float_impl( T v )
    {
      std::uint32_t w;
      std::memcpy( &w, &v, sizeof( v ) );
      
      return w;
    }
    
    // double
    template <typename T>
    typename std::enable_if<sizeof(T) == 8, std::size_t>::type
    hash_float_impl( T v )
    {
      std::uint64_t w;
      std::memcpy( &w, &v, sizeof( v ) );
      
      //return boost_hash::hash_value( w );
      return hash_detail::hash_integral_impl<std::uint64_t>::fn( w );
    }
    
    
    std::size_t hash_range( std::size_t seed, const char *first, const char *last )
    {
      std::size_t n = static_cast<std::size_t>( last - first );
      
      for( ; n >= 4; first += 4, n -= 4 )
      {
        std::uint32_t w =
        static_cast<std::uint32_t>( static_cast<unsigned char>( first[0] ) ) |
        static_cast<std::uint32_t>( static_cast<unsigned char>( first[1] ) ) <<  8 |
        static_cast<std::uint32_t>( static_cast<unsigned char>( first[2] ) ) << 16 |
        static_cast<std::uint32_t>( static_cast<unsigned char>( first[3] ) ) << 24;
        
        hash_combine( seed, w );
      }
      
      {
        std::uint32_t w = 0x01u;
        
        switch( n )
        {
          case 1:
            
            w =
            static_cast<std::uint32_t>( static_cast<unsigned char>( first[0] ) ) |
            0x0100u;
            
            break;
            
          case 2:
            
            w =
            static_cast<std::uint32_t>( static_cast<unsigned char>( first[0] ) ) |
            static_cast<std::uint32_t>( static_cast<unsigned char>( first[1] ) ) <<  8 |
            0x010000u;
            
            break;
            
          case 3:
            
            w =
            static_cast<std::uint32_t>( static_cast<unsigned char>( first[0] ) ) |
            static_cast<std::uint32_t>( static_cast<unsigned char>( first[1] ) ) <<  8 |
            static_cast<std::uint32_t>( static_cast<unsigned char>( first[2] ) ) << 16 |
            0x01000000u;
            
            break;
        }
        
        hash_combine( seed, w );
      }
      
      return seed;
    }
  }//namespace hash_detail
  
  // Hash floating point types (e.g., `float` and `double`)
  template <typename T>
  typename std::enable_if<std::is_floating_point<T>::value, std::size_t>::type
  hash_value( T v )
  {
    return boost_hash::hash_detail::hash_float_impl( v );
  }
  
  // Hash integral types (e.g., `int` and `uint64_t`, etc)
  template <typename T>
  typename std::enable_if<std::is_integral<T>::value, std::size_t>::type
  hash_value( T v )
  {
    return hash_detail::hash_integral_impl<T>::fn( v );
  }
  
  std::size_t hash_value( const std::string &v )
  {
    return boost_hash::hash_detail::hash_range( 0, v.data(), v.data() + v.size() );
  }
  
  template <class T>
  std::size_t hash_value( const std::vector<T> &v )
  {
    std::size_t seed = 0;
    for( const T &val : v )
      hash_combine<T>(seed, val );
    
    return seed;
  }
  
  
  template <class T>
  void hash_combine( std::size_t& seed, T const& v )
  {
    seed = boost_hash::hash_detail::hash_mix( seed + 0x9e3779b9 + boost_hash::hash_value( v ) );
  }
}//namespace boost_hash

#endif //SpecUtils_boost_hash_hpp
