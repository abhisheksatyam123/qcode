#include "ai/types/client.h"
#include "providers/openai/openai_factory.h"
#include <iostream>
#include <string>
#include <fstream>

int main() {
    std::ifstream t("/tmp/ag_token.txt");
    std::string token; t >> token;
    
    // Create client using the antigravity endpoint
    ai::Client client = ai::openai::create_client(
        token, "https://daily-cloudcode-pa.googleapis.com/v1internal"
    );

    ai::GenerateOptions opts("claude-opus-4-6-thinking", "Say hi in 3 words.");
    ai::StreamOptions stream_opts(std::move(opts));
    
    std::cout << "Streaming..." << std::endl;
    auto stream = client.stream_text(stream_opts);
    
    if (stream.has_error()) {
        std::cerr << "Error: " << stream.error_message() << std::endl;
        return 1;
    }

    for (const auto& event : stream) {
        if (event.is_text_delta()) {
            std::cout << event.text_delta << std::flush;
        } else if (event.is_finish()) {
            std::cout << "\n[FINISH]" << std::endl;
        } else if (event.is_error()) {
            std::cerr << "\n[ERROR]: " << event.error.value_or("unknown") << std::endl;
        }
    }
    return 0;
}
