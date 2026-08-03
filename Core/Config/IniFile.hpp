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

    [[nodiscard]]
    bool load(const std::filesystem::path& a_path)
    {
        std::ifstream file(a_path);
        if (!file.is_open())
        {
            return false;
        }

        m_data.clear();
        std::string line;
        std::string currentSection;

        while (std::getline(file, line))
        {
            trim(line);
            if (line.empty() || line.front() == ';' || line.front() == '#')
            {
                continue;
            }

            if (line.front() == '[' && line.back() == ']')
            {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }

            const auto eqPos = line.find('=');
            if (eqPos == std::string::npos)
            {
                continue;
            }

            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
            trim(key);
            trim(value);

            m_data[currentSection][key] = value;
        }
        return true;
    }

    [[nodiscard]]
    bool save(const std::filesystem::path& a_path) const
    {
        std::ofstream file(a_path, std::ios::trunc);
        if (!file.is_open())
        {
            return false;
        }

        for (const auto& [section, keys] : m_data)
        {
            file << '[' << section << "]\n";
            for (const auto& [key, value] : keys)
            {
                file << key << '=' << value << '\n';
            }
            file << '\n';
        }
        return true;
    }

    [[nodiscard]]
    bool getBool(const Section& a_section, const Key& a_key, bool a_default) const
    {
        const auto raw = getRaw(a_section, a_key);
        if (!raw)
        {
            return a_default;
        }
        return *raw == "1" || *raw == "true" || *raw == "True";
    }

    [[nodiscard]]
    long getLong(const Section& a_section, const Key& a_key, long a_default) const
    {
        const auto raw = getRaw(a_section, a_key);
        if (!raw)
        {
            return a_default;
        }
        try
        {
            return std::stol(*raw);
        }
        catch (...)
        {
            return a_default;
        }
    }

    [[nodiscard]]
    unsigned long getUlong(
        const Section& a_section, const Key& a_key, unsigned long a_default) const
    {
        const auto raw = getRaw(a_section, a_key);
        if (!raw)
        {
            return a_default;
        }
        try
        {
            return std::stoul(*raw);
        }
        catch (...)
        {
            return a_default;
        }
    }

    [[nodiscard]]
    std::string getString(
        const Section& a_section, const Key& a_key, const std::string& a_default) const
    {
        const auto raw = getRaw(a_section, a_key);
        return raw ? *raw : a_default;
    }

    void set(const Section& a_section, const Key& a_key, bool a_value)
    {
        m_data[a_section][a_key] = a_value ? "true" : "false";
    }

    void set(const Section& a_section, const Key& a_key, long a_value)
    {
        m_data[a_section][a_key] = std::to_string(a_value);
    }

    void set(const Section& a_section, const Key& a_key, unsigned long a_value)
    {
        m_data[a_section][a_key] = std::to_string(a_value);
    }

    void set(const Section& a_section, const Key& a_key, const std::string& a_value)
    {
        m_data[a_section][a_key] = a_value;
    }

private:

    [[nodiscard]]
    std::optional<std::string> getRaw(const Section& a_section, const Key& a_key) const
    {
        const auto sectionIt = m_data.find(a_section);
        if (sectionIt == m_data.end())
        {
            return std::nullopt;
        }

        const auto keyIt = sectionIt->second.find(a_key);
        if (keyIt == sectionIt->second.end())
        {
            return std::nullopt;
        }

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