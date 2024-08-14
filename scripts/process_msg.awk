#!/usr/bin/awk -f

function trim(s) {
  sub(/^[ \t]+/, "", s)
  sub(/[ \t]+$/, "", s)
  return s
}

function process_member(member,    type, vars, i, var, result, in_template, depth) {
  member = trim(member)
  sub(/;$/, "", member)  # Remove trailing semicolon
  in_template = 0
  depth = 0
  type = ""
  vars = ""
  
  for (i = 1; i <= length(member); i++) {
    c = substr(member, i, 1)
    if (c == "<") {
      in_template++
      depth++
    } else if (c == ">") {
      depth--
      if (depth == 0) in_template = 0
    }
    
    if (c == " " && !in_template && type == "") {
      type = trim(substr(member, 1, i-1))
      vars = trim(substr(member, i+1))
      break
    }
  }
  
  if (type == "") {
    type = member
  }
  
  if (vars == "") {
    return "    (" type ", " type ")"
  }
  
  split(vars, var_arr, ",")
  result = ""
  for (i in var_arr) {
    var = trim(var_arr[i])
    if (var != "") {
      if (result != "") result = result ",\n"
      result = result "    (" type ", " var ")"
    }
  }
  return result
}

BEGIN {
  in_struct = 0
  template_depth = 0
}

{
  if ($0 ~ /#[ \t]*pragma.*once/) {
    print $0
    print "\n#include <boost/hana.hpp>\n"
    next
  }

  if ($0 ~ /struct/) {
    in_struct = 1
    struct_start = NR
    struct_content = ""
    struct_name = $2
  }

  if (in_struct) {
    gsub(/\/\/.*$/, "")  # Remove inline comments
    line = $0
    while (match(line, /[<>]/)) {
      c = substr(line, RSTART, 1)
      template_depth += (c == "<") ? 1 : -1
      line = substr(line, RSTART+1)
    }
    
    struct_content = struct_content $0 "\n"

    if (template_depth == 0 && $0 ~ /}[ \t]*;/) {
      in_struct = 0
      if (match(struct_content, /{[^}]*}/)) {
        tmp = substr(struct_content, RSTART+1, RLENGTH-2)
      }

      n = split(tmp, arr, "\n")
      print "struct " struct_name " {"
      if (n > 0) {
        print "  BOOST_HANA_DEFINE_STRUCT(" struct_name ","
        for (i=1; i<=n; i++) {
          if (trim(arr[i]) == "") continue
          member = process_member(arr[i])
          if (member != "") {
            sep = (i == n || i == n-1 && trim(arr[n]) == "") ? "" : ","
            print member sep
          }
        }
        print "  );"
      } else {
        print "  BOOST_HANA_DEFINE_STRUCT(" struct_name ");"
      }
      print "};"
    }
  } else {
    print $0
  }
}
