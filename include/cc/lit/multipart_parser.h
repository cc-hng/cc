#pragma once

#include <functional>
#include <map>
#include <regex>
#include <string>
#include <string_view>
#include <vector>
#include <cc/lit/object.h>

#define CPPHTTPLIB_HEADER_MAX_LENGTH 8192

namespace cc {
namespace lit {

namespace detail {

static inline std::string multipart_parse_content_type(std::string_view ct) {
    static std::regex re_content_type(R"(multipart/form-data;\s*boundary=(.*))",
                                      std::regex_constants::icase);

    auto p = static_cast<const char*>(ct.data());
    std::cmatch cmatch;
    if (std::regex_match(p, p + ct.size(), cmatch, re_content_type)) {
        return cmatch[1].str();
    }

    return "";
}

struct MultipartFormData {
    std::string name;
    std::string content;
    std::string filename;
    std::string content_type;
};
using MultipartFormDataItems = std::vector<MultipartFormData>;
using MultipartFormDataMap   = std::multimap<std::string, MultipartFormData>;

using ContentReceiver = std::function<bool(const char* data, size_t data_length)>;

using MultipartContentHeader = std::function<bool(const MultipartFormData& file)>;

namespace detail {
inline bool is_space_or_tab(char c) {
    return c == ' ' || c == '\t';
}

inline std::pair<size_t, size_t> trim(const char* b, const char* e, size_t left, size_t right) {
    while (b + left < e && is_space_or_tab(b[left])) {
        left++;
    }
    while (right > 0 && is_space_or_tab(b[right - 1])) {
        right--;
    }
    return std::make_pair(left, right);
}

inline std::string trim_copy(const std::string& s) {
    auto r = trim(s.data(), s.data() + s.size(), 0, s.size());
    return s.substr(r.first, r.second - r.first);
}
}  // namespace detail

class MultipartFormDataParser {
public:
    MultipartFormDataParser() = default;

    void set_boundary(std::string&& boundary) {
        boundary_           = boundary;
        dash_boundary_crlf_ = dash_ + boundary_ + crlf_;
        crlf_dash_boundary_ = crlf_ + dash_ + boundary_;
    }

    bool is_valid() const { return is_valid_; }

