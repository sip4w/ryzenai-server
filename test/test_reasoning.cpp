#include "ryzenai/reasoning.h"

#include <cstdlib>
#include <string>

using ryzenai::ReasoningStreamParser;
using ryzenai::parseReasoningContent;

static void expect(bool condition) {
    if (!condition) std::abort();
}

static void checkStream(const std::string& raw, const std::string& reasoning,
                        const std::string& content) {
    for (size_t chunk_size = 1; chunk_size <= raw.size(); ++chunk_size) {
        ReasoningStreamParser parser(true);
        std::string streamed_reasoning;
        std::string streamed_content;
        for (size_t pos = 0; pos < raw.size(); pos += chunk_size) {
            auto part = parser.processToken(raw.substr(pos, chunk_size));
            streamed_reasoning += part.first;
            streamed_content += part.second;
        }
        auto tail = parser.flush();
        streamed_reasoning += tail.first;
        streamed_content += tail.second;
        expect(streamed_reasoning == reasoning);
        expect(streamed_content == content);
    }

    auto parsed = parseReasoningContent(raw, true);
    expect(parsed.reasoning_content == reasoning);
    expect(parsed.regular_content == content);
    expect(parsed.has_reasoning == !reasoning.empty());
}

int main() {
    checkStream("<|channel|>analysis<|message|>thinking<|end|>"
                "<|start|>assistant<|channel|>final<|message|>4<|return|>",
                "thinking", "4");
    checkStream("<|channel|>final<|message|>Hello<|return|>", "", "Hello");
    checkStream("<|start|>assistant<|channel|>analysis<|message|>step 1"
                "<|end|><|start|>assistant<|channel|>final<|message|>done",
                "step 1", "done");
    checkStream("plain reply", "", "plain reply");
    checkStream("<|channel|>analysis<|message|>unfinished", "unfinished", "");
    checkStream("<|start|>assistant<|channel|>commentary<|message|>hidden<|end|>"
                "<|start|>assistant<|channel|>final<|message|>visible<|return|>",
                "", "visible");
    checkStream("<|channel|>final<|message|>text<|ret", "", "text");

    auto ordinary = parseReasoningContent("<think>reason</think>answer");
    expect(ordinary.reasoning_content == "reason");
    expect(ordinary.regular_content == "answer");
}
