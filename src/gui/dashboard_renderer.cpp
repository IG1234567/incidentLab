#include "dashboard_renderer.hpp"
#include "ui_font.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#ifndef MOUSE_BUTTON_LEFT
#define MOUSE_BUTTON_LEFT MOUSE_LEFT_BUTTON
#endif

namespace incidentlab {
namespace {

const Color kBackground{0x10, 0x15, 0x1F, 255};
const Color kPanel{0x1B, 0x24, 0x32, 255};
const Color kBorder{0x34, 0x41, 0x55, 255};
const Color kText{0xF3, 0xF6, 0xFA, 255};
const Color kMuted{0xA9, 0xB6, 0xC8, 255};
const Color kHealthy{0x36, 0xC9, 0x8F, 255};
const Color kWarning{0xF3, 0xC9, 0x69, 255};
const Color kFailure{0xEF, 0x65, 0x75, 255};
const Color kAccent{0x64, 0xB5, 0xF6, 255};
const Color kDropped{0xE0, 0x8A, 0x3C, 255};
const Color kButton{0x24, 0x30, 0x44, 255};

struct NodeBox {
    std::string name;
    Rectangle bounds;
};

struct Layout {
    Rectangle header;
    Rectangle controls;
    Rectangle topology;
    Rectangle metrics;
    Rectangle timeline;
    Rectangle footer;
    Rectangle start;
    Rectangle pause;
    Rectangle resume;
    Rectangle step;
    Rectangle reset;
    Rectangle speed;
    std::vector<NodeBox> nodes;
    Rectangle client;
};

int& timeline_scroll() {
    static int rows = 0;
    return rows;
}

bool hit(Rectangle bounds, Vector2 point) { return CheckCollisionPointRec(point, bounds); }

bool roomy_layout() { return GetScreenHeight() >= 820; }

int timeline_row_height() { return ui_text_size(UiRole::Timeline) + 10; }

Rectangle timeline_view(const Layout& layout) {
    const float top = 12.0F + static_cast<float>(ui_text_size(UiRole::Heading)) + 8.0F;
    Rectangle view{layout.timeline.x + 12.0F, layout.timeline.y + top,
                   layout.timeline.width - 24.0F, layout.timeline.height - top - 10.0F};
    const int row = std::max(1, timeline_row_height());
    const int rows = std::max(1, static_cast<int>(view.height) / row);
    view.height = static_cast<float>(rows * row);
    return view;
}

int metric_row_height() {
    return std::max(ui_text_size(UiRole::Label), ui_text_size(UiRole::Value)) + 8;
}

Rectangle button_row(Rectangle bar, int index, int count) {
    const float gap = 8.0F;
    const float width = (bar.width - 24.0F - gap * static_cast<float>(count - 1)) /
                        static_cast<float>(count);
    return Rectangle{bar.x + 12.0F + static_cast<float>(index) * (width + gap), bar.y + 10.0F,
                     width, bar.height - 20.0F};
}

int visual_slot(const std::string& name) {
    if (name == "OrderService") {
        return 0;
    }
    if (name == "PaymentService") {
        return 1;
    }
    if (name == "InventoryService") {
        return 2;
    }
    return 3;
}

Layout make_layout(const VisualizationSnapshot& snapshot) {
    const float width = static_cast<float>(std::max(GetScreenWidth(), 1));
    const float height = static_cast<float>(std::max(GetScreenHeight(), 1));
    const float margin = 12.0F;
    const bool roomy = roomy_layout();
    Layout layout;
    layout.header = Rectangle{margin, margin, width - 2.0F * margin, roomy ? 76.0F : 66.0F};
    layout.controls =
        Rectangle{margin, layout.header.y + layout.header.height + 8.0F, width - 2.0F * margin,
                  roomy ? 54.0F : 48.0F};
    const float footer_h = roomy ? 96.0F : 84.0F;
    layout.footer = Rectangle{margin, height - margin - footer_h, width - 2.0F * margin, footer_h};
    const float middle_top = layout.controls.y + layout.controls.height + 8.0F;
    const float middle_bottom = layout.footer.y - 8.0F;
    const float middle_h = std::max(180.0F, middle_bottom - middle_top);
    const float timeline_share = height >= 860.0F ? 0.34F : 0.30F;
    const float timeline_h = std::min(300.0F, std::max(128.0F, middle_h * timeline_share));
    layout.timeline =
        Rectangle{margin, middle_bottom - timeline_h, width - 2.0F * margin, timeline_h};
    const float body_h = layout.timeline.y - 10.0F - middle_top;
    const float max_left = std::max(360.0F, width - 2.0F * margin - 340.0F);
    const float left_w = std::min(max_left, std::max(360.0F, width * 0.46F));
    layout.topology = Rectangle{margin, middle_top, left_w, body_h};
    layout.metrics = Rectangle{layout.topology.x + layout.topology.width + 12.0F, middle_top,
                                width - margin - (layout.topology.x + layout.topology.width + 12.0F),
                                body_h};

    layout.start = button_row(layout.controls, 0, 6);
    layout.pause = button_row(layout.controls, 1, 6);
    layout.resume = button_row(layout.controls, 2, 6);
    layout.step = button_row(layout.controls, 3, 6);
    layout.reset = button_row(layout.controls, 4, 6);
    layout.speed = button_row(layout.controls, 5, 6);

    layout.client = Rectangle{layout.topology.x + 24.0F, layout.topology.y, 88.0F, 36.0F};
    std::vector<const ServiceNodeView*> ordered(3, nullptr);
    std::vector<const ServiceNodeView*> extra;
    for (const ServiceNodeView& node : snapshot.services) {
        const int slot = visual_slot(node.name);
        if (slot < 3 && ordered[static_cast<std::size_t>(slot)] == nullptr) {
            ordered[static_cast<std::size_t>(slot)] = &node;
        } else {
            extra.push_back(&node);
        }
    }
    std::vector<const ServiceNodeView*> placed;
    for (const ServiceNodeView* node : ordered) {
        if (node != nullptr) {
            placed.push_back(node);
        }
    }
    placed.insert(placed.end(), extra.begin(), extra.end());
    const float node_w = std::min(280.0F, std::max(220.0F, layout.topology.width * 0.46F));
    const float top_pad = roomy ? 58.0F : 52.0F;
    const float bottom_pad = 16.0F;
    const float min_h = roomy ? 76.0F : 68.0F;
    const float max_h = roomy ? 96.0F : 78.0F;
    const float count = static_cast<float>(placed.size());
    const float usable = std::max(min_h, layout.topology.height - top_pad - bottom_pad);
    float node_h = min_h;
    if (count > 0.0F) {
        const float gaps = std::max(0.0F, count - 1.0F);
        const float raw = (usable - 12.0F * gaps) / count;
        node_h = std::min(max_h, std::max(min_h, raw));
    }
    const float stack = node_h * count;
    float gap = count <= 1.0F ? 0.0F : (usable - stack) / (count - 1.0F);
    float block = stack + gap * std::max(0.0F, count - 1.0F);
    if (gap > 48.0F) {
        gap = 48.0F;
        block = stack + gap * std::max(0.0F, count - 1.0F);
    }
    const float node_x = layout.topology.x + layout.topology.width - node_w - 28.0F;
    const float slack = std::max(0.0F, usable - block);
    float y = layout.topology.y + top_pad + slack * 0.35F;
    for (const ServiceNodeView* node : placed) {
        layout.nodes.push_back(NodeBox{node->name, Rectangle{node_x, y, node_w, node_h}});
        y += node_h + gap;
    }
    if (!layout.nodes.empty()) {
        const Rectangle& first = layout.nodes.front().bounds;
        const Rectangle& last = layout.nodes.back().bounds;
        const float mid = (first.y + last.y + last.height) * 0.5F;
        layout.client.y = mid - layout.client.height * 0.5F;
    }
    return layout;
}

void panel(Rectangle bounds, const char* title) {
    DrawRectangleRounded(Rectangle{bounds.x - 1.0F, bounds.y - 1.0F, bounds.width + 2.0F,
                                   bounds.height + 2.0F},
                         0.08F, 8, kBorder);
    DrawRectangleRounded(bounds, 0.08F, 8, kPanel);
    if (title != nullptr) {
        ui_draw(UiRole::Heading, bounds.x + 14.0F, bounds.y + 10.0F, title, kMuted);
    }
}

void draw_button(Rectangle bounds, const char* label, bool active) {
    DrawRectangleRounded(bounds, 0.25F, 6, active ? kAccent : kButton);
    const float text_w = ui_measure(UiRole::Button, label);
    const float text_h = static_cast<float>(ui_text_size(UiRole::Button));
    ui_draw(UiRole::Button, bounds.x + (bounds.width - text_w) * 0.5F,
            bounds.y + (bounds.height - text_h) * 0.5F, label, active ? kBackground : kText);
}

Color status_color(VisualServiceStatus status) {
    switch (status) {
    case VisualServiceStatus::Healthy:
        return kHealthy;
    case VisualServiceStatus::Processing:
        return kAccent;
    case VisualServiceStatus::Failed:
    case VisualServiceStatus::CircuitOpen:
        return kFailure;
    case VisualServiceStatus::TimedOut:
        return kWarning;
    case VisualServiceStatus::Dropped:
        return kDropped;
    case VisualServiceStatus::Idle:
        return kMuted;
    }
    return kMuted;
}

Color circuit_color(const std::optional<CircuitState>& state) {
    if (!state.has_value()) {
        return kMuted;
    }
    switch (*state) {
    case CircuitState::Closed:
        return kHealthy;
    case CircuitState::Open:
        return kFailure;
    case CircuitState::HalfOpen:
        return kWarning;
    }
    return kMuted;
}

void format_percent(char* buffer, std::size_t size, double value) {
    std::snprintf(buffer, size, "%.2f%%", value);
}

void format_ms(char* buffer, std::size_t size, double value) {
    std::snprintf(buffer, size, "%.2f ms", value);
}

void draw_line_value(float x, float y, float value_x, const char* label, const char* value) {
    ui_draw(UiRole::Label, x, y, label, kMuted);
    ui_draw(UiRole::Value, value_x, y, value, kText);
}

const ServiceNodeView* find_node(const VisualizationSnapshot& snapshot, const std::string& name) {
    for (const ServiceNodeView& node : snapshot.services) {
        if (node.name == name) {
            return &node;
        }
    }
    return nullptr;
}

const ServiceMetrics* find_metrics(const VisualizationSnapshot& snapshot, const std::string& name) {
    for (const ServiceMetrics& service : snapshot.metrics.services) {
        if (service.service_name == name) {
            return &service;
        }
    }
    return nullptr;
}

const CircuitMetrics* find_circuit(const VisualizationSnapshot& snapshot, const std::string& name) {
    for (const CircuitMetrics& circuit : snapshot.metrics.circuits) {
        if (circuit.service_name == name) {
            return &circuit;
        }
    }
    return nullptr;
}

void draw_dashed(Vector2 from, Vector2 to, Color color) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0F) {
        return;
    }
    const float step = 10.0F;
    for (float distance = 0.0F; distance < length; distance += step * 2.0F) {
        const float end = std::min(distance + step, length);
        DrawLine(static_cast<int>(from.x + dx * (distance / length)),
                 static_cast<int>(from.y + dy * (distance / length)),
                 static_cast<int>(from.x + dx * (end / length)),
                 static_cast<int>(from.y + dy * (end / length)), color);
    }
}

