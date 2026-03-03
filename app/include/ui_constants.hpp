#ifndef UI_CONSTANTS_HPP
#define UI_CONSTANTS_HPP

#include <QApplication>
#include <QScreen>

namespace ui::scaling {
    // Returns scale factor relative to 96 DPI baseline.
    // e.g. 1.0 at 96 DPI, 1.5 at 144 DPI, 2.0 at 192 DPI
    // Cached after first call for performance.
    inline double factor() {
        static double cached = -1.0;
        if (cached < 0.0) {
            if (auto* screen = QApplication::primaryScreen()) {
                cached = screen->logicalDotsPerInch() / 96.0;
            } else {
                cached = 1.0;
            }
        }
        return cached;
    }
    
    // Scale a pixel value by the DPI factor
    inline int scaled(int base_value) {
        return static_cast<int>(base_value * factor());
    }
}

namespace ui::dimensions {
    // Base values at 96 DPI (1x scale)
    // Standalone File Tinder (Basic Mode)
    constexpr int kStandaloneFileTinderMinWidth = 700;
    constexpr int kStandaloneFileTinderMinHeight = 400;
    
    // Advanced File Tinder
    constexpr int kAdvancedFileTinderMinWidth = 800;
    constexpr int kAdvancedFileTinderMinHeight = 600;
    
    // Main action buttons
    constexpr int kMainButtonWidth = 200;
    constexpr int kMainButtonHeight = 70;
    constexpr int kThinButtonHeight = 40;
}

namespace ui::colors {
    constexpr const char* kDeleteColor = "#e74c3c";
    constexpr const char* kKeepColor = "#2ecc71";
    constexpr const char* kSortLaterColor = "#f39c12";
    constexpr const char* kMoveColor = "#3498db";
}

namespace ui::fonts {
    constexpr int kHeaderSize = 18;
}

#endif // UI_CONSTANTS_HPP
