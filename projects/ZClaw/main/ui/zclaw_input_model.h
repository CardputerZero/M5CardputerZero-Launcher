#pragma once

#include <string>

namespace zclaw {

enum class InputMode {
    Chat,
    SetupEdit,
    ProviderEdit,
    PairingCode,
};

enum class InputSubmissionAction {
    None,
    SendChat,
    ApplySetupEdit,
    ApplyProviderEdit,
    StartPairing,
};

struct InputSubmission {
    InputSubmissionAction action = InputSubmissionAction::None;
    std::string value;
};

bool input_is_single_line(InputMode mode);
InputSubmission input_submission(InputMode mode, std::string value);

}  // namespace zclaw