void draw_topology(const Layout& layout, const VisualizationSnapshot& snapshot) {
    panel(layout.topology, "SERVICE TOPOLOGY");
    ui_draw(UiRole::Detail, layout.topology.x + 14.0F,
            layout.topology.y + 12.0F + static_cast<float>(ui_text_size(UiRole::Heading)),
            "Observed calls, not dependencies", kMuted);
    DrawRectangleRounded(layout.client, 0.3F, 6, kButton);
    const float client_text_w = ui_measure(UiRole::Label, "client");
    ui_draw(UiRole::Label, layout.client.x + (layout.client.width - client_text_w) * 0.5F,
            layout.client.y + (layout.client.height - static_cast<float>(ui_text_size(UiRole::Label))) * 0.5F,
            "client", kText);

    for (const FlowEdge& edge : snapshot.flows) {
        const NodeBox* target = nullptr;
        for (const NodeBox& node : layout.nodes) {
            if (node.name == edge.target_service) {
                target = &node;
            }
        }
        if (target == nullptr) {
            continue;
        }
        const Vector2 from{layout.client.x + layout.client.width,
                           layout.client.y + layout.client.height * 0.5F};
        const Vector2 to{target->bounds.x, target->bounds.y + target->bounds.height * 0.5F};
        const Color color = edge.observed ? kAccent : kBorder;
        if (edge.observed) {
            DrawLine(static_cast<int>(from.x), static_cast<int>(from.y), static_cast<int>(to.x),
                     static_cast<int>(to.y), color);
        } else {
            draw_dashed(from, to, color);
        }
    }

    for (const NodeBox& box : layout.nodes) {
        const ServiceNodeView* node = find_node(snapshot, box.name);
        if (node == nullptr) {
            continue;
        }
        const Color mark = status_color(node->status);
        if (node->selected) {
            DrawRectangleRounded(Rectangle{box.bounds.x - 3.0F, box.bounds.y - 3.0F,
                                           box.bounds.width + 6.0F, box.bounds.height + 6.0F},
                                 0.12F, 6, kAccent);
        }
        DrawRectangleRounded(box.bounds, 0.12F, 6, kBackground);
        DrawRectangle(static_cast<int>(box.bounds.x), static_cast<int>(box.bounds.y + 8.0F), 6,
                      static_cast<int>(box.bounds.height - 16.0F), mark);
        const float name_size = static_cast<float>(ui_text_size(UiRole::Label));
        const float detail_size = static_cast<float>(ui_text_size(UiRole::Detail));
        const float text_block = name_size + detail_size + detail_size + 12.0F;
        const float text_y =
            box.bounds.y + std::max(8.0F, (box.bounds.height - text_block) * 0.5F);
        ui_draw(UiRole::Label, box.bounds.x + 16.0F, text_y, node->name.c_str(), kText);
        ui_draw(UiRole::Detail, box.bounds.x + 16.0F, text_y + name_size + 4.0F, to_string(node->status),
                mark);
        char detail[96];
        std::snprintf(detail, sizeof(detail), "%zu done   %.0f%% ok", node->completed_requests,
                      node->success_rate_percent);
        ui_draw(UiRole::Detail, box.bounds.x + 16.0F, text_y + name_size + detail_size + 8.0F, detail,
                kMuted);
    }
}

