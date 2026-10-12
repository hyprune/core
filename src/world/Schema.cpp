#include "Schema.hpp"
#include <cmath>
#include <map>
#include <mutex>
#include "SchemaData.hpp"
#include <regex>
#include <stdexcept>
namespace hyprune::world {
namespace {
using Json = nlohmann::json;
void require(bool ok, const std::string &path, const std::string &why) {
    if (!ok)
        throw std::runtime_error(path + ": " + why);
}
// root: the document local "#/..." references resolve in (object and library schemas);
// common: the shared definitions "common.schema.json#/..." (also "../v0.N/common...") name.
void validate(const Json &value, const Json &schema, const Json &common, const std::string &path,
              const Json *root = nullptr) {
    if (schema.is_boolean()) {
        require(schema.get<bool>(), path, "field forbidden");
        return;
    }
    auto recurse = [&](const Json &v, const Json &s, const std::string &p) { validate(v, s, common, p, root); };
    if (schema.contains("$ref")) {
        auto ref = schema.at("$ref").get<std::string>();
        auto hash = ref.find('#');
        auto document = ref.substr(0, hash);
        if (document.empty() && root)
            recurse(value, root->at(Json::json_pointer(ref.substr(hash + 1))), path);
        else {
            require(document == "common.schema.json" || (root && document.ends_with("/common.schema.json")), path,
                    "unknown schema reference");
            validate(value, common.at(Json::json_pointer(ref.substr(hash + 1))), common, path,
                     root ? &common : nullptr);
        }
    }
    for (auto keyword : {"allOf", "anyOf", "oneOf"})
        if (schema.contains(keyword)) {
            int matches = 0;
            std::string first;
            for (auto &child : schema[keyword])
                try {
                    recurse(value, child, path);
                    ++matches;
                } catch (const std::runtime_error &e) {
                    if (first.empty())
                        first = e.what();
                }
            std::string k = keyword;
            if (k == "allOf" && matches != int(schema[keyword].size()))
                throw std::runtime_error(first);
            require(k == "allOf" || (k == "anyOf" ? matches > 0 : matches == 1), path,
                    k == "oneOf" && matches > 1 ? "matches more than one alternative" : "schema alternatives failed");
        }
    if (schema.contains("not")) {
        bool matched = true;
        try {
            recurse(value, schema["not"], path);
        } catch (const std::runtime_error &) {
            matched = false;
        }
        require(!matched, path, "field forbidden");
    }
    if (schema.contains("if")) {
        bool holds = true;
        try {
            recurse(value, schema["if"], path);
        } catch (const std::runtime_error &) {
            holds = false;
        }
        if (holds && schema.contains("then"))
            recurse(value, schema["then"], path);
        if (!holds && schema.contains("else"))
            recurse(value, schema["else"], path);
    }
    if (schema.contains("const"))
        require(value == schema["const"], path, "unexpected constant");
    if (schema.contains("enum"))
        require(std::find(schema["enum"].begin(), schema["enum"].end(), value) != schema["enum"].end(), path,
                "unknown enum");
    if (schema.contains("type")) {
        auto is = [&](const std::string &type) {
            return (type == "object" && value.is_object()) || (type == "array" && value.is_array()) ||
                   (type == "string" && value.is_string()) || (type == "number" && value.is_number()) ||
                   (type == "integer" && (value.is_number_integer() ||
                                          (value.is_number_float() && std::floor(value.get<double>()) == value.get<double>()))) ||
                   (type == "boolean" && value.is_boolean()) || (type == "null" && value.is_null());
        };
        bool ok = false;
        if (schema["type"].is_array())
            for (auto &t : schema["type"])
                ok = ok || is(t.get<std::string>());
        else
            ok = is(schema["type"].get<std::string>());
        require(ok, path, "incorrect type");
    }
    if (value.is_object()) {
        if (schema.contains("required"))
            for (auto &key : schema["required"])
                require(value.contains(key.get<std::string>()), path, "missing " + key.get<std::string>());
        if (schema.contains("minProperties"))
            require(value.size() >= schema["minProperties"].get<size_t>(), path, "too few properties");
        if (schema.contains("maxProperties"))
            require(value.size() <= schema["maxProperties"].get<size_t>(), path, "too many properties");
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (schema.contains("propertyNames"))
                recurse(Json(it.key()), schema["propertyNames"], path + "/" + it.key());
            if (schema.contains("properties") && schema["properties"].contains(it.key()))
                recurse(it.value(), schema["properties"][it.key()], path + "/" + it.key());
            else if (schema.contains("additionalProperties"))
                recurse(it.value(), schema["additionalProperties"], path + "/" + it.key());
        }
    }
    if (value.is_array()) {
        if (schema.contains("minItems"))
            require(value.size() >= schema["minItems"].get<size_t>(), path, "too few items");
        if (schema.contains("maxItems"))
            require(value.size() <= schema["maxItems"].get<size_t>(), path, "too many items");
        if (schema.value("uniqueItems", false))
            for (size_t i = 0; i < value.size(); i++)
                for (size_t j = 0; j < i; j++)
                    require(value[i] != value[j], path, "duplicate array item");
        if (schema.contains("items"))
            for (size_t i = 0; i < value.size(); i++)
                recurse(value[i], schema["items"], path + "/" + std::to_string(i));
    }
    if (value.is_string()) {
        auto text = value.get<std::string>();
        size_t chars = 0;
        for (unsigned char c : text)
            if ((c & 0xc0) != 0x80)
                ++chars;
        if (schema.contains("minLength"))
            require(chars >= schema["minLength"].get<size_t>(), path, "string too short");
        if (schema.contains("maxLength"))
            require(chars <= schema["maxLength"].get<size_t>(), path, "string too long");
        if (schema.contains("pattern"))
            require(std::regex_search(text, std::regex(schema["pattern"].get<std::string>())), path,
                    "invalid string pattern");
    }
    if (value.is_number()) {
        double n = value.get<double>();
        require(std::isfinite(n), path, "nonfinite number");
        if (schema.contains("minimum"))
            require(n >= schema["minimum"].get<double>(), path, "below minimum");
        if (schema.contains("exclusiveMinimum"))
            require(n > schema["exclusiveMinimum"].get<double>(), path, "below exclusive minimum");
        if (schema.contains("maximum"))
            require(n <= schema["maximum"].get<double>(), path, "above maximum");
        if (schema.contains("exclusiveMaximum"))
            require(n < schema["exclusiveMaximum"].get<double>(), path, "above exclusive maximum");
    }
}
} // namespace
void validateObjectDocument(const Json &value, const std::string &kind, const std::string &version) {
    struct Pinned {
        const char *version, *object, *library;
    };
    static constexpr Pinned pinned[]{{"0.9", objectSchema09, librarySchema09}, {"0.10", objectSchema010, librarySchema010}};
    static std::map<std::string, Json> parsed;
    static std::mutex lock;
    static const Json common = Json::parse(commonSchema08);
    for (auto &p : pinned)
        if (version == p.version) {
            auto key = version + ":" + kind;
            std::unique_lock guard(lock);
            auto it = parsed.find(key);
            if (it == parsed.end())
                it = parsed.emplace(key, Json::parse(kind == "object" ? p.object : p.library)).first;
            guard.unlock();
            validate(value, it->second, common, "", &it->second);
            return;
        }
    throw std::runtime_error("unknown " + kind + " format " + version);
}
void validateManifest(const Json &value) {
    // Each pinned world format validates with its own schema (and common defs).
    struct Pinned {
        const char *version, *world, *common;
    };
    static constexpr Pinned pinned[]{{"0.8", worldSchema08, commonSchema08}, {"0.7", worldSchema07, commonSchema07}, {"0.6", worldSchema06, commonSchema06},
                                     {"0.5", worldSchema05, commonSchema04}, {"0.4", worldSchema04, commonSchema04},
                                     {"0.3", worldSchema03, commonSchema03}, {"0.2", worldSchema02, commonSchema02}};
    static std::map<std::string, std::pair<Json, Json>> parsed;
    static std::mutex lock;
    auto version = value.value("formatVersion", "");
    for (auto &p : pinned)
        if (version == p.version) {
            std::unique_lock guard(lock);
            auto it = parsed.find(version);
            if (it == parsed.end())
                it = parsed.emplace(version, std::pair{Json::parse(p.world), Json::parse(p.common)}).first;
            guard.unlock();
            validate(value, it->second.first, it->second.second, "world");
            return;
        }
    static const auto common = Json::parse(commonSchema), schema = Json::parse(worldSchema);
    validate(value, schema, common, "world");
}
} // namespace hyprune::world
