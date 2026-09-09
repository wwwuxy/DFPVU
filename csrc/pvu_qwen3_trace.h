#ifndef PVU_QWEN3_TRACE_H
#define PVU_QWEN3_TRACE_H

#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pvu {

struct Tensor {
  std::vector<size_t> shape;
  std::vector<float> values;
};

struct LinearModuleTrace {
  Tensor input;
  Tensor weight;
  Tensor output;
};

struct Qwen3Trace {
  std::string model;
  std::string prompt;
  size_t layer_index = 0;
  std::vector<uint64_t> input_token_ids;
  std::map<std::string, LinearModuleTrace> modules;
};

struct Qwen3Selection {
  std::string trace_root;
  size_t token_count = 0;
  size_t row_count = 0;
};

namespace qwen3_trace_detail {

struct JsonValue {
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  Type type = Type::kNull;
  bool boolean = false;
  std::string text;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue> object;
};

class JsonParser {
 public:
  explicit JsonParser(const std::string& input) : input_(input) {}

  JsonValue parse() {
    JsonValue value = parse_value();
    skip_space();
    if (position_ != input_.size()) fail("trailing content");
    return value;
  }

 private:
  const std::string& input_;
  size_t position_ = 0;

  [[noreturn]] void fail(const std::string& message) const {
    throw std::runtime_error("metadata.json: invalid JSON at byte " +
                             std::to_string(position_) + ": " + message);
  }

  void skip_space() {
    while (position_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[position_]))) {
      ++position_;
    }
  }

  bool consume(char expected) {
    skip_space();
    if (position_ < input_.size() && input_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  JsonValue parse_value() {
    skip_space();
    if (position_ == input_.size()) fail("expected a value");
    switch (input_[position_]) {
      case '{':
        return parse_object();
      case '[':
        return parse_array();
      case '"': {
        JsonValue value;
        value.type = JsonValue::Type::kString;
        value.text = parse_string();
        return value;
      }
      case 't':
        return parse_literal("true", JsonValue::Type::kBool, true);
      case 'f':
        return parse_literal("false", JsonValue::Type::kBool, false);
      case 'n':
        return parse_literal("null", JsonValue::Type::kNull, false);
      default:
        if (input_[position_] == '-' ||
            std::isdigit(static_cast<unsigned char>(input_[position_]))) {
          return parse_number();
        }
        fail("unexpected character");
    }
  }

  JsonValue parse_object() {
    JsonValue value;
    value.type = JsonValue::Type::kObject;
    ++position_;
    if (consume('}')) return value;
    while (true) {
      skip_space();
      if (position_ == input_.size() || input_[position_] != '"') {
        fail("expected an object key");
      }
      const std::string key = parse_string();
      if (!consume(':')) fail("expected ':' after object key");
      JsonValue child = parse_value();
      if (!value.object.emplace(key, std::move(child)).second) {
        fail("duplicate object key '" + key + "'");
      }
      if (consume('}')) return value;
      if (!consume(',')) fail("expected ',' or '}' in object");
    }
  }

  JsonValue parse_array() {
    JsonValue value;
    value.type = JsonValue::Type::kArray;
    ++position_;
    if (consume(']')) return value;
    while (true) {
      value.array.push_back(parse_value());
      if (consume(']')) return value;
      if (!consume(',')) fail("expected ',' or ']' in array");
    }
  }

  static unsigned hex_digit(char digit) {
    if (digit >= '0' && digit <= '9') return static_cast<unsigned>(digit - '0');
    if (digit >= 'a' && digit <= 'f') return static_cast<unsigned>(digit - 'a' + 10);
    if (digit >= 'A' && digit <= 'F') return static_cast<unsigned>(digit - 'A' + 10);
    return 16;
  }

  uint32_t parse_hex_quad() {
    if (input_.size() - position_ < 4) fail("short Unicode escape");
    uint32_t codepoint = 0;
    for (int index = 0; index < 4; ++index) {
      const unsigned digit = hex_digit(input_[position_++]);
      if (digit == 16) fail("invalid Unicode escape");
      codepoint = codepoint * 16 + digit;
    }
    return codepoint;
  }

  void append_utf8(std::string& result, uint32_t codepoint) {
    if (codepoint <= 0x7F) {
      result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
      result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
      result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
      result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
      result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
      result.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
      result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
  }

  std::string parse_string() {
    ++position_;
    std::string result;
    while (position_ < input_.size()) {
      const unsigned char character =
          static_cast<unsigned char>(input_[position_++]);
      if (character == '"') return result;
      if (character < 0x20) fail("control character in string");
      if (character != '\\') {
        result.push_back(static_cast<char>(character));
        continue;
      }
      if (position_ == input_.size()) fail("unterminated escape");
      const char escape = input_[position_++];
      switch (escape) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'u': {
          uint32_t codepoint = parse_hex_quad();
          if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (input_.size() - position_ < 6 || input_[position_] != '\\' ||
                input_[position_ + 1] != 'u') {
              fail("missing low surrogate");
            }
            position_ += 2;
            const uint32_t low = parse_hex_quad();
            if (low < 0xDC00 || low > 0xDFFF) fail("invalid low surrogate");
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) +
                        (low - 0xDC00);
          } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            fail("unexpected low surrogate");
          }
          append_utf8(result, codepoint);
          break;
        }
        default:
          fail("invalid escape");
      }
    }
    fail("unterminated string");
  }

  JsonValue parse_number() {
    const size_t start = position_;
    if (input_[position_] == '-') ++position_;
    if (position_ == input_.size()) fail("incomplete number");
    if (input_[position_] == '0') {
      ++position_;
    } else if (input_[position_] >= '1' && input_[position_] <= '9') {
      while (position_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        ++position_;
      }
    } else {
      fail("invalid number");
    }
    if (position_ < input_.size() && input_[position_] == '.') {
      ++position_;
      if (position_ == input_.size() ||
          !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        fail("invalid fraction");
      }
      while (position_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        ++position_;
      }
    }
    if (position_ < input_.size() &&
        (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() &&
          (input_[position_] == '+' || input_[position_] == '-')) {
        ++position_;
      }
      if (position_ == input_.size() ||
          !std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        fail("invalid exponent");
      }
      while (position_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        ++position_;
      }
    }
    JsonValue value;
    value.type = JsonValue::Type::kNumber;
    value.text = input_.substr(start, position_ - start);
    return value;
  }

  JsonValue parse_literal(const char* literal, JsonValue::Type type,
                          bool boolean) {
    const size_t length = std::strlen(literal);
    if (input_.compare(position_, length, literal) != 0) {
      fail("invalid literal");
    }
    position_ += length;
    JsonValue value;
    value.type = type;
    value.boolean = boolean;
    return value;
  }
};

