#pragma once

#include <list>
#include <map>
#include <string>
#include <cstring>
#include <iostream>
#include <algorithm>

namespace edn { 
  
  enum TokenType {
    TokenString,
    TokenAtom,
    TokenParen
  };

  struct EdnToken {
    TokenType type;
    int line;
    int column;
    std::string value;
  };

  enum NodeType {
    EdnNil,
    EdnSymbol,
    EdnKeyword,
    EdnBool,
    EdnInt,
    EdnFloat,
    EdnString, 
    EdnChar, 

    EdnList,
    EdnVector,
    EdnMap,
    EdnSet,

    EdnDiscard,
    EdnTagged
  };

  struct EdnNode {
    NodeType type;
    int line;
    int column;
    std::string value;
    std::list<EdnNode> values;
    std::map<std::string, std::string> metadata;

    std::string pprint();
  };

  EdnNode read(std::string edn);
  std::string pprint(EdnNode &node, int indent = 1, bool multiline = true);
}