
#!/usr/bin/awk -f

function trim(s) {
  sub(/^[[:space:]]+/, "", s)  # 去除开头的空白字符
  sub(/[[:space:]]+$/, "", s)  # 去除结尾的空白字符
  return s
}

BEGIN {
  in_struct = 0
  in_headers = 0
}

{
  if ($0 ~ /#[[:space:]]*include/) {
    in_headers = 1
    print $0
    next
  }

  if (in_headers  && $0 !~ /#[[:space:]]include/) {
    in_headers = 0
    print "#include <boost/hana.hpp>"
    print $0
    next
  }

  if ($0 ~ /struct/) {
    in_struct = 1
    struct_start = NR
    struct_content = ""
    struct_name = $2
  }

  if (in_struct) {
    struct_content = struct_content $0 " "

    if ($0 ~ /}[[:space:]]*;/) {
      in_struct = 0
      if (match(struct_content, /\{[^{}]*/)) {
        tmp = substr(struct_content, RSTART+1, RLENGTH-1)
      }

      split(tmp, arr, ";")
      sep = ""
      print "struct " struct_name " {"
      if (length(arr) > 0) {
        print "  BOOST_HANA_DEFINE_STRUCT(" struct_name ","
        for (i=1; i<length(arr); i++) {
          split(trim(arr[i]), arr0, "[[:space:]]+")
          sep = i == length(arr) - 1 ? ");" : ","
          print "    (" arr0[1] ", " arr0[2] ")" sep
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
