#!/usr/bin/awk -f

function trim(s) {
  sub(/^[ \t]+/, "", s)
  sub(/[ \t]+$/, "", s)
  return s
}

BEGIN {
  in_struct = 0
  in_headers = 0
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
    sub("//.*", "")
    struct_content = struct_content $0 " "

    if ($0 ~ /}[ \t]*;/) {
      in_struct = 0
      if (match(struct_content, /{[^{}]*/)) {
        tmp = substr(struct_content, RSTART+1, RLENGTH-1)
      }

      n = split(tmp, arr, ";")
      sep = ""
      print "struct " struct_name " {"
      if (n > 0) {
        print "  BOOST_HANA_DEFINE_STRUCT(" struct_name ","
        for (i=1; i<n; i++) {
          line = trim(arr[i])
          num_words = split(line, words)
          type = words[1]
          for (j=2; j<=num_words; j++) {
            var = words[j]
            gsub(/,/, "", var)  # Remove commas
            if (var != "") {
              sep = (i == n-1 && j == num_words) ? ");" : ","
              print "    (" type ", " var ")" sep
            }
          }
        }
      } else {
        print "  BOOST_HANA_DEFINE_STRUCT(" struct_name ");"
      }
      print "};"
    }
  } else {
    print $0
  }
}