void draw_metrics(const Layout& layout, const VisualizationSnapshot& snapshot) {
    panel(layout.metrics, "METRICS");
    BeginScissorMode(static_cast<int>(layout.metrics.x), static_cast<int>(layout.metrics.y),
                     static_cast<int>(layout.metrics.width),
                     static_cast<int>(layout.metrics.height));
    const GlobalMetrics& global = snapshot.metrics.global;
    const float left = layout.metrics.x + 16.0F;
    const float right = layout.metrics.x + layout.metrics.width * 0.52F;
    const float left_value = left + 156.0F;
    const float right_value = right + 156.0F;
    const float content_top = 14.0F + static_cast<float>(ui_text_size(UiRole::Heading));
    const float available = std::max(0.0F, layout.metrics.height - content_top - 16.0F);
    const float natural = static_cast<float>(metric_row_height());
    const float step = std::min(natural + 14.0F, std::max(natural, available / 10.0F));
    float y = layout.metrics.y + content_top;
    char value[64];
    std::snprintf(value, sizeof(value), "%zu", global.outcomes.completed);
    draw_line_value(left, y, left_value, "Completed", value);
    std::snprintf(value, sizeof(value), "%zu", global.retries.total_attempts);
    draw_line_value(right, y, right_value, "Attempts", value);
    y += step;
    std::snprintf(value, sizeof(value), "%zu", global.outcomes.successful);
    draw_line_value(left, y, left_value, "Successful", value);
    std::snprintf(value, sizeof(value), "%zu", global.retries.retry_attempts);
    draw_line_value(right, y, right_value, "Retries", value);
    y += step;
    std::snprintf(value, sizeof(value), "%zu", global.outcomes.failed);
    draw_line_value(left, y, left_value, "Failed", value);
    std::snprintf(value, sizeof(value), "%zu", global.retries.recovered_after_retry);
    draw_line_value(right, y, right_value, "Recovered", value);
    y += step;
    format_percent(value, sizeof(value), global.success_rate_percent);
    draw_line_value(left, y, left_value, "Success rate", value);
    std::snprintf(value, sizeof(value), "%zu", global.retries.exhausted_retries);
    draw_line_value(right, y, right_value, "Exhausted", value);
    y += step;
    format_percent(value, sizeof(value), global.failure_rate_percent);
    draw_line_value(left, y, left_value, "Failure rate", value);
    std::snprintf(value, sizeof(value), "%zu", global.circuit_openings);
    draw_line_value(right, y, right_value, "Openings", value);
    y += step;
    format_ms(value, sizeof(value), global.latency.average_ms);
    draw_line_value(left, y, left_value, "Average", value);
    std::snprintf(value, sizeof(value), "%zu", global.circuit_half_openings);
    draw_line_value(right, y, right_value, "Half-open", value);
    y += step;
    format_ms(value, sizeof(value), global.latency.p50_ms);
    draw_line_value(left, y, left_value, "P50", value);
    std::snprintf(value, sizeof(value), "%zu", global.outcomes.circuit_rejected);
    draw_line_value(right, y, right_value, "Rejected", value);
    y += step;
    format_ms(value, sizeof(value), global.latency.p95_ms);
    draw_line_value(left, y, left_value, "P95", value);
    std::snprintf(value, sizeof(value), "%zu", global.dead_lettered_requests);
    draw_line_value(right, y, right_value, "DLQ entries", value);
    y += step;
    format_ms(value, sizeof(value), global.latency.p99_ms);
    draw_line_value(left, y, left_value, "P99", value);
    y += step;
    const std::string throughput = format_optional_metric(global.throughput_requests_per_second);
    char throughput_text[64];
    if (throughput == "N/A") {
        std::snprintf(throughput_text, sizeof(throughput_text), "N/A");
    } else {
        std::snprintf(throughput_text, sizeof(throughput_text), "%s req/s", throughput.c_str());
    }
    draw_line_value(left, y, left_value, "Throughput", throughput_text);
    EndScissorMode();
}

