#pragma once

#include <string>

namespace zclaw {

enum class InputMode {
    Chat,
    SetupEdit,
    SetupUriEdit,
    ProviderEdit,
    ProviderUriEdit,
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
bool input_saves_on_close(InputMode mode);
InputSubmission input_submission(InputMode mode, std::string value);

}  // namespace zclaw
