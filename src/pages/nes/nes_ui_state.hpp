#ifndef LILYGO_UI_NES_PAGES_NES_NES_UI_STATE_HPP
#define LILYGO_UI_NES_PAGES_NES_NES_UI_STATE_HPP

#include <cstdint>
#include <string>
#include <utility>

enum class NesUiState {
    library,
    running,
    stopping,
    error,
};

// Shared session feedback lets each page publish an error while the application
// presents it once. Page-specific state stays in the owning ViewModel.
struct NesUiStatus {
    NesUiState state = NesUiState::library;
    std::string status;
    std::uint64_t error_revision = 0;

    void set_error(std::string message)
    {
        state = NesUiState::error;
        status = std::move(message);
        ++error_revision;
    }
};

#endif