void draw_timeline(const Layout& layout, const VisualizationSnapshot& snapshot) {
    panel(layout.timeline, "INCIDENT TIMELINE");
    const char* hint = "Space pause/resume    N step    R reset    Esc close";
    const float hint_w = ui_measure(UiRole::Detail, hint);
    if (hint_w + 220.0F < layout.timeline.width) {
        ui_draw(UiRole::Detail, layout.timeline.x + layout.timeline.width - hint_w - 14.0F,
                layout.timeline.y + 12.0F, hint, kMuted);
    }
    const std::vector<TimelineEntry> entries = timeline_for_selected_service(snapshot);
    const Rectangle view = timeline_view(layout);
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y),
                     static_cast<int>(view.width), static_cast<int>(view.height));
    if (entries.empty()) {
        const char* message = snapshot.selected_service.has_value()
                                  ? "No events for the selected service."
                                  : "No events yet. Press Start or Step.";
        ui_draw(UiRole::Timeline, view.x + 4.0F, view.y + 6.0F, message, kMuted);
    }
    const int row_h = timeline_row_height();
    const int text_pad = std::max(0, (row_h - ui_text_size(UiRole::Timeline)) / 2);
    const int max_scroll = std::max(0, static_cast<int>(entries.size()) - 1);
    int& scroll = timeline_scroll();
    if (scroll > max_scroll) {
        scroll = max_scroll;
    }
    if (scroll < 0) {
        scroll = 0;
    }
    int y = static_cast<int>(std::round(view.y)) - scroll * row_h;
    for (const TimelineEntry& entry : entries) {
        if (y + row_h >= static_cast<int>(view.y) && y <= static_cast<int>(view.y + view.height)) {
            const bool selected =
                snapshot.selected_event_id.has_value() && *snapshot.selected_event_id == entry.event_id;
            if (selected) {
                DrawRectangle(static_cast<int>(view.x), y, static_cast<int>(view.width), row_h,
                              kButton);
            }
            char line[160];
            std::snprintf(line, sizeof(line), "%5llu ms    %-16s   %-8s   %s",
                          static_cast<unsigned long long>(entry.time_ms), entry.service_name.c_str(),
                          entry.request_id.c_str(), to_string(entry.type));
            ui_draw(UiRole::Timeline, view.x + 4.0F, static_cast<float>(y + text_pad), line, kText);
        }
        y += row_h;
    }
    EndScissorMode();
}

