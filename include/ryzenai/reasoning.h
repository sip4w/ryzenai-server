#pragma once

#include <string>
#include <utility>

namespace ryzenai {

// Result of parsing reasoning content from model output
struct ReasoningParseResult {
    std::string reasoning_content;  // Content inside <think> tags (without the tags)
    std::string regular_content;     // Content outside <think> tags
    bool has_reasoning;              // True if reasoning content was found
    bool is_thinking;                // True if still inside unclosed <think> tag
};

// Parse reasoning content from model output
// Extracts content between <think> and </think> tags
// If only </think> is found (no opening tag), treats everything before it as reasoning
ReasoningParseResult parseReasoningContent(const std::string& text, bool gpt_oss = false);

class GptOssStreamParser {
public:
    std::pair<std::string, std::string> processToken(const std::string& token);
    std::pair<std::string, std::string> flush();
    bool isThinking() const { return state_ == State::Analysis; }
    void reset();

private:
    enum class State { Final, Analysis, Header, Channel, Other, Done };
    State state_ = State::Final;
    std::string buffer_;
    std::string channel_;
    std::pair<std::string, std::string> drain(bool final);
};

// For streaming: tracks state across multiple token callbacks
class ReasoningStreamParser {
public:
    explicit ReasoningStreamParser(bool gpt_oss = false);
    
    // Process a single token
    // Returns: {reasoning_content, regular_content}
    // reasoning_content is non-empty if the token is part of thinking
    // regular_content is non-empty if the token is regular content
    std::pair<std::string, std::string> processToken(const std::string& token);
    
    // Flush any remaining buffer content (call on final token)
    // Returns: {reasoning_content, regular_content}
    std::pair<std::string, std::string> flush();
    
    // Check if currently inside a <think> block
    bool isThinking() const { return gpt_oss_ ? gpt_parser_.isThinking() : in_thinking_; }
    
    // Get accumulated buffer (for detecting tags split across tokens)
    const std::string& getBuffer() const { return buffer_; }
    
    // Reset the parser state
    void reset();
    
private:
    bool in_thinking_;       // True if currently inside <think> tags
    bool gpt_oss_;
    GptOssStreamParser gpt_parser_;
    std::string buffer_;     // Buffer for detecting tags split across tokens
    
    // Check if buffer contains opening tag
    bool containsOpenTag(const std::string& text) const;
    
    // Check if buffer contains closing tag
    bool containsCloseTag(const std::string& text) const;
    
    // Find and process tags in the current buffer
    std::pair<std::string, std::string> processTags();
};

} // namespace ryzenai

