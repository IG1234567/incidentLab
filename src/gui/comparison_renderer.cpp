#include "comparison_renderer.hpp"

#include "ui_font.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace incidentlab {
namespace {

const Color kBackground{0x10, 0x15, 0x1F, 255};
const Color kPanel{0x1B, 0x24, 0x32, 255};
const Color kBorder{0x34, 0x41, 0x55, 255};
const Color kText{0xF3, 0xF6, 0xFA, 255};
const Color kMuted{0xA9, 0xB6, 0xC8, 255};
const Color kAccent{0x64, 0xB5, 0xF6, 255};
const Color kButton{0x24, 0x30, 0x44, 255};
const Color kBar{0x3D, 0x8B, 0xD6, 255};

struct ComparisonLayout {
    Rectangle scenarios;
    Rectangle rerun;
    Rectangle bars;
    Rectangle table;
    Rectangle delta;
    Rectangle choice[4];
};

bool hit(Rectangle bounds, Vector2 point) { return CheckCollisionPointRec(point, bounds); }

void panel(Rectangle bounds, const char* title) {
    DrawRectangleRounded(Rectangle{bounds.x - 1.0F, bounds.y - 1.0F, bounds.width + 2.0F,
                                   bounds.height + 2.0F},
                         0.08F, 8, kBorder);
    DrawRectangleRounded(bounds, 0.08F, 8, kPanel);
    if (title != nullptr) {
        ui_draw(UiRole::Heading, bounds.x + 14.0F, bounds.y + 10.0F, title, kMuted);
    }
}

void button(Rectangle bounds, const char* label, bool active) {
    DrawRectangleRounded(bounds, 0.22F, 6, active ? kAccent : kButton);
    const float text_w = ui_measure(UiRole::Button, label);
    const float text_h = static_cast<float>(ui_text_size(UiRole::Button));
    ui_draw(UiRole::Button, bounds.x + (bounds.width - text_w) * 0.5F,
            bounds.y + (bounds.height - text_h) * 0.5F, label, active ? kBackground : kText);
}

ComparisonLayout make_layout() {
    const float width = static_cast<float>(std::max(GetScreenWidth(), 1));
    const float height = static_cast<float>(std::max(GetScreenHeight(), 1));
    const float margin = 12.0F;
    const bool roomy = height >= 820.0F;
    const float header_h = roomy ? 76.0F : 66.0F;
    const float top = margin + header_h + 8.0F + 40.0F + 8.0F;
    const float body = std::max(240.0F, height - top - margin);
    ComparisonLayout layout;
    layout.scenarios = Rectangle{margin, top, width - 2.0F * margin, 48.0F};
    const float gap = 8.0F;
    const float choices = 4.0F;
    const float rerun_w = 120.0F;
    const float choice_w =
        (layout.scenarios.width - 24.0F - rerun_w - gap * choices) / choices;
    for (int index = 0; index < 4; ++index) {
        layout.choice[index] =
            Rectangle{layout.scenarios.x + 12.0F + static_cast<float>(index) * (choice_w + gap),
                      layout.scenarios.y + 8.0F, choice_w, layout.scenarios.height - 16.0F};
    }
    layout.rerun = Rectangle{layout.scenarios.x + layout.scenarios.width - rerun_w - 12.0F,
                             layout.scenarios.y + 8.0F, rerun_w, layout.scenarios.height - 16.0F};

    float bars_h = std::min(188.0F, std::max(132.0F, body * 0.28F));
    float delta_h = std::min(168.0F, std::max(112.0F, body * 0.22F));
    float table_h = body - layout.scenarios.height - bars_h - delta_h - 24.0F;
    if (table_h < 180.0F) {
        const float missing = 180.0F - table_h;
        delta_h = std::max(96.0F, delta_h - missing * 0.5F);
        bars_h = std::max(120.0F, bars_h - missing * 0.5F);
        table_h = body - layout.scenarios.height - bars_h - delta_h - 24.0F;
    }
    const float bars_y = layout.scenarios.y + layout.scenarios.height + 8.0F;
    layout.bars = Rectangle{margin, bars_y, width - 2.0F * margin, bars_h};
    layout.table = Rectangle{margin, layout.bars.y + layout.bars.height + 8.0F, width - 2.0F * margin,
                             std::max(120.0F, table_h)};
    layout.delta = Rectangle{margin, layout.table.y + layout.table.height + 8.0F, width - 2.0F * margin,
                             delta_h};
    return layout;
}

std::string count_text(std::size_t value) { return std::to_string(value); }

std::string percent_text(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f%%", value);
    return buffer;
}

std::string latency_text(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f ms", value);
    return buffer;
}

std::string throughput_text(const std::optional<double>& value) {
    if (!value.has_value() || !std::isfinite(*value)) {
        return "N/A";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f req/s", *value);
    return buffer;
}

std::string signed_points(double value) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%+.2f percentage points", value);
    return buffer;
}

std::string signed_count(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%+.0f", value);
    return buffer;
}

const GlobalMetrics* column_metrics(const ComparisonReport& report, std::size_t index) {
    if (index >= report.scenarios.size()) {
        return nullptr;
    }
    return &report.scenarios[index].metrics.global;
}

void draw_bars(const ComparisonLayout& layout, const ComparisonView& view) {
    panel(layout.bars, "RELIABILITY COMPARISON");
    const float top = layout.bars.y + 14.0F + static_cast<float>(ui_text_size(UiRole::Heading));
    const float row_h = std::max(22.0F, (layout.bars.height - (top - layout.bars.y) - 12.0F) / 4.0F);
    const float label_w = 168.0F;
    const float value_w = 72.0F;
    const float bar_x = layout.bars.x + 16.0F + label_w;
    const float bar_w = std::max(40.0F, layout.bars.width - label_w - value_w - 36.0F);
    const ComparisonReport& report = view.report();
    for (std::size_t index = 0; index < report.scenarios.size() && index < 4; ++index) {
        const ScenarioResult& item = report.scenarios[index];
        const float y = top + static_cast<float>(index) * row_h;
        const bool selected = index == view.selected_index();
        ui_draw(UiRole::Label, layout.bars.x + 16.0F, y, item.config.name.c_str(),
                selected ? kText : kMuted);
        const float rate = static_cast<float>(item.metrics.global.success_rate_percent);
        const float filled = std::max(0.0F, std::min(bar_w, bar_w * rate / 100.0F));
        DrawRectangle(static_cast<int>(bar_x), static_cast<int>(y + 2.0F), static_cast<int>(bar_w),
                      static_cast<int>(row_h - 8.0F), kButton);
        DrawRectangle(static_cast<int>(bar_x), static_cast<int>(y + 2.0F), static_cast<int>(filled),
                      static_cast<int>(row_h - 8.0F), selected ? kAccent : kBar);
        const std::string text = percent_text(item.metrics.global.success_rate_percent);
        ui_draw(UiRole::Value, bar_x + bar_w + 10.0F, y, text.c_str(), kText);
    }
}

void draw_cell(float x, float y, const std::string& text, Color color) {
    ui_draw(UiRole::Detail, x, y, text.c_str(), color);
}

void draw_table(const ComparisonLayout& layout, const ComparisonView& view) {
    panel(layout.table, "METRIC COMPARISON");
    const ComparisonReport& report = view.report();
    const float top = layout.table.y + 14.0F + static_cast<float>(ui_text_size(UiRole::Heading));
    const float usable = std::max(1.0F, layout.table.height - (top - layout.table.y) - 12.0F);
    const float row_h = usable / 10.0F;
    const float x0 = layout.table.x + 16.0F;
    const float span = layout.table.width - 32.0F;
    const float columns[5] = {x0, x0 + span * 0.34F, x0 + span * 0.50F, x0 + span * 0.66F,
                              x0 + span * 0.82F};
    const char* headers[] = {"Metric", "Baseline", "Retry", "Circuit", "Full"};
    for (int index = 0; index < 5; ++index) {
        const bool selected = index > 0 && view.selected_index() + 1 == static_cast<std::size_t>(index);
        ui_draw(UiRole::Label, columns[index], top, headers[index], selected ? kAccent : kMuted);
    }

    const char* labels[] = {"Success rate", "Failure rate", "Average",        "P95",
                            "Throughput",   "Attempts",     "Retry attempts", "Recovered",
                            "Rejected",     "DLQ entries"};
    for (int row = 0; row < 10; ++row) {
        const float y = top + static_cast<float>(row + 1) * row_h;
        ui_draw(UiRole::Detail, columns[0], y, labels[row], kText);
        for (std::size_t column = 0; column < 4; ++column) {
            const GlobalMetrics* metrics = column_metrics(report, column);
            std::string text = "n/a";
            if (metrics != nullptr) {
                switch (row) {
                case 0:
                    text = percent_text(metrics->success_rate_percent);
                    break;
                case 1:
                    text = percent_text(metrics->failure_rate_percent);
                    break;
                case 2:
                    text = latency_text(metrics->latency.average_ms);
                    break;
                case 3:
                    text = latency_text(metrics->latency.p95_ms);
                    break;
                case 4:
                    text = throughput_text(metrics->throughput_requests_per_second);
                    break;
                case 5:
                    text = count_text(metrics->retries.total_attempts);
                    break;
                case 6:
                    text = count_text(metrics->retries.retry_attempts);
                    break;
                case 7:
                    text = count_text(metrics->retries.recovered_after_retry);
                    break;
                case 8:
                    text = count_text(metrics->outcomes.circuit_rejected);
                    break;
                default:
                    text = count_text(metrics->dead_lettered_requests);
                    break;
                }
            }
            const bool selected = column == view.selected_index();
            draw_cell(columns[column + 1], y, text, selected ? kText : kMuted);
        }
    }
}

void draw_delta(const ComparisonLayout& layout, const ComparisonView& view) {
    panel(layout.delta, "BASELINE DELTAS");
    const ScenarioDelta delta = view.selected_delta();
    const float x = layout.delta.x + 16.0F;
    float y = layout.delta.y + 14.0F + static_cast<float>(ui_text_size(UiRole::Heading));
    const float gap = static_cast<float>(ui_text_size(UiRole::Detail) + 6);
    const std::string title =
        delta.scenario_name.empty() ? "Selected scenario: none" : "Selected scenario: " + delta.scenario_name;
    ui_draw(UiRole::Label, x, y, title.c_str(), kText);
    y += gap;
    if (!delta.baseline_found) {
        ui_draw(UiRole::Detail, x, y, "Baseline is not in this comparison.", kMuted);
        return;
    }
    const std::string success = "Success rate difference: " + signed_points(delta.success_rate.absolute_difference);
    ui_draw(UiRole::Detail, x, y, success.c_str(), kText);
    y += gap;
    std::string relative = "Relative success-rate change: unavailable";
    if (delta.success_rate.relative_difference_available) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "Relative success-rate change: %+.2f%%",
                      delta.success_rate.relative_difference_percent);
        relative = buffer;
    }
    ui_draw(UiRole::Detail, x, y, relative.c_str(), kMuted);
    const float right = layout.delta.x + layout.delta.width * 0.48F;
    float right_y = layout.delta.y + 14.0F + static_cast<float>(ui_text_size(UiRole::Heading)) + gap;
    const std::string attempts = "Additional attempts: " + signed_count(delta.total_attempts.absolute_difference);
    const std::string retries = "Additional retries: " + signed_count(delta.retry_attempts.absolute_difference);
    const std::string rejected =
        "Additional rejections: " + signed_count(delta.circuit_rejections.absolute_difference);
    const std::string dlq = "Additional DLQ entries: " + signed_count(delta.dlq_entries.absolute_difference);
    ui_draw(UiRole::Detail, right, right_y, attempts.c_str(), kText);
    right_y += gap;
    ui_draw(UiRole::Detail, right, right_y, retries.c_str(), kMuted);
    right_y += gap;
    ui_draw(UiRole::Detail, right, right_y, rejected.c_str(), kMuted);
    right_y += gap;
    ui_draw(UiRole::Detail, right, right_y, dlq.c_str(), kMuted);
}

}  // namespace

