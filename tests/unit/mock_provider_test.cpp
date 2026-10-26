#include <gtest/gtest.h>

#include "qcode/interfaces/abstract.h"

namespace qcode {

class MockProvider : public IProvider {
public:
    explicit MockProvider(std::string name, std::string response)
        : name_(std::move(name)), response_(std::move(response)) {}

    std::string name() const override { return name_; }
    std::string request(const std::string&) override { return response_; }

private:
    std::string name_;
    std::string response_;
};

TEST(MockProviderTest, ReturnsConfiguredResponse) {
    const std::string expected = "mock-response";
    MockProvider p("mock", expected);
    EXPECT_EQ(p.name(), "mock");
    EXPECT_EQ(p.request("any prompt"), expected);
}

} // namespace qcode

