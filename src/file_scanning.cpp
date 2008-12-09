// Copyright 2008 John Maddock
//
// Use, modification and distribution are subject to the
// Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "auto_index.hpp"

//
// Helper to dump file contents into a std::string:
//
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
//
// Scan a source file for things to index:
//
void scan_file(const char* file)
{
   if(verbose)
      std::cout << "Scanning file... " << file << std::endl;
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
      if(verbose)
         std::cout << "Scanning for class names... " << std::endl;
      boost::sregex_token_iterator i(text.begin(), text.end(), class_e, 5), j;
      while(i != j)
      {
         index_info info;
         info.term = i->str();
         info.search_text = "\\<" + i->str() + "\\>";
         info.category = "class_name";
         if(index_terms.count(info) == 0)
         {
            if(verbose)
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
      if(verbose)
         std::cout << "Scanning for typedef names... " << std::endl;
      static const boost::regex typedef_exp(
         "typedef[^;{}#]+?(\\w+)\\s*;");
      boost::sregex_token_iterator i(text.begin(), text.end(), typedef_exp, 1), j;
      while(i != j)
      {
         index_info info;
         info.term = i->str();
         info.search_text = "\\<" + i->str() + "\\>";
         info.category = "typedef_name";
         if(index_terms.count(info) == 0)
         {
            if(verbose)
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
      if(verbose)
         std::cout << "Scanning for macro names... " << std::endl;
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
            if(verbose)
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
      if(verbose)
         std::cout << "Scanning for function names... " << std::endl;
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
            if(verbose)
               std::cout << "Indexing function " << info.term << std::endl;
            index_terms.insert(info);
         }
         ++i;
      }
   }
}
//
// Scan a whole directory for files to search:
//
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
//
// Remove quotes from a string:
//
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
//
// Load and process a script file:
//
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
         "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)*\")"
         "(?:"
            "[[:space:]]+"
            "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)*\")"
            "(?:"
               "[[:space:]]+"
               "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)*\")"
            ")?"
         ")?"
      ")?"
      "[[:space:]]*");
   static const boost::regex rewrite_parser(
      "!(rewrite-name|rewrite-id)\\s+"
      "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")\\s+"
      "([^\"[:space:]]+|\"(?:[^\"\\\\]|\\\\.)+\")"
      );
   if(verbose)
      std::cout << "Processing script " << script << std::endl;
   boost::smatch what;
   std::string line;
   std::ifstream is(script);
   if(is.bad())
   {
      throw std::runtime_error("Could not open script file");
   }
   while(std::getline(is, line).good())
   {
      if(regex_match(line, what, scan_parser))
      {
         std::string f = unquote(what[1].str());
         boost::filesystem::path base(script);
         base.remove_filename();
         base /= f;
         f = base.file_string();
         scan_file(f.c_str());
      }
      else if(regex_match(line, what, scan_dir_parser))
      {
         std::string d = unquote(what[1].str());
         std::string m = unquote(what[2].str());
         bool r = unquote(what[3].str()) == "true";
         boost::filesystem::path base(script);
         base.remove_filename();
         base /= d;
         d = base.directory_string();
         if(verbose)
            std::cout << "Scanning directory " << d << std::endl;
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

         s = unquote(what.str(3));
         if(s.size())
            info.search_id = s;
         if(what[4].matched)
            info.category = unquote(what.str(4));
         index_terms.insert(info);
      }
   }
}

