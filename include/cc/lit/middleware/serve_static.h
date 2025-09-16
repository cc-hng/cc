#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <cc/lit/mime_types.h>
#include <cc/lit/object.h>
#include <gsl/gsl>
#include <string.h>

namespace cc {
namespace lit {

namespace detail {
// Return a reasonable mime type based on the extension of a file.
inline std::string_view get_mime_type(std::string_view path) {
    static auto get_ext = [](std::string_view path) {
        auto slash_pos = path.find_last_of("/\\");
        auto filename  = (slash_pos == std::string_view::npos) ? path : path.substr(slash_pos + 1);

        auto dot_pos = filename.rfind('.');
        if (dot_pos == std::string_view::npos || dot_pos == 0) {
            return std::string_view{};
        }
        return filename.substr(dot_pos);
    };
    return cc::lit::get_mime_type(get_ext(path));
}
}  // namespace detail

namespace fs = std::filesystem;

class StaticFileProvider {
    std::string mount_point_;
    std::string doc_root_;

    enum { CHUNK_SIZE = 128 * 1024 };

public:
    StaticFileProvider(std::string_view mount_point, std::string_view dir)
      : mount_point_(mount_point)
      , doc_root_(dir) {
        if (!mount_point.empty() && mount_point.back() == '/') {
            mount_point_.pop_back();
        }
    }

    net::awaitable<void>  //
    operator()(const auto& req, auto& resp, const auto& go) {
        auto method = req->method();
        if (GSL_UNLIKELY(method != http::verb::get && method != http::verb::head)) {
            co_return co_await go();
        }

        if (!str_starts_with(req.path, mount_point_)) {
            co_return co_await go();
        }

        auto path = pathcat(doc_root_, req.path.substr(mount_point_.size()));
        if (!fs::exists(path)) {
            co_return co_await go();
        }

        if (str_is_directory(path)) {
            auto path0 = path;
            path.append("index.html");

            if (!fs::exists(path) && fs::is_directory(path0)) {
                resp.set_content(render_directory(mount_point_, doc_root_,
                                                  req.path.substr(mount_point_.size())),
                                 "text/html");
                co_return;
            }
        }

        resp->set(http::field::content_type, detail::get_mime_type(path));
        // Attempt to open the file
        std::ifstream t(path.c_str());

        t.seekg(0, std::ios::end);
        size_t size = t.tellg();
        t.seekg(0, std::ios::beg);
        if (method == http::verb::head) {
            resp.set_content("", detail::get_mime_type(path));
            resp->content_length(size);
        } else {
            uint64_t start = 0, end = size - 1;

            // Send whole file
            if (req->find(http::field::range) == req->end()) {
                resp.set_content(std::string((std::istreambuf_iterator<char>(t)),
                                             std::istreambuf_iterator<char>()),
                                 detail::get_mime_type(path));
                co_return;
            }

            if (parse_range(req.raw[http::field::range], size, start, end)) {
                char buf[64] = {0};
                snprintf(buf, 64, "bytes %lu-%lu/%zu", start, end, size);
                resp->result(http::status::partial_content);
                resp->set(http::field::content_range, std::string(buf));
                resp->body() = read_range(t, start, end);
            } else {
                resp->result(http::status::range_not_satisfiable);
                resp->set(http::field::content_range, "bytes */" + std::to_string(size));
            }
        }
    }

private:
    static std::string read_range(std::ifstream& f, size_t start, size_t end) {
        std::string out;
        if (!f || end < start) {
            return out;
        }

        const size_t len = end - start + 1;
        out.resize(len);

        f.seekg(start);
        f.read(out.data(), len);
        out.resize(f.gcount());  // trim in case EOF is reached earlier
        return out;
    }

    static bool parse_range(std::string_view range, uint64_t size, uint64_t& start, uint64_t& end) {
        static std::regex range_regex(R"(bytes=(\d*)-(\d*))");
        const char* p = range.data();
        std::cmatch match;
        if (std::regex_match(p, p + range.size(), match, range_regex)) {
            try {
                bool exist1 = match[1].length();
                bool exist2 = match[2].length();
                if (!exist1 && !exist2) return false;
                if (exist1) start = std::stoull(match[1]);
                if (exist2) end = std::stoull(match[2]);
                if (!exist1) {
                    start = size - end;
                    end   = size - 1;
                }
                if (!exist2) {
                    end = start + CHUNK_SIZE - 1;
                }
                if (end >= size) end = size - 1;
                if (start > end || start >= size) return false;
            } catch (...) {
                return false;
            }
        }
        return true;
    }

    static inline bool str_starts_with(std::string_view s, std::string_view prefix) {
        const auto size = prefix.size();
        return s.size() >= size && memcmp(s.data(), prefix.data(), size) == 0;
    }

    static bool str_ends_with(std::string_view s, std::string_view prefix) {
        const size_t slen = s.size();
        const size_t plen = prefix.size();
        const char* ps    = s.data();
        return slen >= plen && memcmp(ps + slen - plen, prefix.data(), plen) == 0;
    }

    static inline bool str_is_directory(std::string_view path) {
#ifdef BOOST_MSVC
        return str_ends_with(path, "\\");
#else
        return str_ends_with(path, "/");
#endif
    }

    static std::string pathcat(std::string_view base, std::string_view path) {
        if (base.empty()) return std::string(path);
        std::string res(base);
        char sep = '/';
#ifdef BOOST_MSVC
        sep = '\\';
#endif
        if (res.back() == sep) res.pop_back();
        res.append(path.data(), path.size());
#ifdef BOOST_MSVC
        std::replace(res.begin(), res.end(), '/', '\\');
#endif
        return res;
    }

    static std::string render_directory(std::string_view mount_point, std::string_view doc_root,
                                        std::string_view path) {
        std::string path0 = pathcat(doc_root, path);
        std::ostringstream oss;
        oss << R"(<!DOCTYPE html> <html> <head> </head> <body>)";
        oss << "<h1>Directory: " << doc_root << path << "</h1> <ul>";
        try {
            // 收集并排序目录条目
            std::vector<fs::directory_entry> entries;
            for (const auto& entry : fs::directory_iterator(path0)) {
                entries.push_back(entry);
            }

            // 先显示目录，后显示文件
            std::sort(entries.begin(), entries.end(),
                      [](const fs::directory_entry& a, const fs::directory_entry& b) {
                          if (fs::is_directory(a) != fs::is_directory(b)) {
                              return fs::is_directory(a);
                          }
                          return a.path().filename() < b.path().filename();
                      });

            // 生成列表项
            for (const auto& entry : entries) {
                std::string name = entry.path().filename().string();
                std::string back = fs::is_directory(entry) ? "/" : "";

                oss << "            <li><a href=\"" << mount_point << path << name << back << "\">"
                    << name << back << "</a></li>\n";
            }

        } catch (const std::exception& e) {
            oss << "            <li>Error: " << e.what() << "</li>\n";
        }

        // HTML footer
        oss << R"(        </ul> </body> </html>)";
        return oss.str();
    }
};

}  // namespace lit
}  // namespace cc
