// settings.h - flat key/value settings persisted next to the executable
#pragma once

#include <string>
#include <utility>
#include <vector>

// A plain-text "key = value" store. The file stays human readable and editable
// on purpose; backslashes and newlines in values survive a round trip.
class Settings {
public:
    explicit Settings(std::string path);

    // Read the file when it exists. A missing file is not an error: the
    // defaults supplied by the caller stay in effect.
    void load();

    // Overwrite the file. Returns false and fills error() when it cannot be
    // written (for example when the application directory is read-only).
    bool save();

    const std::string &path() const { return m_path; }
    const std::string &error() const { return m_error; }

    std::string get(const std::string &key, const std::string &fallback = "") const;
    int         get_int(const std::string &key, int fallback) const;
    double      get_double(const std::string &key, double fallback) const;
    bool        get_bool(const std::string &key, bool fallback) const;
    // Whether the key is present at all. `key = ` (present but empty) and a key
    // that was never written are different things for values where empty is a
    // meaningful setting, such as -l in Z-Image (empty means auto).
    bool        has(const std::string &key) const { return find(key) != nullptr; }

    void set(const std::string &key, const std::string &value);
    void set_int(const std::string &key, int value);
    void set_double(const std::string &key, double value);
    void set_bool(const std::string &key, bool value);

    // Drop every key starting with `prefix` and return how many were removed.
    // Deleting a named preset must remove all of its keys: leaving them behind
    // would resurrect the preset on the next load.
    int remove_prefix(const std::string &prefix);

private:
    std::string m_path;
    std::string m_error;
    std::vector<std::pair<std::string, std::string>> m_items;

    std::string *find(const std::string &key);
    const std::string *find(const std::string &key) const;
};
