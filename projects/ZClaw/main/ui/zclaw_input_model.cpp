#include "zclaw_input_model.h"

#include <utility>

namespace zclaw {

bool input_is_single_line(InputMode mode)
{
    // All editable configuration fields share the same multiline editor so
    // long API keys, models, aliases, and URLs wrap instead of scrolling as a
    // single clipped line. Pairing codes remain deliberately single-line.
    return mode == InputMode::PairingCode;
}

InputSubmission input_submission(InputMode mode, std::string value)
{
    InputSubmission submission;
    submission.value = std::move(value);
    switch (mode) {
    case InputMode::Chat:
        submission.action = submission.value.empty() ? InputSubmissionAction::None
                                                     : InputSubmissionAction::SendChat;
        break;
    case InputMode::SetupEdit:
    case InputMode::SetupUriEdit:
        submission.action = InputSubmissionAction::ApplySetupEdit;
        break;
    case InputMode::ProviderEdit:
    case InputMode::ProviderUriEdit:
        submission.action = InputSubmissionAction::ApplyProviderEdit;
        break;
    case InputMode::PairingCode:
        submission.action = submission.value.empty() ? InputSubmissionAction::None
                                                     : InputSubmissionAction::StartPairing;
        break;
    }
    return submission;
}

}  // namespace zclaw
