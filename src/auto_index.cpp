// Copyright 2008 John Maddock
//
// Use, modification and distribution are subject to the
// Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "auto_index.hpp"

int help()
{
   std::cout << "Please refer to the documentation for the correct command line syntax" << std::endl;
   return 1;
}

void eat_whitespace(std::istream & is)
{
   char c = is.peek();
   while(std::isspace(c))
   {
      is.get(c);
      c = is.peek();
   }
}

void eat_block(std::string& result, std::istream & is)
{
   //
   // everything until we get to a closing '>':
   //
   char c;
   while(is.get(c) && c != '>')
   {
      result += c;
      if(c == '\\')
      {
         is.get(c);
         result += c;
      }
   }
   result += c;
}

std::string get_header(std::istream & is)
{
   //
   // We need to get any leading <? and <! elements:
   //
   std::string result;
   eat_whitespace(is);
   if(is.get() != '<')
      throw std::runtime_error("Invalid leading markup in XML file found");
   char c = is.peek();
   while((c == '?') || (c == '!'))
   {
      result += '<';
      eat_block(result, is);
      eat_whitespace(is);
      if(is.get() != '<')
         throw std::runtime_error("Invalid leading markup in XML file found");
      c = is.peek();
      result += '\n';
   }
   return result;
}
//
// Find attribute named "name" in node "node":
//
const std::string* find_attr(boost::tiny_xml::element_ptr node, const char* name)
{
   for(boost::tiny_xml::attribute_list::const_iterator i = node->attributes.begin();
      i != node->attributes.end(); ++i)
   {
      if(i->name == name)
         return &(i->value);
   }
   return 0;
}
//
// Get the ID of the current block scope, basically
// move up the XML tree until we find a valid ID:
//
const std::string* get_current_block_id(node_id const* id)
{
   while((id->id == 0) && (id->prev))
      id = id->prev;
   return id->id;
}
//
// Get the title of the current block scope, basically
// move up the XML tree until we find a valid title:
//
const std::string& get_current_block_title(title_info const* id)
{
   while((id->title.size() == 0) && (id->prev))
      id = id->prev;
   return id->title;
}
//
// Get all the content under this node, with any inline XML
// stripped out:
//
std::string get_consolidated_content(boost::tiny_xml::element_ptr node)
{
   std::string result(node->content);
   for(boost::tiny_xml::element_list::const_iterator i = node->elements.begin();
      i != node->elements.end(); ++i)
   {
      result += " ";
      result += get_consolidated_content(*i);
   }
   static const boost::regex e("(^[[:space:]]+)|([[:space:]]+)|([[:space:]]+$)");
   return regex_replace(result, e, "(?2 )", boost::regex_constants::format_all);
}
//
// Rewrite a title based on any rewrite rules we may have:
//
std::string rewrite_title(const std::string& title, const std::string& id)
{
   for(std::list<id_rewrite_rule>::const_iterator i = id_rewrite_list.begin(); i != id_rewrite_list.end(); ++i)
   {
      if(i->base_on_id)
      {
         if(regex_match(id, i->id))
            return i->new_name;
      }
      else
      {
         if(regex_match(title, i->id))
            return regex_replace(title, i->id, i->new_name);
      }
   }
   return title;
}
//
// This does most of the work: process the node pointed to, and any children
// that it may have:
//
void process_node(boost::tiny_xml::element_ptr node, node_id* prev, title_info* pt, boost::tiny_xml::element_ptr parent_node = boost::tiny_xml::element_ptr())
{
   //
   // Store the current ID and title as nested scoped objects:
   //
   node_id id = { 0, prev };
   id.id = find_attr(node, "id");
   title_info title = { "", pt};
   if(node->name == "title")
   {
      //
      // This actually sets the title of the enclosing scope, 
      // not this tag itself:
      //
      title.prev->title = get_consolidated_content(node);
      if(verbose)
         std::cout << "Indexing section: " << title.prev->title << std::endl;
   }
   else if(node->name == "index")
   {
      // Keep track of all the indexes we see:
      indexes.push_back(node);
      if(parent_node->name == "para")
         parent_node->name = "";
   }

   //
   // Search content for items: we only search if the name of this node is
   // empty, and the content is not empty, and the content is not whitespace
   // alone.
   //
   static const boost::regex space_re("[[:space:]]+");
   if((node->name == "") && node->content.size() && !regex_match(node->content, space_re))
   {
      // Save block ID and title in case we find some hits:
      const std::string* pid = get_current_block_id(&id);
      const std::string& rtitle = get_current_block_title(&title);
      const std::string simple_title = rewrite_title(rtitle, *pid);
      // Scan for each index term:
      for(std::multiset<index_info>::const_iterator i = index_terms.begin();
            i != index_terms.end(); ++i)
      {
         if(regex_search(node->content, i->search_text))
         {
            //
            // We need to check to see if this term has already been indexed
            // in this zone, in order to prevent duplicate entries, also check
            // that any constrait placed on the terms ID is satisfied:
            //
            std::pair<std::string, std::string> item_index(*pid, i->term);
            if(((no_duplicates == false) || (0 == found_terms.count(item_index))) 
               && (i->search_id.empty() || regex_search(*pid, i->search_id)))
            {
               // We have something to index!
               found_terms.insert(item_index);

               //
               // First off insert index entry with primary term
               // consisting of the section title, and secondary term the
               // actual index term:
               //
               if(internal_indexes == false)
               {
                  // Insert an <indexterm> into the XML:
                  boost::tiny_xml::element_ptr p(new boost::tiny_xml::element());
                  p->name = "indexterm";
                  boost::tiny_xml::element_ptr prim(new boost::tiny_xml::element());
                  prim->name = "primary";
                  prim->elements.push_front(boost::tiny_xml::element_ptr(new boost::tiny_xml::element()));
                  prim->elements.front()->content = simple_title;
                  p->elements.push_front(prim);

                  boost::tiny_xml::element_ptr sec(new boost::tiny_xml::element());
                  sec->name = "secondary";
                  sec->elements.push_front(boost::tiny_xml::element_ptr(new boost::tiny_xml::element()));
                  sec->elements.front()->content = i->term;
                  p->elements.push_back(sec);
                  if(parent_node)
                     parent_node->elements.push_front(p);
               }
               // Track the entry in our internal index:
               index_entry_ptr item1(new index_entry(simple_title));
               index_entry_ptr item2(new index_entry(i->term, *pid));
               if(index_entries.find(item1) == index_entries.end())
               {
                  index_entries.insert(item1);
               }
               (**index_entries.find(item1)).sub_keys.insert(item2);

               //
               // Now insert another index entry with the index term
               // as the primary key, and the section title as the 
               // secondary key, this one gets assigned to the 
               // appropriate index category if there is one:
               //
               if(internal_indexes == false)
               {
                  // Insert <indexterm> into the XML:
                  boost::tiny_xml::element_ptr p2(new boost::tiny_xml::element());
                  p2->name = "indexterm";
                  if(i->category.size())
                  {
                     p2->attributes.push_back(boost::tiny_xml::attribute("type", i->category));
                  }
                  boost::tiny_xml::element_ptr prim2(new boost::tiny_xml::element());
                  prim2->name = "primary";
                  prim2->elements.push_front(boost::tiny_xml::element_ptr(new boost::tiny_xml::element()));
                  prim2->elements.front()->content = i->term;
                  p2->elements.push_front(prim2);

                  boost::tiny_xml::element_ptr sec2(new boost::tiny_xml::element());
                  sec2->name = "secondary";
                  sec2->elements.push_front(boost::tiny_xml::element_ptr(new boost::tiny_xml::element()));
                  sec2->elements.front()->content = rtitle;
                  p2->elements.push_back(sec2);
                  if(parent_node)
                     parent_node->elements.push_front(p2);
               }
               // Track the entry in our internal index:
               index_entry_ptr item3(new index_entry(i->term));
               index_entry_ptr item4(new index_entry(rtitle, *pid));
               if(index_entries.find(item3) == index_entries.end())
               {
                  index_entries.insert(item3);
                  if(i->category.size())
                  {
                     (**index_entries.find(item3)).category = i->category;
                  }
               }
               (**index_entries.find(item3)).sub_keys.insert(item4);
            }
         }
      }
   }
   //
   // Recurse through children:
   //
   for(boost::tiny_xml::element_list::const_iterator i = node->elements.begin();
      i != node->elements.end(); ++i)
   {
      process_node(*i, &id, &title, node);
   }
}