void update_comparison(ComparisonView& view) {
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        return;
    }
    const ComparisonLayout layout = make_layout();
    const Vector2 mouse = GetMousePosition();
    if (hit(layout.rerun, mouse)) {
        view.rerun();
        return;
    }
    const std::size_t count = std::min<std::size_t>(4, view.report().scenarios.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (hit(layout.choice[index], mouse)) {
            view.select_index(index);
            return;
        }
    }
}

void draw_comparison(const ComparisonView& view, GuiMode mode) {
    const ComparisonLayout layout = make_layout();
    ClearBackground(kBackground);
    const float margin = 12.0F;
    const float width = static_cast<float>(std::max(GetScreenWidth(), 1));
    const float header_h = GetScreenHeight() >= 820 ? 76.0F : 66.0F;
    panel(Rectangle{margin, margin, width - 2.0F * margin, header_h}, nullptr);
    ui_draw(UiRole::Title, margin + 16.0F, margin + 12.0F, "INCIDENTLAB", kText);
    ui_draw(UiRole::Detail, margin + 16.0F,
            margin + 16.0F + static_cast<float>(ui_text_size(UiRole::Title)),
            "Scenario comparison", kMuted);
    draw_mode_buttons(mode_buttons(), mode);

    panel(layout.scenarios, nullptr);
    const std::size_t count = std::min<std::size_t>(4, view.report().scenarios.size());
    for (std::size_t index = 0; index < count; ++index) {
        button(layout.choice[index], view.report().scenarios[index].config.name.c_str(),
               index == view.selected_index());
    }
    button(layout.rerun, "RERUN", false);
    draw_bars(layout, view);
    draw_table(layout, view);
    draw_delta(layout, view);
}

}  // namespace incidentlab
