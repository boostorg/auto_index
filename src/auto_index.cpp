
#include "tiny_xml.hpp"
#include <boost/regex.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

int help()
{
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

struct index_info
{
   std::string term;
   boost::regex search_text;
   boost::regex search_id;
   std::string category;
};
bool operator < (const index_info& a, const index_info& b)
{
   return a.term < b.term;
}

std::multiset<index_info> index_terms;
std::set<std::pair<std::string, std::string> > found_terms;
bool no_duplicates = false;

struct index_entry;
typedef boost::shared_ptr<index_entry> index_entry_ptr;
bool operator < (const index_entry_ptr& a, const index_entry_ptr& b);
typedef std::set<index_entry_ptr> index_entry_set;

std::string make_upper_key(const std::string& s)
{
   std::string result;
   for(std::string::const_iterator i = s.begin(); i != s.end(); ++i)
      result.append(1, std::toupper(*i));
   return result;
}

struct index_entry
{
   std::string key;
   std::string sort_key;
   std::string id;
   std::string category;
   index_entry_set sub_keys;

   index_entry(){}
   index_entry(const std::string& k) : key(k) { sort_key = make_upper_key(key); }
   index_entry(const std::string& k, const std::string& i) : key(k), id(i) { sort_key = make_upper_key(key); }
   index_entry(const std::string& k, const std::string& i, const std::string& c) : key(k), id(i), category(c) { sort_key = make_upper_key(key); }
};

index_entry_set index_entries;

bool operator < (const index_entry_ptr& a, const index_entry_ptr& b)
{
   return a->sort_key < b->sort_key;
}

boost::tiny_xml::element_list indexes;

struct id_rewrite_rule
{
   bool base_on_id;
   boost::regex id;
   std::string new_name;

   id_rewrite_rule(const std::string& i, const std::string& n, bool b)
      : base_on_id(b), id(i), new_name(n) {}
};
std::list<id_rewrite_rule> id_rewrite_list;

bool internal_indexes = false;

struct node_id
{
   const std::string* id;
   node_id* prev;
};

struct title_info
{
   std::string title;
   title_info* prev;
};

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

const std::string* get_current_block_id(node_id const* id)
{
   while((id->id == 0) && (id->prev))
      id = id->prev;
   return id->id;
}

const std::string& get_current_block_title(title_info const* id)
{
   while((id->title.size() == 0) && (id->prev))
      id = id->prev;
   return id->title;
}

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

void process_node(boost::tiny_xml::element_ptr node, node_id* prev, title_info* pt, boost::tiny_xml::element_ptr parent_node = boost::tiny_xml::element_ptr())
{
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
      std::cout << "Indexing section: " << title.prev->title << std::endl;
   }
   else if(node->name == "index")
   {
      indexes.push_back(node);
   }

   //
   // Search content for items:
   //
   static const boost::regex space_re("[[:space:]]+");
   if((node->name == "") && node->content.size() && !regex_match(node->content, space_re))
   {
      const std::string* pid = get_current_block_id(&id);
      const std::string& rtitle = get_current_block_title(&title);
      const std::string simple_title = rewrite_title(rtitle, *pid);

      for(std::multiset<index_info>::const_iterator i = index_terms.begin();
            i != index_terms.end(); ++i)
      {
         if(regex_search(node->content, i->search_text))
         {
            //
            // We need to check to see if this term has already been indexed
            // in this zone, in order to prevent duplicate entries:
            //
            std::pair<std::string, std::string> item_index(*pid, i->term);
            if(((no_duplicates == false) || (0 == found_terms.count(item_index))) 
               && (i->search_id.empty() || regex_search(*pid, i->search_id)))
            {
               found_terms.insert(item_index);
               /*
               std::cout << "<indexterm zone=\"" << *pid << "\">\n  <primary>"
                  << rtitle << "</primary>\n"
                  << "  <secondary>" << i->first << "</secondary>\n</indexterm>" << std::endl;
               std::cout << "<indexterm zone=\"" << *pid << "\">\n  <primary>"
                  << i->first << "</primary>\n"
                  << "  <secondary>" << rtitle << "</secondary>\n</indexterm>" << std::endl;
               */

               if(internal_indexes == false)
               {
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
               index_entry_ptr item1(new index_entry(simple_title));
               index_entry_ptr item2(new index_entry(i->term, *pid));
               if(index_entries.find(item1) == index_entries.end())
               {
                  index_entries.insert(item1);
               }
               (**index_entries.find(item1)).sub_keys.insert(item2);

               if(internal_indexes == false)
               {
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

void load_file(std::string& s, std::istream& is)
{
   s.erase();
   if(is.bad()) return;
   s.reserve(is.rdbuf()->in_avail());
   char c;
   while(is.get(c))
   {
      if(s.capacity() == s.size())
         s.reserve(s.capacity() * 3);
      s.append(1, c);
   }
}

void scan_file(const char* file)
{
   static const boost::regex class_e(
         // possibly leading whitespace:   
         "^[[:space:]]*" 
         // possible template declaration:
         "(template[[:space:]]*<[^;:{]+>[[:space:]]*)?"
         // class or struct:
         "(class|struct)[[:space:]]*" 
         // leading declspec macros etc:
         "("
            "\\<\\w+\\>"
            "("
               "[[:blank:]]*\\([^)]*\\)"
            ")?"
            "[[:space:]]*"
         ")*" 
         // the class name
         "(\\<\\w*\\>)[[:space:]]*" 
         // template specialisation parameters
         "(<[^;:{]+>)?[[:space:]]*"
         // terminate in { or :
         "(\\{|:[^;\\{()]*\\{)"
      );
   std::string text;
   std::ifstream is(file);
   load_file(text, is);
   {
      boost::sregex_token_iterator i(text.begin(), text.end(), class_e, 5), j;
      while(i != j)
      {
         index_info info;
         info.term = i->str();
         info.search_text = "\\<" + i->str() + "\\>";
         info.category = "class_name";
         if(index_terms.count(info) == 0)
         {
            std::cout << "Indexing class " << info.term << std::endl;
            index_terms.insert(info);
         }
         ++i;
      }
   }

   //
   // Now typedefs:
   //
   {
      static const boost::regex typedef_exp(
         "typedef.+?(\\w+)\\s*;");
      boost::sregex_token_iterator i(text.begin(), text.end(), typedef_exp, 1), j;
      while(i != j)
      {
         index_info info;
         info.term = i->str();
         info.search_text = "\\<" + i->str() + "\\>";
         info.category = "typedef_name";
         if(index_terms.count(info) == 0)
         {
            std::cout << "Indexing typedef " << info.term << std::endl;
            index_terms.insert(info);
         }
         ++i;
      }
   }

   //
   // Now macros:
   //
   {
      static const boost::regex e(
         "^\\s*#\\s*define\\s+(\\w+)"
         );
      boost::sregex_token_iterator i(text.begin(), text.end(), e, 1), j;
      while(i != j)
      {
         index_info info;
         info.term = i->str();
         info.search_text = "\\<" + i->str() + "\\>";
         info.category = "macro_name";
         if(index_terms.count(info) == 0)
         {
            std::cout << "Indexing macro " << info.term << std::endl;
            index_terms.insert(info);
         }
         ++i;
      }
   }
   //
   // Now functions:
   //
   {
      static const boost::regex e(
         "\\w+\\s+(\\w+)\\s*\\([^\\)]*\\)\\s*\\{"
         );
      boost::sregex_token_iterator i(text.begin(), text.end(), e, 1), j;
      while(i != j)
      {
         index_info info;
         info.term = i->str();
         info.search_text = "\\<" + i->str() + "\\>";
         info.category = "function_name";
         if(index_terms.count(info) == 0)
         {
            std::cout << "Indexing function " << info.term << std::endl;
            index_terms.insert(info);
         }
         ++i;
      }
   }
}

void scan_dir(const std::string& dir, const std::string& mask, bool recurse)
{
   using namespace boost::filesystem;
   boost::regex e(mask);
   directory_iterator i(dir), j;

   while(i != j)
   {
      if(regex_match(i->path().filename(), e))
      {
         scan_file(i->path().directory_string().c_str());
      }
      else if(recurse && is_directory(i->status()))
      {
         scan_dir(i->path().directory_string(), mask, recurse);
      }
      ++i;
   }
}

std::string unquote(const std::string& s)
{
   std::string result(s);
   if((s.size() >= 2) && (*s.begin() == '\"') && (*s.rbegin() == '\"'))
   {
      result.erase(result.begin());
      result.erase(--result.end());
   }
   return result;
}

void process_script(const char* script)
{
   static const boost::regex scan_parser(
      "!scan[[:space:]]+"
      "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")"
      );
   static const boost::regex scan_dir_parser(
      "!scan-path[[:space:]]+"
      "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")"
      "[[:space:]]+"
      "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")"
      "(?:"
         "[[:space:]]+"
         "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")"
      ")?"
      );
   static const boost::regex entry_parser( 
      "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")"
      "(?:"
         "[[:space:]]+"
         "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")"
         "(?:"
            "[[:space:]]+"
            "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")"
            "(?:"
               "[[:space:]]+"
               "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")"
            ")?"
         ")?"
      ")?"
      "[[:space:]]*");
   static const boost::regex rewrite_parser(
      "!(rewrite-name|rewrite-id)\\s+"
      "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")\\s+"
      "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")"
      );
   boost::smatch what;
   std::string line;
   std::ifstream is(script);
   if(is.bad())
   {
      std::cerr << "Could not open script " << script << std::endl;
      return;
   }
   while(std::getline(is, line).good())
   {
      if(regex_match(line, what, scan_parser))
      {
         std::string f = unquote(what[1].str());
         scan_file(f.c_str());
      }
      else if(regex_match(line, what, scan_dir_parser))
      {
         std::string d = unquote(what[1].str());
         std::string m = unquote(what[2].str());
         bool r = unquote(what[3].str()) == "true";
         scan_dir(d, m, r);
      }
      else if(regex_match(line, what, rewrite_parser))
      {
         bool id = what[1] == "rewrite-id";
         std::string a = unquote(what[2].str());
         std::string b = unquote(what[3].str());
         id_rewrite_list.push_back(id_rewrite_rule(a, b, id));
      }
      else if(line.compare(0, 9, "!exclude ") == 0)
      {
         static const boost::regex delim("([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")");
         boost::sregex_token_iterator i(line.begin() + 9, line.end(), delim, 0), j;
         while(i != j)
         {
            index_info info;
            info.term = unquote(*i);
            index_terms.erase(info);
            ++i;
         }
      }
      else if(regex_match(line, what, entry_parser))
      {
         // what[1] is the Index entry
         // what[2] is the regex to search for (optional)
         // what[3] is a section id that must be matched 
         // in order for the term to be indexed (optional)
         // what[4] is the index category to place the term in (optional).
         index_info info;
         info.term = unquote(what.str(1));
         std::string s = unquote(what.str(2));
         if(s.size())
            info.search_text = boost::regex(s, boost::regex::icase|boost::regex::perl);
         else
            info.search_text = boost::regex("\\<" + what.str(1) + "\\>", boost::regex::icase|boost::regex::perl);
         if(what[3].matched)
            info.search_id = unquote(what.str(3));
         if(what[4].matched)
            info.category = unquote(what.str(4));
         index_terms.insert(info);
      }
   }
}

std::string get_next_index_id()
{
   static int index_id_count = 0;
   std::stringstream s;
   s << "idx_id_" << index_id_count;
   ++index_id_count;
   return s.str();
}

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
      navbar->name = "simplelist";
      boost::tiny_xml::attribute attr;
      attr.name = "type";
      attr.value = "horiz";
      navbar->attributes.push_back(attr);
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
               id.name = "ID";
               id.value = id_name;
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
               nav->name = "member";
               boost::tiny_xml::element_ptr navlink(new boost::tiny_xml::element());
               navlink->name = "link";
               navlink->content = term->content;
               boost::tiny_xml::attribute navid;
               navid.name = "linkend";
               navid.value = id_name;
               navlink->attributes.push_back(navid);
               nav->elements.push_back(navlink);
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

std::string infile, outfile;

int main(int argc, char* argv[])
{
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

   return 0;
}