inline const JsonValue& require_type(const JsonValue& value,
                                     JsonValue::Type type,
                                     const std::string& context) {
  if (value.type != type) {
    throw std::runtime_error("metadata.json: " + context +
                             " has the wrong JSON type");
  }
  return value;
}

inline const JsonValue& require_member(const JsonValue& object,
                                       const std::string& key,
                                       const std::string& context) {
  require_type(object, JsonValue::Type::kObject, context);
  const auto iterator = object.object.find(key);
  if (iterator == object.object.end()) {
    throw std::runtime_error("metadata.json: missing " + context + "." + key);
  }
  return iterator->second;
}

inline const std::string& require_string(const JsonValue& value,
                                         const std::string& context) {
  return require_type(value, JsonValue::Type::kString, context).text;
}

inline bool require_bool(const JsonValue& value, const std::string& context) {
  return require_type(value, JsonValue::Type::kBool, context).boolean;
}

inline uint64_t require_uint(const JsonValue& value,
                             const std::string& context) {
  const std::string& number =
      require_type(value, JsonValue::Type::kNumber, context).text;
  if (number.empty() || number[0] == '-' ||
      number.find_first_not_of("0123456789") != std::string::npos) {
    throw std::runtime_error("metadata.json: " + context +
                             " must be an unsigned integer");
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(number.c_str(), &end, 10);
  if (errno == ERANGE || end == nullptr || *end != '\0') {
    throw std::runtime_error("metadata.json: " + context +
                             " is outside the supported integer range");
  }
  return static_cast<uint64_t>(parsed);
}

inline size_t checked_size(uint64_t value, const std::string& context) {
  if (value == 0 || value > std::numeric_limits<size_t>::max()) {
    throw std::runtime_error("metadata.json: " + context +
                             " must be a positive size_t value");
  }
  return static_cast<size_t>(value);
}

inline std::vector<size_t> require_matrix_shape(const JsonValue& value,
                                                const std::string& context) {
  const auto& entries =
      require_type(value, JsonValue::Type::kArray, context).array;
  if (entries.size() != 2) {
    throw std::runtime_error("metadata.json: " + context +
                             " must have exactly two dimensions");
  }
  return {
      checked_size(require_uint(entries[0], context + "[0]"), context + "[0]"),
      checked_size(require_uint(entries[1], context + "[1]"), context + "[1]")};
}

inline size_t element_count(const std::vector<size_t>& shape,
                            const std::string& context) {
  size_t count = 1;
  for (const size_t dimension : shape) {
    if (dimension > std::numeric_limits<size_t>::max() / count) {
      throw std::runtime_error("metadata.json: " + context +
                               " element count overflows size_t");
    }
    count *= dimension;
  }
  return count;
}

inline std::string join_path(const std::string& root, const std::string& name) {
  if (root.empty()) throw std::runtime_error("trace root must not be empty");
  return root.back() == '/' ? root + name : root + "/" + name;
}

inline std::string read_text_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error(path + ": cannot open file");
  std::ostringstream stream;
  stream << file.rdbuf();
  if (!file.good() && !file.eof()) {
    throw std::runtime_error(path + ": failed while reading file");
  }
  return stream.str();
}