void draw_footer(const Layout& layout, const VisualizationSnapshot& snapshot) {
    panel(layout.footer, nullptr);
    const float x = layout.footer.x + 16.0F;
    float y = layout.footer.y + 10.0F;
    const float event_x = layout.footer.x + layout.footer.width * 0.60F;
    const float gap = static_cast<float>(ui_text_size(UiRole::Detail) + 4);
    BeginScissorMode(static_cast<int>(layout.footer.x), static_cast<int>(layout.footer.y),
                     static_cast<int>(event_x - layout.footer.x) - 8,
                     static_cast<int>(layout.footer.height));
    if (!snapshot.selected_service.has_value()) {
        ui_draw(UiRole::Label, x, y, "Selected service: none. Click a service node.", kMuted);
    } else {
        const std::string& name = *snapshot.selected_service;
        const ServiceNodeView* node = find_node(snapshot, name);
        const ServiceMetrics* metrics = find_metrics(snapshot, name);
        char line[192];
        std::snprintf(line, sizeof(line), "Selected service: %s", name.c_str());
        ui_draw(UiRole::Label, x, y, line, kText);
        y += gap;
        if (node == nullptr || metrics == nullptr) {
            ui_draw(UiRole::Detail, x, y, "No completed activity for this service.", kMuted);
        } else {
            char rate[32];
            char failure[32];
            char average[32];
            char p95[32];
            format_percent(rate, sizeof(rate), metrics->success_rate_percent);
            format_percent(failure, sizeof(failure), metrics->failure_rate_percent);
            format_ms(average, sizeof(average), metrics->latency.average_ms);
            format_ms(p95, sizeof(p95), metrics->latency.p95_ms);
            std::snprintf(line, sizeof(line), "Done %zu   Success %s   Failure %s",
                          metrics->outcomes.completed, rate, failure);
            ui_draw(UiRole::Detail, x, y, line, kMuted);
            y += gap;
            std::snprintf(line, sizeof(line), "Avg %s   P95 %s   Retries %zu", average, p95,
                          metrics->retries.retry_attempts);
            ui_draw(UiRole::Detail, x, y, line, kMuted);
        }
        y += gap;
        const CircuitMetrics* circuit = find_circuit(snapshot, name);
        const std::string label = circuit_label(node == nullptr ? std::nullopt : node->circuit);
        if (circuit == nullptr) {
            std::snprintf(line, sizeof(line), "Circuit Breaker: %s", label.c_str());
        } else {
            std::snprintf(line, sizeof(line),
                          "Circuit Breaker: %s    Openings %zu    Half-open %zu    Rejected %zu    DLQ %zu",
                          label.c_str(), circuit->opened, circuit->half_opened, circuit->blocked_requests,
                          metrics == nullptr ? 0 : metrics->dead_lettered_requests);
        }
        ui_draw(UiRole::Detail, x, y, line, circuit_color(node == nullptr ? std::nullopt : node->circuit));
    }
    EndScissorMode();

    const std::optional<TimelineEntry> selected = selected_timeline_entry(snapshot);
    if (!selected.has_value()) {
        ui_draw(UiRole::Label, event_x, layout.footer.y + 10.0F, "Selected event: none", kMuted);
    } else {
        char line[160];
        std::snprintf(line, sizeof(line), "%s    %llu ms", to_string(selected->type),
                      static_cast<unsigned long long>(selected->time_ms));
        ui_draw(UiRole::Label, event_x, layout.footer.y + 10.0F, line, kText);
        std::snprintf(line, sizeof(line), "%s    %s    %s", selected->service_name.c_str(),
                      selected->request_id.c_str(), selected->trace_id.c_str());
        ui_draw(UiRole::Detail, event_x, layout.footer.y + 10.0F + gap, line, kMuted);
    }
}

