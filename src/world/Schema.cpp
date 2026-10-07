#include "Schema.hpp"
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
void validate(const Json &value, const Json &schema, const Json &common, const std::string &path) {
    if (schema.is_boolean()) {
        require(schema.get<bool>(), path, "field forbidden");
        return;
    }
    if (schema.contains("$ref")) {
        auto ref = schema.at("$ref").get<std::string>();
        auto hash = ref.find('#');
        require(ref.substr(0, hash) == "common.schema.json", path, "unknown schema reference");
        validate(value, common.at(Json::json_pointer(ref.substr(hash + 1))), common, path);
    }
    for (auto keyword : {"allOf", "anyOf"})
        if (schema.contains(keyword)) {
            int matches = 0;
            for (auto &child : schema[keyword])
                try {
                    validate(value, child, common, path);
                    ++matches;
                } catch (const std::runtime_error &) {
                }
            require(std::string(keyword) == "allOf" ? matches == int(schema[keyword].size()) : matches > 0, path,
                    "schema alternatives failed");
        }
    if (schema.contains("const"))
        require(value == schema["const"], path, "unexpected constant");
    if (schema.contains("enum"))
        require(std::find(schema["enum"].begin(), schema["enum"].end(), value) != schema["enum"].end(), path,
                "unknown enum");
    if (schema.contains("type")) {
        std::string type = schema["type"];
        bool ok = (type == "object" && value.is_object()) || (type == "array" && value.is_array()) ||
                  (type == "string" && value.is_string()) || (type == "number" && value.is_number()) ||
                  (type == "integer" && value.is_number_integer()) || (type == "boolean" && value.is_boolean());
        require(ok, path, "incorrect type");
    }
    if (value.is_object()) {
        if (schema.contains("required"))
            for (auto &key : schema["required"])
                require(value.contains(key.get<std::string>()), path, "missing " + key.get<std::string>());
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (schema.contains("propertyNames"))
                validate(Json(it.key()), schema["propertyNames"], common, path);
            if (schema.contains("properties") && schema["properties"].contains(it.key()))
                validate(it.value(), schema["properties"][it.key()], common, path + "/" + it.key());
            else if (schema.contains("additionalProperties"))
                validate(it.value(), schema["additionalProperties"], common, path + "/" + it.key());
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
                validate(value[i], schema["items"], common, path + "/" + std::to_string(i));
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
    }
}
} // namespace
void validateManifest(const Json &value) {
    static const auto common = Json::parse(commonSchema), schema = Json::parse(worldSchema);
    static const auto common03 = Json::parse(commonSchema03), schema03 = Json::parse(worldSchema03);
    if (value.value("formatVersion", "") == "0.3") {
        validate(value, schema03, common03, "world");
        return;
    }
    static const auto common02 = Json::parse(commonSchema02), schema02 = Json::parse(worldSchema02);
    if (value.value("formatVersion", "") == "0.2")
        validate(value, schema02, common02, "world");
    else
        validate(value, schema, common, "world");
}
} // namespace hyprune::world
