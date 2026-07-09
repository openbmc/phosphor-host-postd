#pragma once

#include "lpcsnoop/snoop.hpp"

#include <functional>

namespace lpcsnoop
{

class SnoopControl : public PostReporter
{
  public:
    using PostReporter::PostReporter;

    void setCapture(std::function<void(bool)> fn)
    {
        onCapture = std::move(fn);
    }

    bool captureEnabled(bool value) override
    {
        auto result = PostInterface::captureEnabled(value);
        if (onCapture)
            onCapture(value);
        return result;
    }

  private:
    std::function<void(bool)> onCapture;
};

} // namespace lpcsnoop
