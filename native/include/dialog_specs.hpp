#pragma once
#include "dialog.hpp"
#include "win11usb.hpp"

namespace wu {
// Pure presentation specifications shared by production UI and read-only visual QA.
// These functions never display windows, query devices, or change task state.
ProductDialog StartDialog(const Request &request);
ProductDialog StopDialog(bool closing);
ProductDialog WorkflowDialog(Prompt prompt, const std::wstring &details, const Request &request);
ProductDialog ResultDialog(const Request &request, const Result &result);
} // namespace wu