    bool parse(const char* buf, size_t n, const ContentReceiver& content_callback,
               const MultipartContentHeader& header_callback) {
        // TODO: support 'filename*'
        static const std::regex re_content_disposition(
            R"~(^Content-Disposition:\s*form-data;\s*name="(.*?)"(?:;\s*filename="(.*?)")?(?:;\s*filename\*=\S+)?\s*$)~",
            std::regex_constants::icase);

        buf_append(buf, n);

        while (buf_size() > 0) {
            switch (state_) {
            case 0: {  // Initial boundary
                buf_erase(buf_find(dash_boundary_crlf_));
                if (dash_boundary_crlf_.size() > buf_size()) {
                    return true;
                }
                if (!buf_start_with(dash_boundary_crlf_)) {
                    return false;
                }
                buf_erase(dash_boundary_crlf_.size());
                state_ = 1;
                break;
            }
            case 1: {  // New entry
                clear_file_info();
                state_ = 2;
                break;
            }
            case 2: {  // Headers
                auto pos = buf_find(crlf_);
                if (pos > CPPHTTPLIB_HEADER_MAX_LENGTH) {
                    return false;
                }
                while (pos < buf_size()) {
                    // Empty line
                    if (pos == 0) {
                        if (!header_callback(file_)) {
                            is_valid_ = false;
                            return false;
                        }
                        buf_erase(crlf_.size());
                        state_ = 3;
                        break;
                    }

                    static const std::string header_name = "content-type:";
                    const auto header                    = buf_head(pos);
                    if (start_with_case_ignore(header, header_name)) {
                        file_.content_type = detail::trim_copy(header.substr(header_name.size()));
                    } else {
                        std::smatch m;
                        if (std::regex_match(header, m, re_content_disposition)) {
                            file_.name     = m[1];
                            file_.filename = m[2];
                        } else {
                            is_valid_ = false;
                            return false;
                        }
                    }
                    buf_erase(pos + crlf_.size());
                    pos = buf_find(crlf_);
                }
                if (state_ != 3) {
                    return true;
                }
                break;
            }
            case 3: {  // Body
                if (crlf_dash_boundary_.size() > buf_size()) {
                    return true;
                }
                auto pos = buf_find(crlf_dash_boundary_);
                if (pos < buf_size()) {
                    if (!content_callback(buf_data(), pos)) {
                        is_valid_ = false;
                        return false;
                    }
                    buf_erase(pos + crlf_dash_boundary_.size());
                    state_ = 4;
                } else {
                    auto len = buf_size() - crlf_dash_boundary_.size();
                    if (len > 0) {
                        if (!content_callback(buf_data(), len)) {
                            is_valid_ = false;
                            return false;
                        }
                        buf_erase(len);
                    }
                    return true;
                }
                break;
            }
            case 4: {  // Boundary
                if (crlf_.size() > buf_size()) {
                    return true;
                }
                if (buf_start_with(crlf_)) {
                    buf_erase(crlf_.size());
                    state_ = 1;
                } else {
                    if (dash_crlf_.size() > buf_size()) {
                        return true;
                    }
                    if (buf_start_with(dash_crlf_)) {
                        buf_erase(dash_crlf_.size());
                        is_valid_ = true;
                        buf_erase(buf_size());  // Remove epilogue
                    } else {
                        return true;
                    }
                }
                break;
            }
            }
        }

        return true;
    }

private:
    void clear_file_info() {
        file_.name.clear();
        file_.filename.clear();
        file_.content_type.clear();
    }

    bool start_with_case_ignore(const std::string& a, const std::string& b) const {
        if (a.size() < b.size()) {
            return false;
        }
        for (size_t i = 0; i < b.size(); i++) {
            if (::tolower(a[i]) != ::tolower(b[i])) {
                return false;
            }
        }
        return true;
    }

    const std::string dash_      = "--";
    const std::string crlf_      = "\r\n";
    const std::string dash_crlf_ = "--\r\n";
    std::string boundary_;
    std::string dash_boundary_crlf_;
    std::string crlf_dash_boundary_;

    size_t state_  = 0;
    bool is_valid_ = false;
    MultipartFormData file_;

    // Buffer
    bool start_with(const std::string& a, size_t spos, size_t epos, const std::string& b) const {
        if (epos - spos < b.size()) {
            return false;
        }
        for (size_t i = 0; i < b.size(); i++) {
            if (a[i + spos] != b[i]) {
                return false;
            }
        }
        return true;
    }

    size_t buf_size() const { return buf_epos_ - buf_spos_; }

    const char* buf_data() const { return &buf_[buf_spos_]; }

    std::string buf_head(size_t l) const { return buf_.substr(buf_spos_, l); }

    bool buf_start_with(const std::string& s) const {
        return start_with(buf_, buf_spos_, buf_epos_, s);
    }

    size_t buf_find(const std::string& s) const {
        auto c = s.front();

        size_t off = buf_spos_;
        while (off < buf_epos_) {
            auto pos = off;
            while (true) {
                if (pos == buf_epos_) {
                    return buf_size();
                }
                if (buf_[pos] == c) {
                    break;
                }
                pos++;
            }

            auto remaining_size = buf_epos_ - pos;
            if (s.size() > remaining_size) {
                return buf_size();
            }

            if (start_with(buf_, pos, buf_epos_, s)) {
                return pos - buf_spos_;
            }

            off = pos + 1;
        }

        return buf_size();
    }

