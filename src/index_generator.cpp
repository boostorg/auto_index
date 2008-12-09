// Copyright 2008 John Maddock
//
// Use, modification and distribution are subject to the
// Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "auto_index.hpp"

//
// Get a numerical ID for the next item:
//
std::string get_next_index_id()
{
   static int index_id_count = 0;
   std::stringstream s;
   s << "idx_id_" << index_id_count;
   ++index_id_count;
   return s.str();
}
//
// Generate indexes using our own internal method:
//
void generate_indexes()
{
   for(boost::tiny_xml::element_list::const_iterator i = indexes.begin(); i != indexes.end(); ++i)
   {
      boost::tiny_xml::element_ptr node = *i;
      const std::string* category = find_attr(node, "type");
      bool has_title = false;

      for(boost::tiny_xml::element_list::const_iterator k = (*i)->elements.begin(); k != (*i)->elements.end(); ++k)
      {
         if((**k).name == "title")
         {
            has_title = true;
            break;
         }
      }

      boost::tiny_xml::element_ptr navbar(new boost::tiny_xml::element());
      navbar->name = "para";
      node->elements.push_back(navbar);

      char last_c = 0;
      boost::tiny_xml::element_ptr list(new boost::tiny_xml::element());
      list->name = "variablelist";
      boost::tiny_xml::element_ptr listentry;
      boost::tiny_xml::element_ptr listitem;
      boost::tiny_xml::element_ptr sublist;
      node->elements.push_back(list);

      for(index_entry_set::const_iterator i = index_entries.begin(); i != index_entries.end(); ++i)
      {
         if((0 == category) || (category->size() == 0) || (category && (**i).category == *category))
         {
            if(std::toupper((**i).key[0]) != last_c)
            {
               std::string id_name = get_next_index_id();
               last_c = std::toupper((**i).key[0]);
               listentry.reset(new boost::tiny_xml::element());
               listentry->name = "varlistentry";
               boost::tiny_xml::attribute id;
               id.name = "id";
               id.value = id_name;
               listentry->attributes.push_back(id);
               boost::tiny_xml::element_ptr term(new boost::tiny_xml::element());
               term->name = "term";
               term->content.assign(&last_c, 1);
               listentry->elements.push_front(term);
               list->elements.push_back(listentry);
               listitem.reset(new boost::tiny_xml::element());
               listitem->name = "listitem";
               sublist.reset(new boost::tiny_xml::element());
               sublist->name = "variablelist";
               listitem->elements.push_back(sublist);
               listentry->elements.push_back(listitem);

               boost::tiny_xml::element_ptr nav(new boost::tiny_xml::element());
               nav->name = "";
               nav->content = " ";
               boost::tiny_xml::element_ptr navlink(new boost::tiny_xml::element());
               navlink->name = "link";
               navlink->content = term->content;
               boost::tiny_xml::attribute navid;
               navid.name = "linkend";
               navid.value = id_name;
               navlink->attributes.push_back(navid);
               navbar->elements.push_back(navlink);
               navbar->elements.push_back(nav);
            }
            boost::tiny_xml::element_ptr subentry(new boost::tiny_xml::element());
            subentry->name = "varlistentry";
            boost::tiny_xml::element_ptr subterm(new boost::tiny_xml::element());
            subterm->name = "term";
            if((**i).id.empty())
               subterm->content = (**i).key;
            else
            {
               boost::tiny_xml::element_ptr link(new boost::tiny_xml::element());
               link->name = "link";
               link->content = (**i).key;
               boost::tiny_xml::attribute at;
               at.name = "linkend";
               at.value = (**i).id;
               link->attributes.push_back(at);
               subterm->elements.push_back(link);
            }
            subentry->elements.push_back(subterm);
            boost::tiny_xml::element_ptr subitem(new boost::tiny_xml::element());
            subitem->name = "listitem";
            subentry->elements.push_back(subitem);
            sublist->elements.push_back(subentry);

            boost::tiny_xml::element_ptr secondary_list(new boost::tiny_xml::element());
            secondary_list->name = "simplelist";
            subitem->elements.push_back(secondary_list);

            for(index_entry_set::const_iterator k = (**i).sub_keys.begin(); k != (**i).sub_keys.end(); ++k)
            {
               boost::tiny_xml::element_ptr member(new boost::tiny_xml::element());
               member->name = "member";
               boost::tiny_xml::element_ptr para(new boost::tiny_xml::element());
               para->name = "para";
               if((**k).id.empty())
                  para->content = (**k).key;
               else
               {
                  boost::tiny_xml::element_ptr link(new boost::tiny_xml::element());
                  link->name = "link";
                  boost::tiny_xml::attribute at;
                  at.name = "linkend";
                  at.value = (**k).id;
                  link->attributes.push_back(at);
                  link->content = (**k).key;
                  para->elements.push_back(link);
               }
               member->elements.push_back(para);
               secondary_list->elements.push_back(member);
            }
         }
      }
      node->name = "section";
      node->attributes.clear();
      if(!has_title)
      {
         boost::tiny_xml::element_ptr t(new boost::tiny_xml::element());
         t->name = "title";
         t->content = "Index";
         node->elements.push_front(t);
      }
   }
}