PlaybackSpeed next_speed(PlaybackSpeed speed) {
    switch (speed) {
    case PlaybackSpeed::Half:
        return PlaybackSpeed::Normal;
    case PlaybackSpeed::Normal:
        return PlaybackSpeed::Double;
    case PlaybackSpeed::Double:
        return PlaybackSpeed::Quadruple;
    case PlaybackSpeed::Quadruple:
        return PlaybackSpeed::Half;
    }
    return PlaybackSpeed::Normal;
}

int timeline_row_at(const Layout& layout, Vector2 mouse, std::size_t count) {
    const Rectangle view = timeline_view(layout);
    if (!hit(view, mouse) || count == 0) {
        return -1;
    }
    const int local = static_cast<int>(mouse.y - view.y);
    const int index = timeline_scroll() + local / timeline_row_height();
    if (index < 0 || static_cast<std::size_t>(index) >= count) {
        return -1;
    }
    return index;
}

}  // namespace

void update_dashboard(SimulationController& controller) {
    const VisualizationSnapshot& snapshot = controller.snapshot();
    const Layout layout = make_layout(snapshot);
    const Vector2 mouse = GetMousePosition();

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (hit(layout.start, mouse)) {
            controller.start();
        } else if (hit(layout.pause, mouse)) {
            controller.pause();
        } else if (hit(layout.resume, mouse)) {
            controller.resume();
        } else if (hit(layout.step, mouse)) {
            controller.step();
        } else if (hit(layout.reset, mouse)) {
            controller.reset();
            timeline_scroll() = 0;
        } else if (hit(layout.speed, mouse)) {
            controller.set_speed(next_speed(controller.speed()));
        } else {
            bool node_hit = false;
            for (const NodeBox& node : layout.nodes) {
                if (hit(node.bounds, mouse)) {
                    controller.select_service(node.name);
                    node_hit = true;
                }
            }
            const std::vector<TimelineEntry> entries = timeline_for_selected_service(snapshot);
            const int row = timeline_row_at(layout, mouse, entries.size());
            if (row >= 0) {
                controller.select_event(entries[static_cast<std::size_t>(row)].event_id);
            } else if (!node_hit && hit(layout.topology, mouse)) {
                controller.clear_service_selection();
            }
        }
    }

    const Rectangle view = timeline_view(layout);
    const float wheel = GetMouseWheelMove();
    if (hit(view, mouse) && wheel != 0.0F) {
        timeline_scroll() -= static_cast<int>(wheel * 3.0F);
    }

    if (IsKeyPressed(KEY_SPACE)) {
        switch (controller.state()) {
        case PlaybackState::Ready:
            controller.start();
            break;
        case PlaybackState::Running:
            controller.pause();
            break;
        case PlaybackState::Paused:
            controller.resume();
            break;
        case PlaybackState::Completed:
            break;
        }
    }
    if (IsKeyPressed(KEY_N)) {
        controller.step();
    }
    if (IsKeyPressed(KEY_R)) {
        controller.reset();
        timeline_scroll() = 0;
    }
}

