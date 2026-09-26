// settings.cpp - plain text key/value persistence
#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace {

// Keep every value on a single line.
std::string escape(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

std::string unescape(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            char n = in[++i];
            switch (n) {
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case '\\': out += '\\'; break;
                default:   out += n;    break;
            }
        } else {
            out += in[i];
        }
    }
    return out;
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

Settings::Settings(std::string path) : m_path(std::move(path)) {}

std::string *Settings::find(const std::string &key) {
    for (auto &kv : m_items)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

const std::string *Settings::find(const std::string &key) const {
    for (const auto &kv : m_items)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

void Settings::load() {
    m_items.clear();
    m_error.clear();

    std::ifstream in(m_path);
    if (!in) return;   // first run: no file yet

    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        if (key.empty()) continue;
        m_items.emplace_back(key, unescape(trim(t.substr(eq + 1))));
    }
}

bool Settings::save() {
    m_error.clear();

    std::ofstream out(m_path, std::ios::trunc);
    if (!out) {
        m_error = "cannot write " + m_path;
        return false;
    }

    out << "# Image-ncnn-Vulkan-UI settings - written automatically, safe to edit\n";
    for (const auto &kv : m_items)
        out << kv.first << " = " << escape(kv.second) << "\n";

    if (!out) {
        m_error = "write failed: " + m_path;
        return false;
    }
    return true;
}

std::string Settings::get(const std::string &key, const std::string &fallback) const {
    const std::string *v = find(key);
    return v ? *v : fallback;
}

int Settings::get_int(const std::string &key, int fallback) const {
    const std::string *v = find(key);
    return v ? atoi(v->c_str()) : fallback;
}

double Settings::get_double(const std::string &key, double fallback) const {
    const std::string *v = find(key);
    return v ? atof(v->c_str()) : fallback;
}

bool Settings::get_bool(const std::string &key, bool fallback) const {
    const std::string *v = find(key);
    if (!v) return fallback;
    return *v == "1" || *v == "true" || *v == "yes";
}

void Settings::set(const std::string &key, const std::string &value) {
    if (std::string *slot = find(key)) *slot = value;
    else m_items.emplace_back(key, value);
}

int Settings::remove_prefix(const std::string &prefix) {
    int removed = 0;
    for (auto it = m_items.begin(); it != m_items.end(); ) {
        if (it->first.rfind(prefix, 0) == 0) {
            it = m_items.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void Settings::set_int(const std::string &key, int value) {
    set(key, std::to_string(value));
}

void Settings::set_double(const std::string &key, double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    set(key, buf);
}

void Settings::set_bool(const std::string &key, bool value) {
    set(key, value ? "1" : "0");
}