    void buf_append(const char* data, size_t n) {
        auto remaining_size = buf_size();
        if (remaining_size > 0 && buf_spos_ > 0) {
            for (size_t i = 0; i < remaining_size; i++) {
                buf_[i] = buf_[buf_spos_ + i];
            }
        }
        buf_spos_ = 0;
        buf_epos_ = remaining_size;

        if (remaining_size + n > buf_.size()) {
            buf_.resize(remaining_size + n);
        }

        for (size_t i = 0; i < n; i++) {
            buf_[buf_epos_ + i] = data[i];
        }
        buf_epos_ += n;
    }

    void buf_erase(size_t size) { buf_spos_ += size; }

    std::string buf_;
    size_t buf_spos_ = 0;
    size_t buf_epos_ = 0;
};
}  // namespace detail

struct multipart_formdata_t {
    std::string name;
    std::string filename;
    std::string content_type;
    std::string content;

    static std::vector<multipart_formdata_t> decode(const http_request_t& req) {
        auto content_type = req->at(http::field::content_type);
        auto boundary     = detail::multipart_parse_content_type(content_type);
        if (boundary.empty()) {
            throw std::runtime_error("Error multipart:" + std::string(content_type));
        }

        class MFDP {
        public:
            std::vector<multipart_formdata_t>
            operator()(std::string_view boundary, std::string_view body) {
                parser_.set_boundary(std::string(boundary));
                parser_.parse(body.data(), body.size(),
                              std::bind(&MFDP::content_callback, this, std::placeholders::_1,
                                        std::placeholders::_2),
                              std::bind(&MFDP::header_callback, this, std::placeholders::_1));
                return std::move(result_);
            }

        private:
            bool content_callback(const char* data, size_t data_length) {
                cur_.content = std::string_view(data, data_length);
                result_.emplace_back(std::move(cur_));
                return true;
            }

            bool header_callback(const detail::MultipartFormData& file) {
                cur_.filename     = std::move(file.filename);
                cur_.name         = std::move(file.name);
                cur_.content_type = std::move(file.content_type);
                return true;
            }

        private:
            detail::MultipartFormDataParser parser_;
            multipart_formdata_t cur_;
            std::vector<multipart_formdata_t> result_;
        };

        MFDP mfdp;
        return mfdp(boundary, req->body());
    }

    static void encode(http_request_t& req, const std::vector<multipart_formdata_t>& formdata,
                       const std::string& boundary) {
        std::ostringstream oss;
        for (const auto& part : formdata) {
            // Boundary
            oss << "--" << boundary << "\r\n";

            // Content-Disposition
            oss << "Content-Disposition: form-data; name=\"" << part.name << "\"";
            if (!part.filename.empty()) {
                oss << "; filename=\"" << part.filename << "\"";
            }
            oss << "\r\n";

            // Content-Type
            if (!part.content_type.empty()) {
                oss << "Content-Type: " << part.content_type << "\r\n";
            }

            // Content
            oss << "\r\n" << part.content << "\r\n";
        }

        // Closing boundary
        oss << "--" << boundary << "--\r\n";

        req->method(http::verb::post);
        req->version(11);
        req->set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req->set(http::field::content_type, "multipart/form-data; boundary=" + boundary);
        req->body() = oss.str();
        req->prepare_payload();
    }

    static std::string
    encode(const std::vector<multipart_formdata_t>& formdata, const std::string& boundary) {
        std::ostringstream oss;
        for (const auto& part : formdata) {
            // Boundary
            oss << "--" << boundary << "\r\n";

            // Content-Disposition
            oss << "Content-Disposition: form-data; name=\"" << part.name << "\"";
            if (!part.filename.empty()) {
                oss << "; filename=\"" << part.filename << "\"";
            }
            oss << "\r\n";

            // Content-Type
            if (!part.content_type.empty()) {
                oss << "Content-Type: " << part.content_type << "\r\n";
            }

            // Content
            oss << "\r\n" << part.content << "\r\n";
        }

        // Closing boundary
        oss << "--" << boundary << "--\r\n";
        return oss.str();
    }
};

}  // namespace lit
}  // namespace cc