void process_nodes(boost::tiny_xml::element_ptr node)
{
   node_id id = { 0, };
   title_info t = { "", 0 };
   process_node(node, &id, &t);
}

std::string infile, outfile;
std::multiset<index_info> index_terms;
std::set<std::pair<std::string, std::string> > found_terms;
bool no_duplicates = false;
bool verbose = false;
index_entry_set index_entries;
boost::tiny_xml::element_list indexes;
std::list<id_rewrite_rule> id_rewrite_list;
bool internal_indexes = false;

int main(int argc, char* argv[])
{
   try{

   if(argc < 2)
      return help();

   //
   // Process arguments:
   //
   for(int i = 1; i < argc; ++i)
   {
      if(std::strncmp(argv[i], "in=", 3) == 0)
      {
         infile = argv[i] + 3;
      }
      else if(std::strncmp(argv[i], "out=", 4) == 0)
      {
         outfile = argv[i] + 4;
      }
      else if(std::strncmp(argv[i], "scan=", 5) == 0)
      {
         scan_file(argv[i] + 5);
      }
      else if(std::strncmp(argv[i], "script=", 7) == 0)
      {
         process_script(argv[i] + 7);
      }
      else if(std::strcmp(argv[i], "--no-duplicates") == 0)
      {
         no_duplicates = true;
      }
      else if(std::strcmp(argv[i], "--internal-index") == 0)
      {
         internal_indexes = true;
      }
      else if(std::strcmp(argv[i], "--verbose") == 0)
      {
         verbose = true;
      }
      else
      {
         std::cerr << "Unrecognosed option " << argv[i] << std::endl;
         return 1;
      }
   }

   if(infile.empty())
   {
      return help();
   }
   if(outfile.empty())
   {
      return help();
   }

   std::ifstream is(infile.c_str());
   if(!is.good())
   {
      std::cerr << "Unable to open XML data file " << argv[1] << std::endl;
      return 1;
   }
   //
   // We need to skip any leading <? and <! elements:
   //
   std::string header = get_header(is);
   boost::tiny_xml::element_ptr xml = boost::tiny_xml::parse(is, "");
   is.close();

   //index_terms["students_t_distribution"] = "\\<students_t_distribution\\>";

   std::cout << "Indexing " << index_terms.size() << " terms..." << std::endl;

   process_nodes(xml);

   if(internal_indexes)
      generate_indexes();

   std::ofstream os(outfile.c_str());
   os << header << std::endl;
   boost::tiny_xml::write(*xml, os);

   }
   catch(const std::exception& e)
   {
      std::cerr << e.what() << std::endl;
      return 1;
   }
   catch(const std::string& s)
   {
      std::cerr << s << std::endl;
      return 1;
   }

   return 0;
}
