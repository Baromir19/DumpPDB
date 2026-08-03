#pragma once

#include <string>
#include <map>
#include <fstream>
#include <filesystem>
#include <optional>

class IniFile
{
public:

    using Section = std::string;
    using Key = std::string;

    bool load(const std::filesystem::path& a_path)
    {
        std::ifstream file(a_path);
        if (!file.is_open())
            return false;

        m_data.clear();
        std::string line;
        std::string currentSection;

        while (std::getline(file, line))
        {
            trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#')
                continue;

            if (line.front() == '[' && line.back() == ']')
            {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }

            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            trim(key);
            trim(value);

            m_data[currentSection][key] = value;
        }
        return true;
    }

    bool save(const std::filesystem::path& a_path) const
    {
        std::ofstream file(a_path, std::ios::trunc);
        if (!file.is_open())
            return false;

        for (const auto& [section, keys] : m_data)
        {
            file << '[' << section << "]\n";
            for (const auto& [key, value] : keys)
                file << key << '=' << value << '\n';
            file << '\n';
        }
        return true;
    }

    bool getBool(const Section& s, const Key& k, bool a_default) const
    {
        const auto raw = getRaw(s, k);
        if (!raw)
            return a_default;
        return *raw == "1" || *raw == "true" || *raw == "True";
    }

    long getLong(const Section& s, const Key& k, long a_default) const
    {
        const auto raw = getRaw(s, k);
        if (!raw)
            return a_default;
        try
        {
            return std::stol(*raw);
        }
        catch (...)
        {
            return a_default;
        }
    }

    unsigned long getUlong(const Section& s, const Key& k, unsigned long a_default) const
    {
        const auto raw = getRaw(s, k);
        if (!raw)
            return a_default;
        try
        {
            return std::stoul(*raw);
        }
        catch (...)
        {
            return a_default;
        }
    }

    std::string getString(const Section& s, const Key& k, const std::string& a_default) const
    {
        const auto raw = getRaw(s, k);
        return raw ? *raw : a_default;
    }

    void set(const Section& s, const Key& k, bool a_value)
    {
        m_data[s][k] = a_value ? "true" : "false";
    }
    void set(const Section& s, const Key& k, long a_value)
    {
        m_data[s][k] = std::to_string(a_value);
    }
    void set(const Section& s, const Key& k, unsigned long a_value)
    {
        m_data[s][k] = std::to_string(a_value);
    }
    void set(const Section& s, const Key& k, const std::string& a_value)
    {
        m_data[s][k] = a_value;
    }

private:

    std::optional<std::string> getRaw(const Section& s, const Key& k) const
    {
        const auto sectionIt = m_data.find(s);
        if (sectionIt == m_data.end())
            return std::nullopt;

        const auto keyIt = sectionIt->second.find(k);
        if (keyIt == sectionIt->second.end())
            return std::nullopt;

        return keyIt->second;
    }

    static void trim(std::string& a_str)
    {
        const auto first = a_str.find_first_not_of(" \t\r\n");
        const auto last = a_str.find_last_not_of(" \t\r\n");
        a_str = (first == std::string::npos) ? "" : a_str.substr(first, last - first + 1);
    }

    std::map<Section, std::map<Key, std::string>> m_data;
};