inline Tensor read_tensor(const std::string& root, const std::string& filename,
                          std::vector<size_t> shape, bool require_finite) {
  const size_t count = element_count(shape, filename);
  if (count > std::numeric_limits<size_t>::max() / sizeof(float)) {
    throw std::runtime_error(filename + ": byte count overflows size_t");
  }
  const size_t expected_bytes = count * sizeof(float);
  const std::string path = join_path(root, filename);
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error(filename + ": cannot open file");
  const std::streampos end = file.tellg();
  if (end < 0 || static_cast<uint64_t>(end) != expected_bytes) {
    const std::string actual =
        end < 0 ? "unknown" : std::to_string(static_cast<uint64_t>(end));
    throw std::runtime_error(filename + ": byte count mismatch: expected " +
                             std::to_string(expected_bytes) + ", got " + actual);
  }
  if (expected_bytes >
      static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
    throw std::runtime_error(filename + ": byte count exceeds stream limit");
  }
  file.seekg(0);
  std::vector<unsigned char> bytes(expected_bytes);
  if (!file.read(reinterpret_cast<char*>(bytes.data()),
                 static_cast<std::streamsize>(expected_bytes))) {
    throw std::runtime_error(filename + ": failed while reading tensor bytes");
  }

  Tensor tensor;
  tensor.shape = std::move(shape);
  tensor.values.reserve(count);
  for (size_t index = 0; index < count; ++index) {
    const size_t offset = index * 4;
    const uint32_t bits = static_cast<uint32_t>(bytes[offset]) |
                          (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
                          (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
                          (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    if (require_finite && !std::isfinite(value)) {
      throw std::runtime_error(filename + ": non-finite value at element " +
                               std::to_string(index));
    }
    tensor.values.push_back(value);
  }
  return tensor;
}

inline size_t parse_positive_argument(const char* argument,
                                      const std::string& name) {
  if (argument == nullptr || *argument == '\0') {
    throw std::runtime_error(name + " must be a positive integer");
  }
  const std::string value(argument);
  if (value.find_first_not_of("0123456789") != std::string::npos ||
      value == "0") {
    throw std::runtime_error(name + " must be a positive integer");
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(argument, &end, 10);
  if (errno == ERANGE || end == nullptr || *end != '\0' || parsed == 0 ||
      parsed > std::numeric_limits<size_t>::max()) {
    throw std::runtime_error(name + " is outside the supported range");
  }
  return static_cast<size_t>(parsed);
}

inline bool is_qwen3_0_6b_model(const std::string& model) {
  static const std::string identity = "Qwen3-0.6B";
  if (model == identity) return true;
  return model.size() > identity.size() &&
         model.compare(model.size() - identity.size(), identity.size(),
                       identity) == 0 &&
         model[model.size() - identity.size() - 1] == '/';
}

}  // namespace qwen3_trace_detail

inline Qwen3Trace load_qwen3_trace(const std::string& root) {
  using namespace qwen3_trace_detail;
  const JsonValue metadata =
      JsonParser(read_text_file(join_path(root, "metadata.json"))).parse();
  require_type(metadata, JsonValue::Type::kObject, "root");

  const uint64_t version =
      require_uint(require_member(metadata, "format_version", "root"),
                   "format_version");
  if (version != 1) {
    throw std::runtime_error("metadata.json: unsupported format_version " +
                             std::to_string(version));
  }

  Qwen3Trace trace;
  trace.model =
      require_string(require_member(metadata, "model", "root"), "model");
  if (trace.model != "fixture" && !is_qwen3_0_6b_model(trace.model)) {
    throw std::runtime_error(
        "metadata.json: production model must identify Qwen3-0.6B");
  }
  trace.prompt =
      require_string(require_member(metadata, "prompt", "root"), "prompt");
  const uint64_t layer_index =
      require_uint(require_member(metadata, "layer_index", "root"),
                   "layer_index");
  if (layer_index != 0) {
    throw std::runtime_error("metadata.json: layer_index must be 0");
  }
  trace.layer_index = 0;

  const auto& token_values =
      require_type(require_member(metadata, "input_token_ids", "root"),
                   JsonValue::Type::kArray, "input_token_ids")
          .array;
  if (token_values.empty()) {
    throw std::runtime_error("metadata.json: input_token_ids must not be empty");
  }
  for (size_t index = 0; index < token_values.size(); ++index) {
    trace.input_token_ids.push_back(
        require_uint(token_values[index], "input_token_ids[" +
                                              std::to_string(index) + "]"));
  }
  const size_t required_tokens = trace.model == "fixture" ? 1 : 16;
  if (trace.input_token_ids.size() != required_tokens) {
    throw std::runtime_error("metadata.json: model '" + trace.model +
                             "' requires exactly " +
                             std::to_string(required_tokens) + " input tokens");
  }

  static const std::array<std::pair<const char*, const char*>, 6>
      expected_modules = {{
          {"self_attn.q_proj", "q_proj"},
          {"self_attn.k_proj", "k_proj"},
          {"self_attn.v_proj", "v_proj"},
          {"mlp.gate_proj", "gate_proj"},
          {"mlp.up_proj", "up_proj"},
          {"mlp.down_proj", "down_proj"},
      }};
  std::map<std::string, std::string> expected_prefixes;
  for (const auto& entry : expected_modules) {
    expected_prefixes.emplace(entry.first, entry.second);
  }
  const std::map<std::string, std::pair<size_t, size_t>>
      production_dimensions = {
          {"q_proj", {1024, 2048}},
          {"k_proj", {1024, 1024}},
          {"v_proj", {1024, 1024}},
          {"gate_proj", {1024, 3072}},
          {"up_proj", {1024, 3072}},
          {"down_proj", {3072, 1024}},
      };

  const auto& module_values =
      require_type(require_member(metadata, "modules", "root"),
                   JsonValue::Type::kArray, "modules")
          .array;
  if (module_values.size() != expected_modules.size()) {
    throw std::runtime_error(
        "metadata.json: modules must contain exactly six entries");
  }

  std::set<std::string> seen_names;
  std::set<std::string> seen_prefixes;
  for (size_t index = 0; index < module_values.size(); ++index) {
    const JsonValue& descriptor = module_values[index];
    const std::string context = "modules[" + std::to_string(index) + "]";
    require_type(descriptor, JsonValue::Type::kObject, context);
    const std::string name = require_string(
        require_member(descriptor, "name", context), context + ".name");
    const std::string prefix = require_string(
        require_member(descriptor, "artifact_prefix", context),
        context + ".artifact_prefix");
    const auto expected = expected_prefixes.find(name);
    if (expected == expected_prefixes.end() || expected->second != prefix) {
      throw std::runtime_error("metadata.json: " + context +
                               " has an unknown module name/prefix pair");
    }
    if (!seen_names.insert(name).second ||
        !seen_prefixes.insert(prefix).second) {
      throw std::runtime_error("metadata.json: duplicate module " + name);
    }
    const std::string dtype = require_string(
        require_member(descriptor, "dtype", context), context + ".dtype");
    if (dtype != "float32-le") {
      throw std::runtime_error("metadata.json: " + context +
                               ".dtype must be float32-le");
    }
    if (require_bool(require_member(descriptor, "has_bias", context),
                     context + ".has_bias")) {
      throw std::runtime_error("metadata.json: " + context +
                               " declares an unsupported bias");
    }

    std::vector<size_t> input_shape = require_matrix_shape(
        require_member(descriptor, "input_shape", context),
        context + ".input_shape");
    std::vector<size_t> weight_shape = require_matrix_shape(
        require_member(descriptor, "weight_shape", context),
        context + ".weight_shape");
    std::vector<size_t> output_shape = require_matrix_shape(
        require_member(descriptor, "output_shape", context),
        context + ".output_shape");
    if (trace.model != "fixture") {
      const auto dimensions = production_dimensions.at(prefix);
      const size_t input_features = dimensions.first;
      const size_t output_rows = dimensions.second;
      if (input_shape != std::vector<size_t>({16, input_features}) ||
          weight_shape !=
              std::vector<size_t>({output_rows, input_features}) ||
          output_shape != std::vector<size_t>({16, output_rows})) {
        throw std::runtime_error(
            "metadata.json: " + prefix +
            " does not match Qwen3-0.6B tensor dimensions");
      }
    }
    if (input_shape[0] != trace.input_token_ids.size() ||
        output_shape[0] != input_shape[0] ||
        output_shape[1] != weight_shape[0] ||
        input_shape[1] != weight_shape[1]) {
      throw std::runtime_error("metadata.json: " + name +
                               " has incompatible tensor shapes");
    }
    if (input_shape[1] % 4 != 0) {
      throw std::runtime_error("metadata.json: " + name +
                               " K dimension must be divisible by four");
    }

    LinearModuleTrace module;
    module.input =
        read_tensor(root, prefix + ".input.f32", std::move(input_shape), true);
    module.weight = read_tensor(root, prefix + ".weight.f32",
                                std::move(weight_shape), true);
    module.output = read_tensor(root, prefix + ".output.f32",
                                std::move(output_shape), false);
    trace.modules.emplace(prefix, std::move(module));
  }

  for (const auto& expected : expected_modules) {
    if (trace.modules.count(expected.second) == 0) {
      throw std::runtime_error("metadata.json: missing required module " +
                               std::string(expected.first));
    }
  }
  return trace;
}

inline Qwen3Selection parse_qwen3_selection(int argc, char** argv) {
  if (argc < 1 || argc > 4) {
    throw std::runtime_error(
        "usage: qwen3-workload [trace-root [token-count [row-count]]]");
  }
  Qwen3Selection selection;
  if (argc >= 2) {
    selection.trace_root = argv[1] == nullptr ? "" : argv[1];
  } else {
    const char* environment_root = std::getenv("QWEN3_TRACE_DIR");
    if (environment_root == nullptr || *environment_root == 0) {
      throw std::runtime_error(
          "QWEN3_TRACE_DIR is required when trace-root is not an argument");
    }
    selection.trace_root = environment_root;
  }
  if (selection.trace_root.empty()) {
    throw std::runtime_error("trace root must not be empty");
  }
  if (argc >= 3) {
    selection.token_count = qwen3_trace_detail::parse_positive_argument(
        argv[2], "token-count");
  }
  if (argc >= 4) {
    selection.row_count = qwen3_trace_detail::parse_positive_argument(
        argv[3], "row-count");
  }

  const Qwen3Trace trace = load_qwen3_trace(selection.trace_root);
  for (const auto& entry : trace.modules) {
    const LinearModuleTrace& module = entry.second;
    if (selection.token_count > module.input.shape[0]) {
      throw std::runtime_error("token-count exceeds " + entry.first +
                               " metadata dimension");
    }
    if (selection.row_count > module.weight.shape[0]) {
      throw std::runtime_error("row-count exceeds " + entry.first +
                               " metadata dimension");
    }
  }
  return selection;
}

}  // namespace pvu

#endif  // PVU_QWEN3_TRACE_H
