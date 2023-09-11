#ifndef SpecUtils_RapidXmlUtils_h
#define SpecUtils_RapidXmlUtils_h
/* SpecUtils: a library to parse, save, and manipulate gamma spectrum data files.
 
 Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC
 (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
 Government retains certain rights in this software.
 For questions contact William Johnson via email at wcjohns@sandia.gov, or
 alternative emails of interspec@sandia.gov.
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "SpecUtils_config.h"

#include "3rdparty/rapidxml/rapidxml.hpp"

namespace SpecUtils
{
  //The functions and macros in this namespace (I know, macros dont obey
  //  namespaces, but whatever for now) are a small scale first try towards
  //  using non-destructive XML parsing so memory mapped files can be used as
  //  input to the file reader or whatever.  The goal is to enable reasonably
  //  mistake free parsing, while cutting down the verbosity of the code, and
  //  avoiding unecassary allocations.
  //
  //The general convention is that macros are totally unsafe and dont check for
  //  null pointers or anything, while functions are safe to call with nullptrs.
  //  The functions with the postfix "_nso" are name space optional functions
  //  meant to help with cases where some XML files use a namespace, and others
  //  do not (rapidxml is name-space unaware, so we have to deal with them).
  //
  //TODO: Finish converting older sections of code to use these functions/macros
  
  //Function to determine static c-string length at compile time.
  template<size_t N>
  size_t lengthof(const char (&)[N])
  {
    return N - 1;
  }
  
#define XML_VALUE_COMPARE( node, cstr ) (::rapidxml::internal::compare((node)->value(), (node)->value_size(), cstr, SpecUtils::lengthof(cstr), true))
#define XML_VALUE_ICOMPARE( node, cstr ) (::rapidxml::internal::compare((node)->value(), (node)->value_size(), cstr, SpecUtils::lengthof(cstr), false))
#define XML_NAME_COMPARE( node, cstr ) (::rapidxml::internal::compare((node)->name(), (node)->name_size(), cstr, SpecUtils::lengthof(cstr), true))
#define XML_NAME_ICOMPARE( node, cstr ) (::rapidxml::internal::compare((node)->name(), (node)->name_size(), cstr, SpecUtils::lengthof(cstr), false))
  
#define XML_FIRST_NODE(node,name)((node)->first_node(name,SpecUtils::lengthof(name),true))
#define XML_FIRST_INODE(node,name)((node)->first_node(name,SpecUtils::lengthof(name),false))
#define XML_FIRST_ATTRIB(node,name)((node)->first_attribute(name,SpecUtils::lengthof(name),true))
#define XML_FIRST_IATTRIB(node,name)((node)->first_attribute(name,SpecUtils::lengthof(name),false))
  
#define XML_FIRST_NODE_CHECKED(node,name)((node) ? (node)->first_node(name,SpecUtils::lengthof(name),true) : (::rapidxml::xml_node<char> *)0)
#define XML_FIRST_ATTRIB_CHECKED(node,name)((node) ? (node)->first_attribute(name,SpecUtils::lengthof(name),true) : (::rapidxml::xml_node<char> *)0)
  
#define XML_NEXT_TWIN(node)((node)->next_sibling((node)->name(), (node)->name_size()))
#define XML_NEXT_TWIN_CHECKED(node)((node) ? (node)->next_sibling((node)->name(), (node)->name_size()): (::rapidxml::xml_node<char> *)0)
  
  //Usuage:
  //XML_FOREACH_CHILD( child_node_variable, parent_node, "ChildElementName" ){
  //  assert( child_node_variable->name() == "ChildElementName" );
  // }
#define XML_FOREACH_CHILD( nodename, parentnode, childnamestr ) \
for( const ::rapidxml::xml_node<char> *nodename = XML_FIRST_NODE_CHECKED(parentnode,childnamestr); \
nodename; \
nodename = nodename->next_sibling(childnamestr,SpecUtils::lengthof(childnamestr),true) )
  
  template<class Ch,size_t n>
  bool xml_value_compare( const ::rapidxml::xml_base<Ch> *node, const char (&value)[n] )
  {
    if( !node )
      return false;
    if( n<=1 && !node->value_size() )  //They are both empty.
      return true;
    return ::rapidxml::internal::compare((node)->value(), (node)->value_size(), value, n-1, true);
  }
  
  template<class Ch>
  std::string xml_value_str( const ::rapidxml::xml_base<Ch> *n )
  {
    //if( !n || !n->value_size() )
    //  return string();
    //return std::string( n->value(), n->value() + n->value_size() );
    return (n && n->value_size()) ? std::string(n->value(),n->value()+n->value_size()) : std::string();
  }
  
  template<class Ch>
  std::string xml_name_str( const ::rapidxml::xml_base<Ch> *n )
  {
    //if( !node || !node->name_size() )
    //  return "";
    //return std::string( node->name(), node->name() + node->name_size() );
    return (n && n->name_size()) ? std::string(n->name(), n->name() + n->name_size()) : std::string();
  }
  
  template<size_t n>
  const ::rapidxml::xml_node<char> *xml_first_node( const ::rapidxml::xml_node<char> *parent, const char (&name)[n] )
  {
    static_assert( n > 1, "Element name to xml_first_node must not be empty." );
    return parent ? parent->first_node(name, n-1) : nullptr;
  }
  
  template<size_t n>
  const ::rapidxml::xml_node<char> *xml_first_inode( const ::rapidxml::xml_node<char> *parent, const char (&name)[n] )
  {
    static_assert( n > 1, "Element name to xml_first_node must not be empty." );
    return parent ? parent->first_node(name, n-1,false) : nullptr;
  }
  
  template<size_t n>
  const ::rapidxml::xml_attribute<char> *xml_first_attribute( const ::rapidxml::xml_node<char> *parent, const char (&name)[n] )
  {
    static_assert( n > 1, "Element name to xml_first_attribute must not be empty." );
    return parent ? parent->first_attribute(name, n-1) : nullptr;
  }

  template<size_t n>
  const ::rapidxml::xml_attribute<char> *xml_first_iattribute( const ::rapidxml::xml_node<char> *parent, const char (&name)[n] )
  {
    static_assert( n > 1, "Element name to xml_first_iattribute must not be empty." );
    return parent ? parent->first_attribute(name, n-1,false) : nullptr;
  }

  template<size_t n, size_t m>
  const ::rapidxml::xml_node<char> *xml_first_node_nso( const ::rapidxml::xml_node<char> *parent,
                                                     const char (&name)[n],
                                                     const char (&ns)[m],
                                                     const bool case_sensitive = true )
  {
    if( !parent )
      return nullptr;
    
    if( m < 2 )
    {
      return parent->first_node(name, n-1, case_sensitive);
    }else
    {
      const ::rapidxml::xml_node<char> *answer = parent->first_node(name, n-1, case_sensitive);
      if( m>1 && !answer )
      {
        char newname[n+m-1];
        memcpy(newname, ns, m-1);
        memcpy(newname + m - 1, name, n);
        answer = parent->first_node(newname, m+n-2, case_sensitive);
      }
      
      return answer;
    }
  }
  
  template<size_t n>
  const ::rapidxml::xml_node<char> *xml_first_node_nso( const ::rapidxml::xml_node<char> *parent,
                                                     const char (&name)[n],
                                                     const std::string &ns,
                                                     const bool case_sensitive = true )
  {
    if( ns.size() < 2 )
    {
      return parent ? parent->first_node(name, n-1, case_sensitive) : nullptr;
    }else
    {
      if( !parent )
        return 0;
      const ::rapidxml::xml_node<char> *answer = parent->first_node(name, n-1, case_sensitive);
      if( !answer && !ns.empty() )
      {
        const std::string newname = ns + name;
        answer = parent->first_node(newname.c_str(), newname.size(), case_sensitive );
      }
      
      return answer;
    }
  }
}//namespace SpecUtils


#endif //SpecUtils_RapidXmlUtils_h