void draw_dashboard(const VisualizationSnapshot& snapshot) {
    const Layout layout = make_layout(snapshot);
    ClearBackground(kBackground);
    panel(layout.header, nullptr);
    ui_draw(UiRole::Title, layout.header.x + 16.0F, layout.header.y + 12.0F, "INCIDENTLAB", kText);
    ui_draw(UiRole::Detail, layout.header.x + 16.0F,
            layout.header.y + 16.0F + static_cast<float>(ui_text_size(UiRole::Title)),
            "Distributed Systems Failure Simulator", kMuted);
    char clock_line[96];
    std::snprintf(clock_line, sizeof(clock_line), "%s    %llu ms    %s", to_string(snapshot.playback),
                  static_cast<unsigned long long>(snapshot.simulation_time_ms),
                  to_string(snapshot.speed));
    const float clock_w = ui_measure(UiRole::Value, clock_line);
    const float clock_y =
        layout.header.y + (layout.header.height - static_cast<float>(ui_text_size(UiRole::Value))) * 0.5F;
    ui_draw(UiRole::Value, layout.header.x + layout.header.width - clock_w - 16.0F, clock_y,
            clock_line, kAccent);

    panel(layout.controls, nullptr);
    const PlaybackState state = snapshot.playback;
    draw_button(layout.start, "START", state == PlaybackState::Ready);
    draw_button(layout.pause, "PAUSE", state == PlaybackState::Running);
    draw_button(layout.resume, "RESUME", state == PlaybackState::Paused);
    draw_button(layout.step, "STEP", state != PlaybackState::Completed);
    draw_button(layout.reset, "RESET", true);
    char speed_label[32];
    std::snprintf(speed_label, sizeof(speed_label), "SPEED %s", to_string(snapshot.speed));
    draw_button(layout.speed, speed_label, true);

    draw_topology(layout, snapshot);
    draw_metrics(layout, snapshot);
    draw_timeline(layout, snapshot);
    draw_footer(layout, snapshot);
}

}  // namespace incidentlab
